#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <iryven/material.h>
#include <iryven/model.h>
#include <iryven/texture.h>

namespace Iryven {

class AssetManager {
public:
    [[nodiscard]] ModelHandle LoadModel(const std::filesystem::path& path);
    [[nodiscard]] MaterialHandle LoadMaterial(const std::filesystem::path& path);
    [[nodiscard]] TextureHandle LoadTexture(
        const std::filesystem::path& path,
        TextureColorSpace colorSpace = TextureColorSpace::SRGB);

    void StoreModel(const std::filesystem::path& path, ModelHandle model);
    void StoreMaterial(const std::filesystem::path& path, MaterialHandle material);
    void StoreTexture(const std::filesystem::path& path, TextureHandle texture);

    [[nodiscard]] ModelHandle GetModel(const std::filesystem::path& path) const;
    [[nodiscard]] MaterialHandle GetMaterial(const std::filesystem::path& path) const;
    [[nodiscard]] TextureHandle GetTexture(
        const std::filesystem::path& path,
        TextureColorSpace colorSpace = TextureColorSpace::SRGB) const;

    bool UnloadModel(const std::filesystem::path& path);
    bool UnloadMaterial(const std::filesystem::path& path);
    bool UnloadTexture(const std::filesystem::path& path);
    void Clear();

private:
    [[nodiscard]] static std::filesystem::path NormalizePath(const std::filesystem::path& path);

    mutable std::mutex mutex_;
    std::unordered_map<std::filesystem::path, ModelHandle> models_;
    std::unordered_map<std::filesystem::path, MaterialHandle> materials_;
    std::unordered_map<std::filesystem::path, TextureHandle> srgbTextures_;
    std::unordered_map<std::filesystem::path, TextureHandle> linearTextures_;
};

} // namespace Iryven
