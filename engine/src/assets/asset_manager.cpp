#include <iryven/asset_manager.h>

#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace Iryven {
namespace {

float ParseFloat(std::string_view value, const std::filesystem::path& path, std::size_t line) {
    float result = 0.0f;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) {
        throw std::runtime_error("Invalid number in '" + path.string() + "' at line " + std::to_string(line));
    }
    return result;
}

std::size_t ParseObjPosition(std::string_view token, std::size_t positionCount,
                             const std::filesystem::path& path, std::size_t line) {
    const std::size_t slash = token.find('/');
    token = token.substr(0, slash);
    int index = 0;
    const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), index);
    if (error != std::errc{} || end != token.data() + token.size() || index == 0) {
        throw std::runtime_error("Invalid OBJ face index in '" + path.string() + "' at line " + std::to_string(line));
    }
    const auto resolved = index > 0 ? static_cast<long long>(index - 1)
                                    : static_cast<long long>(positionCount) + index;
    if (resolved < 0 || resolved >= static_cast<long long>(positionCount)) {
        throw std::runtime_error("OBJ face index out of range in '" + path.string() + "' at line " + std::to_string(line));
    }
    return static_cast<std::size_t>(resolved);
}

std::shared_ptr<const MeshData> LoadObj(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Could not open model '" + path.string() + "'");
    }

    std::vector<Vertex> positions;
    std::vector<std::uint32_t> indices;
    std::string lineText;
    std::size_t lineNumber = 0;
    while (std::getline(input, lineText)) {
        ++lineNumber;
        std::istringstream line(lineText);
        std::string kind;
        line >> kind;
        if (kind.empty() || kind[0] == '#') continue;
        if (kind == "v") {
            std::string x, y, z;
            if (!(line >> x >> y >> z)) {
                throw std::runtime_error("Invalid OBJ vertex in '" + path.string() + "' at line " + std::to_string(lineNumber));
            }
            positions.push_back(Vertex{
                .position = { ParseFloat(x, path, lineNumber), ParseFloat(y, path, lineNumber), ParseFloat(z, path, lineNumber) }
            });
        } else if (kind == "f") {
            std::vector<std::uint32_t> face;
            std::string token;
            while (line >> token) {
                face.push_back(static_cast<std::uint32_t>(ParseObjPosition(token, positions.size(), path, lineNumber)));
            }
            if (face.size() < 3) {
                throw std::runtime_error("OBJ face has fewer than three vertices in '" + path.string() + "' at line " + std::to_string(lineNumber));
            }
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                indices.insert(indices.end(), { face[0], face[i], face[i + 1] });
            }
        }
    }
    if (positions.empty() || indices.empty()) {
        throw std::runtime_error("Model '" + path.string() + "' contains no renderable geometry");
    }
    return std::make_shared<const MeshData>(MeshData{ std::move(positions), std::move(indices) });
}

MaterialHandle LoadMaterialFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Could not open material '" + path.string() + "'");
    auto material = std::make_shared<Material>();
    material->source = path;
    material->name = path.stem().string();
    std::string text;
    std::size_t lineNumber = 0;
    while (std::getline(input, text)) {
        ++lineNumber;
        const auto first = text.find_first_not_of(" \t\r");
        if (first == std::string::npos || text[first] == '#') continue;
        std::istringstream line(text.substr(first));
        std::string key;
        line >> key;
        if (key == "name") {
            std::getline(line >> std::ws, material->name);
        } else if (key == "base_color") {
            std::string r, g, b, a;
            if (!(line >> r >> g >> b)) throw std::runtime_error("Invalid base_color in '" + path.string() + "' at line " + std::to_string(lineNumber));
            const float alpha = line >> a ? ParseFloat(a, path, lineNumber) : 1.0f;
            material->baseColor = Color(ParseFloat(r, path, lineNumber), ParseFloat(g, path, lineNumber), ParseFloat(b, path, lineNumber), alpha);
        } else {
            throw std::runtime_error("Unknown material property '" + key + "' in '" + path.string() + "' at line " + std::to_string(lineNumber));
        }
    }
    return material;
}

} // namespace

std::filesystem::path AssetManager::NormalizePath(const std::filesystem::path& path) {
    if (path.empty()) throw std::invalid_argument("Asset path must not be empty");
    std::error_code error;
    auto normalized = std::filesystem::weakly_canonical(path, error);
    return error ? std::filesystem::absolute(path).lexically_normal() : normalized;
}

ModelHandle AssetManager::LoadModel(const std::filesystem::path& path) {
    const auto key = NormalizePath(path);
    if (auto existing = GetModel(key)) return existing;
    if (key.extension() != ".obj") throw std::runtime_error("Unsupported model format '" + key.extension().string() + "' (expected .obj)");
    auto model = std::make_shared<Model>(Model{ key, LoadObj(key) });
    std::scoped_lock lock(mutex_);
    const auto [iterator, inserted] = models_.try_emplace(key, model);
    return iterator->second;
}

MaterialHandle AssetManager::LoadMaterial(const std::filesystem::path& path) {
    const auto key = NormalizePath(path);
    if (auto existing = GetMaterial(key)) return existing;
    if (key.extension() != ".material") throw std::runtime_error("Unsupported material format '" + key.extension().string() + "' (expected .material)");
    auto material = LoadMaterialFile(key);
    std::scoped_lock lock(mutex_);
    const auto [iterator, inserted] = materials_.try_emplace(key, std::move(material));
    return iterator->second;
}

void AssetManager::StoreModel(const std::filesystem::path& path, ModelHandle model) {
    if (!model || !model->mesh) throw std::invalid_argument("Cannot store an empty model");
    std::scoped_lock lock(mutex_); models_.insert_or_assign(NormalizePath(path), std::move(model));
}
void AssetManager::StoreMaterial(const std::filesystem::path& path, MaterialHandle material) {
    if (!material) throw std::invalid_argument("Cannot store an empty material");
    std::scoped_lock lock(mutex_); materials_.insert_or_assign(NormalizePath(path), std::move(material));
}
ModelHandle AssetManager::GetModel(const std::filesystem::path& path) const {
    const auto key = NormalizePath(path); std::scoped_lock lock(mutex_);
    const auto found = models_.find(key); return found == models_.end() ? nullptr : found->second;
}
MaterialHandle AssetManager::GetMaterial(const std::filesystem::path& path) const {
    const auto key = NormalizePath(path); std::scoped_lock lock(mutex_);
    const auto found = materials_.find(key); return found == materials_.end() ? nullptr : found->second;
}
bool AssetManager::UnloadModel(const std::filesystem::path& path) { std::scoped_lock lock(mutex_); return models_.erase(NormalizePath(path)) != 0; }
bool AssetManager::UnloadMaterial(const std::filesystem::path& path) { std::scoped_lock lock(mutex_); return materials_.erase(NormalizePath(path)) != 0; }
void AssetManager::Clear() { std::scoped_lock lock(mutex_); models_.clear(); materials_.clear(); }

} // namespace Iryven
