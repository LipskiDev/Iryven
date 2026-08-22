#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace Iryven {

enum class TextureFormat {
    RGBA8,
};

enum class TextureColorSpace {
    Linear,
    SRGB,
};

enum class TextureFilter {
    Nearest,
    Linear,
    NearestMipmapNearest,
    LinearMipmapNearest,
    NearestMipmapLinear,
    LinearMipmapLinear,
};

enum class TextureWrap {
    Repeat,
    ClampToEdge,
    MirroredRepeat,
};

struct TextureSampler {
    TextureFilter minFilter = TextureFilter::LinearMipmapLinear;
    TextureFilter magFilter = TextureFilter::Linear;
    TextureWrap wrapU = TextureWrap::Repeat;
    TextureWrap wrapV = TextureWrap::Repeat;
    bool generateMipmaps = true;
};

struct Texture {
    std::filesystem::path source;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormat format = TextureFormat::RGBA8;
    TextureColorSpace colorSpace = TextureColorSpace::Linear;
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] std::size_t ExpectedByteSize() const noexcept
    {
        return static_cast<std::size_t>(width) * height * 4;
    }

    [[nodiscard]] bool IsValid() const noexcept
    {
        return width > 0 && height > 0 && pixels.size() == ExpectedByteSize();
    }
};

using TextureHandle = std::shared_ptr<const Texture>;

inline constexpr std::uint32_t InvalidTextureIndex =
    std::numeric_limits<std::uint32_t>::max();

struct RegisteredTexture {
    TextureHandle texture;
    std::uint32_t samplerIndex = InvalidTextureIndex;
};

struct TextureRegistry {
    std::vector<RegisteredTexture> textures;
    std::vector<TextureSampler> samplers;

    [[nodiscard]] bool IsValid() const noexcept
    {
        for (const RegisteredTexture& entry : textures) {
            if (!entry.texture || !entry.texture->IsValid() ||
                entry.samplerIndex >= samplers.size()) return false;
        }
        return true;
    }
};

} // namespace Iryven
