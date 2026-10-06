#include "iry_asset.h"

#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <streambuf>
#include <type_traits>

#include <iryven/log.h>

namespace Iryven::IryAsset {
namespace {

constexpr std::array<char, 8> Magic{'I','R','Y','A','S','S','E','T'};
constexpr std::uint32_t Version = 2;
constexpr std::uint32_t ModelType = 1;
constexpr std::uint64_t MaxElements = 1ull << 30;

class MemoryInputBuffer final : public std::streambuf {
public:
    explicit MemoryInputBuffer(std::vector<char>& bytes)
    {
        char* begin = bytes.data();
        setg(begin, begin, begin + bytes.size());
    }
};

template<typename T> void Write(std::ostream& stream, const T& value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
    if (!stream) throw std::runtime_error("Failed to write .iryasset");
}

template<typename T> T Read(std::istream& stream)
{
    static_assert(std::is_trivially_copyable_v<T>);
    T value{};
    stream.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!stream) throw std::runtime_error("Truncated .iryasset");
    return value;
}

void WriteString(std::ostream& stream, const std::string& value)
{
    Write(stream, static_cast<std::uint64_t>(value.size()));
    stream.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!stream) throw std::runtime_error("Failed to write .iryasset string");
}

std::string ReadString(std::istream& stream)
{
    const auto size = Read<std::uint64_t>(stream);
    if (size > MaxElements) throw std::runtime_error("Invalid .iryasset string size");
    std::string value(static_cast<std::size_t>(size), '\0');
    stream.read(value.data(), static_cast<std::streamsize>(size));
    if (!stream) throw std::runtime_error("Truncated .iryasset string");
    return value;
}

template<typename T> void WritePodVector(std::ostream& stream, const std::vector<T>& values)
{
    static_assert(std::is_trivially_copyable_v<T>);
    Write(stream, static_cast<std::uint64_t>(values.size()));
    stream.write(reinterpret_cast<const char*>(values.data()),
        static_cast<std::streamsize>(values.size() * sizeof(T)));
    if (!stream) throw std::runtime_error("Failed to write .iryasset array");
}

template<typename T> std::vector<T> ReadPodVector(std::istream& stream)
{
    static_assert(std::is_trivially_copyable_v<T>);
    const auto count = Read<std::uint64_t>(stream);
    if (count > MaxElements || count > MaxElements / sizeof(T))
        throw std::runtime_error("Invalid .iryasset array size");
    std::vector<T> values(static_cast<std::size_t>(count));
    stream.read(reinterpret_cast<char*>(values.data()),
        static_cast<std::streamsize>(values.size() * sizeof(T)));
    if (!stream) throw std::runtime_error("Truncated .iryasset array");
    return values;
}

void WriteMeshlet(std::ostream& stream, const Meshlet& meshlet)
{
    WritePodVector(stream, meshlet.vertexIndices);
    WritePodVector(stream, meshlet.triangleIndices);
    Write(stream, meshlet.center); Write(stream, meshlet.radius);
    Write(stream, meshlet.coneApex); Write(stream, meshlet.coneAxis); Write(stream, meshlet.coneCutoff);
}

Meshlet ReadMeshlet(std::istream& stream)
{
    Meshlet result;
    result.vertexIndices = ReadPodVector<std::uint32_t>(stream);
    result.triangleIndices = ReadPodVector<std::uint32_t>(stream);
    result.center = Read<glm::vec3>(stream); result.radius = Read<float>(stream);
    result.coneApex = Read<glm::vec3>(stream); result.coneAxis = Read<glm::vec3>(stream);
    result.coneCutoff = Read<float>(stream);
    return result;
}

void WriteMaterial(std::ostream& stream, const Material& value)
{
    WriteString(stream, value.name); WriteString(stream, value.source.generic_string());
    Write(stream, value.baseColor); Write(stream, value.roughness); Write(stream, value.metallic);
    Write(stream, value.emissive); Write(stream, value.normalScale); Write(stream, value.occlusionStrength);
    Write(stream, value.alphaMode); Write(stream, value.alphaCutoff); Write(stream, value.doubleSided);
    Write(stream, value.baseColorTexture); Write(stream, value.baseColorTexCoord);
    Write(stream, value.metallicRoughnessTexture); Write(stream, value.metallicRoughnessTexCoord);
    Write(stream, value.normalTexture); Write(stream, value.normalTexCoord);
    Write(stream, value.occlusionTexture); Write(stream, value.occlusionTexCoord);
    Write(stream, value.emissiveTexture); Write(stream, value.emissiveTexCoord);
    Write(stream, value.specularGlossiness); Write(stream, value.specular); Write(stream, value.glossiness);
    Write(stream, value.specularGlossinessTexture); Write(stream, value.specularGlossinessTexCoord);
    Write(stream, value.transmission); Write(stream, value.transmissionTexture); Write(stream, value.transmissionTexCoord);
}

