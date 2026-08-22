#include <iryven/asset_manager.h>

#include "model_importers.h"

#include <charconv>
#include <fstream>
#include <map>
#include <optional>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <glm/geometric.hpp>
#include <glm/common.hpp>


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
				}
				else if (key == "base_color") {
					std::string r, g, b, a;
					if (!(line >> r >> g >> b)) throw std::runtime_error("Invalid base_color in '" + path.string() + "' at line " + std::to_string(lineNumber));
					const float alpha = line >> a ? ParseFloat(a, path, lineNumber) : 1.0f;
					material->baseColor = Color(ParseFloat(r, path, lineNumber), ParseFloat(g, path, lineNumber), ParseFloat(b, path, lineNumber), alpha);
				}
				else {
					throw std::runtime_error("Unknown material property '" + key + "' in '" + path.string() + "' at line " + std::to_string(lineNumber));
				}
			}
			return material;
		}

		TextureHandle LoadTextureFile(
			const std::filesystem::path& path,
			TextureColorSpace colorSpace) {
			int width = 0;
			int height = 0;
			int sourceChannels = 0;
			stbi_uc* decoded = stbi_load(
				path.string().c_str(), &width, &height, &sourceChannels, STBI_rgb_alpha);
			if (!decoded) {
				const char* reason = stbi_failure_reason();
				throw std::runtime_error(
					"Could not decode texture '" + path.string() + "': " +
					(reason ? reason : "unknown image error"));
			}

			if (width <= 0 || height <= 0) {
				stbi_image_free(decoded);
				throw std::runtime_error("Texture '" + path.string() + "' has invalid dimensions");
			}

			const std::size_t byteCount =
				static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
			std::vector<std::uint8_t> pixels(decoded, decoded + byteCount);
			stbi_image_free(decoded);

			return std::make_shared<const Texture>(Texture{
				.source = path,
				.width = static_cast<std::uint32_t>(width),
				.height = static_cast<std::uint32_t>(height),
				.format = TextureFormat::RGBA8,
				.colorSpace = colorSpace,
				.pixels = std::move(pixels),
				});
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
		ModelHandle model;
		const auto extension = key.extension();
		if (extension == ".obj") {
			model = Importers::ImportObj(key);
		}
		else if (extension == ".glb" || extension == ".gltf") {
			model = Importers::ImportGltf(key);
		}
		else {
			throw std::runtime_error("Unsupported model format '" + extension.string() + "' (expected .obj, .gltf, or .glb)");
		}

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

	TextureHandle AssetManager::LoadTexture(
		const std::filesystem::path& path,
		TextureColorSpace colorSpace) {
		const auto key = NormalizePath(path);
		if (auto existing = GetTexture(key, colorSpace)) return existing;
		auto texture = LoadTextureFile(key, colorSpace);
		std::scoped_lock lock(mutex_);
		auto& textures = colorSpace == TextureColorSpace::SRGB
			? srgbTextures_ : linearTextures_;
		const auto [iterator, inserted] = textures.try_emplace(key, std::move(texture));
		return iterator->second;
	}

	void AssetManager::StoreModel(const std::filesystem::path& path, ModelHandle model) {
		if (!model || !model->IsValid()) throw std::invalid_argument("Cannot store an invalid model");
		std::scoped_lock lock(mutex_); models_.insert_or_assign(NormalizePath(path), std::move(model));
	}
	void AssetManager::StoreMaterial(const std::filesystem::path& path, MaterialHandle material) {
		if (!material) throw std::invalid_argument("Cannot store an empty material");
		std::scoped_lock lock(mutex_); materials_.insert_or_assign(NormalizePath(path), std::move(material));
	}
	void AssetManager::StoreTexture(const std::filesystem::path& path, TextureHandle texture) {
		if (!texture || !texture->IsValid()) throw std::invalid_argument("Cannot store an invalid texture");
		std::scoped_lock lock(mutex_);
		auto& textures = texture->colorSpace == TextureColorSpace::SRGB
			? srgbTextures_ : linearTextures_;
		textures.insert_or_assign(NormalizePath(path), std::move(texture));
	}
	ModelHandle AssetManager::GetModel(const std::filesystem::path& path) const {
		const auto key = NormalizePath(path); std::scoped_lock lock(mutex_);
		const auto found = models_.find(key); return found == models_.end() ? nullptr : found->second;
	}
	MaterialHandle AssetManager::GetMaterial(const std::filesystem::path& path) const {
		const auto key = NormalizePath(path); std::scoped_lock lock(mutex_);
		const auto found = materials_.find(key); return found == materials_.end() ? nullptr : found->second;
	}
	TextureHandle AssetManager::GetTexture(
		const std::filesystem::path& path,
		TextureColorSpace colorSpace) const {
		const auto key = NormalizePath(path);
		std::scoped_lock lock(mutex_);
		const auto& textures = colorSpace == TextureColorSpace::SRGB
			? srgbTextures_ : linearTextures_;
		const auto found = textures.find(key);
		return found == textures.end() ? nullptr : found->second;
	}
	bool AssetManager::UnloadModel(const std::filesystem::path& path) { std::scoped_lock lock(mutex_); return models_.erase(NormalizePath(path)) != 0; }
	bool AssetManager::UnloadMaterial(const std::filesystem::path& path) { std::scoped_lock lock(mutex_); return materials_.erase(NormalizePath(path)) != 0; }
	bool AssetManager::UnloadTexture(const std::filesystem::path& path) {
		const auto key = NormalizePath(path);
		std::scoped_lock lock(mutex_);
		return srgbTextures_.erase(key) + linearTextures_.erase(key) != 0;
	}
	void AssetManager::Clear() {
		std::scoped_lock lock(mutex_);
		models_.clear();
		materials_.clear();
		srgbTextures_.clear();
		linearTextures_.clear();
	}

} // namespace Iryven