Material ReadMaterial(std::istream& stream)
{
    Material value;
    value.name = ReadString(stream); value.source = ReadString(stream);
    value.baseColor = Read<Color>(stream); value.roughness = Read<float>(stream); value.metallic = Read<float>(stream);
    value.emissive = Read<Color>(stream); value.normalScale = Read<float>(stream); value.occlusionStrength = Read<float>(stream);
    value.alphaMode = Read<MaterialAlphaMode>(stream); value.alphaCutoff = Read<float>(stream); value.doubleSided = Read<bool>(stream);
    value.baseColorTexture = Read<std::uint32_t>(stream); value.baseColorTexCoord = Read<std::uint32_t>(stream);
    value.metallicRoughnessTexture = Read<std::uint32_t>(stream); value.metallicRoughnessTexCoord = Read<std::uint32_t>(stream);
    value.normalTexture = Read<std::uint32_t>(stream); value.normalTexCoord = Read<std::uint32_t>(stream);
    value.occlusionTexture = Read<std::uint32_t>(stream); value.occlusionTexCoord = Read<std::uint32_t>(stream);
    value.emissiveTexture = Read<std::uint32_t>(stream); value.emissiveTexCoord = Read<std::uint32_t>(stream);
    value.specularGlossiness = Read<bool>(stream); value.specular = Read<Color>(stream); value.glossiness = Read<float>(stream);
    value.specularGlossinessTexture = Read<std::uint32_t>(stream); value.specularGlossinessTexCoord = Read<std::uint32_t>(stream);
    value.transmission = Read<float>(stream); value.transmissionTexture = Read<std::uint32_t>(stream);
    value.transmissionTexCoord = Read<std::uint32_t>(stream);
    return value;
}

}

void WriteModel(const std::filesystem::path& path, const Model& model)
{
    if (!model.IsValid()) throw std::invalid_argument("Cannot write invalid model asset");
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("Failed to create asset: " + path.string());
    stream.write(Magic.data(), Magic.size()); Write(stream, Version); Write(stream, ModelType);
    WritePodVector(stream, model.vertices); WritePodVector(stream, model.indices);
    Write(stream, static_cast<std::uint64_t>(model.meshes.size()));
    for (const auto& mesh : model.meshes) {
        WriteString(stream, mesh.name); Write(stream, mesh.bounds);
        Write(stream, static_cast<std::uint64_t>(mesh.primitives.size()));
        for (const auto& primitive : mesh.primitives) {
            Write(stream, primitive.firstIndex); Write(stream, primitive.indexCount);
            Write(stream, primitive.vertexOffset); Write(stream, primitive.materialIndex);
            Write(stream, primitive.bounds); Write(stream, primitive.boundingSphere);
            Write(stream, static_cast<std::uint64_t>(primitive.meshlets.size()));
            for (const auto& meshlet : primitive.meshlets) WriteMeshlet(stream, meshlet);
        }
    }
    Write(stream, static_cast<std::uint64_t>(model.materials.size()));
    for (const auto& material : model.materials) WriteMaterial(stream, *material);
    WritePodVector(stream, model.textureRegistry.samplers);
    Write(stream, static_cast<std::uint64_t>(model.textureRegistry.textures.size()));
    for (const auto& registered : model.textureRegistry.textures) {
        const auto& texture = *registered.texture;
        Write(stream, registered.samplerIndex); WriteString(stream, texture.source.generic_string());
        Write(stream, texture.width); Write(stream, texture.height); Write(stream, texture.format);
        Write(stream, texture.colorSpace); WritePodVector(stream, texture.pixels);
    }
    Write(stream, static_cast<std::uint64_t>(model.nodes.size()));
    for (const auto& node : model.nodes) {
        WriteString(stream, node.name); Write(stream, node.localTransform); Write(stream, node.meshIndex);
        WritePodVector(stream, node.children);
    }
    WritePodVector(stream, model.sceneRoots);
}

ModelHandle ReadModel(const std::filesystem::path& path)
{
    const auto diskStart = std::chrono::steady_clock::now();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Failed to open asset: " + path.string());
    const std::streampos end = file.tellg();
    if (end < 0 || static_cast<std::uintmax_t>(end) >
            static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("Asset is too large to read: " + path.string());
    }
    std::vector<char> bytes(static_cast<std::size_t>(end));
    file.seekg(0, std::ios::beg);
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) throw std::runtime_error("Failed to read asset: " + path.string());
    const double diskMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - diskStart).count();

    MemoryInputBuffer inputBuffer(bytes);
    std::istream stream(&inputBuffer);
    const auto deserializeStart = std::chrono::steady_clock::now();
    std::array<char, 8> magic{}; stream.read(magic.data(), magic.size());
    if (magic != Magic || Read<std::uint32_t>(stream) != Version || Read<std::uint32_t>(stream) != ModelType)
        throw std::runtime_error("Unsupported or corrupt .iryasset: " + path.string());
    auto model = std::make_shared<Model>(); model->source = path;
    model->vertices = ReadPodVector<Vertex>(stream); model->indices = ReadPodVector<std::uint32_t>(stream);
    const auto meshCount = Read<std::uint64_t>(stream); if (meshCount > MaxElements) throw std::runtime_error("Invalid mesh count");
    model->meshes.resize(static_cast<std::size_t>(meshCount));
    for (auto& mesh : model->meshes) {
        mesh.name = ReadString(stream); mesh.bounds = Read<BoundingBox>(stream);
        const auto count = Read<std::uint64_t>(stream); if (count > MaxElements) throw std::runtime_error("Invalid primitive count");
        mesh.primitives.resize(static_cast<std::size_t>(count));
        for (auto& primitive : mesh.primitives) {
            primitive.firstIndex = Read<std::uint32_t>(stream); primitive.indexCount = Read<std::uint32_t>(stream);
            primitive.vertexOffset = Read<std::int32_t>(stream); primitive.materialIndex = Read<std::uint32_t>(stream);
            primitive.bounds = Read<BoundingBox>(stream); primitive.boundingSphere = Read<BoundingSphere>(stream);
            const auto meshletCount = Read<std::uint64_t>(stream); if (meshletCount > MaxElements) throw std::runtime_error("Invalid meshlet count");
            primitive.meshlets.reserve(static_cast<std::size_t>(meshletCount));
            for (std::uint64_t i = 0; i < meshletCount; ++i) primitive.meshlets.push_back(ReadMeshlet(stream));
        }
    }
    const auto materialCount = Read<std::uint64_t>(stream); if (materialCount > MaxElements) throw std::runtime_error("Invalid material count");
    for (std::uint64_t i = 0; i < materialCount; ++i) model->materials.push_back(std::make_shared<const Material>(ReadMaterial(stream)));
    model->textureRegistry.samplers = ReadPodVector<TextureSampler>(stream);
    const auto textureCount = Read<std::uint64_t>(stream); if (textureCount > MaxElements) throw std::runtime_error("Invalid texture count");
    for (std::uint64_t i = 0; i < textureCount; ++i) {
        RegisteredTexture registered; registered.samplerIndex = Read<std::uint32_t>(stream);
        auto texture = std::make_shared<Texture>(); texture->source = ReadString(stream);
        texture->width = Read<std::uint32_t>(stream); texture->height = Read<std::uint32_t>(stream);
        texture->format = Read<TextureFormat>(stream); texture->colorSpace = Read<TextureColorSpace>(stream);
        texture->pixels = ReadPodVector<std::uint8_t>(stream); registered.texture = std::move(texture);
        model->textureRegistry.textures.push_back(std::move(registered));
    }
    const auto nodeCount = Read<std::uint64_t>(stream); if (nodeCount > MaxElements) throw std::runtime_error("Invalid node count");
    model->nodes.resize(static_cast<std::size_t>(nodeCount));
    for (auto& node : model->nodes) {
        node.name = ReadString(stream); node.localTransform = Read<glm::mat4>(stream);
        node.meshIndex = Read<std::uint32_t>(stream); node.children = ReadPodVector<std::uint32_t>(stream);
    }
    model->sceneRoots = ReadPodVector<std::uint32_t>(stream);
    if (!model->IsValid()) throw std::runtime_error("Invalid model data in .iryasset: " + path.string());
    const double deserializeMilliseconds =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - deserializeStart).count();
    IRYVEN_CORE_INFO(
        ".iryasset '{}': disk read {:.2f} ms, deserialization {:.2f} ms, {:.2f} MiB",
        path.generic_string(), diskMilliseconds, deserializeMilliseconds,
        static_cast<double>(bytes.size()) / (1024.0 * 1024.0));
    return model;
}

}
