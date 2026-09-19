#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <iryven/math/color.h>
#include <iryven/texture.h>

namespace Iryven {

struct Material {
    std::string name;
    std::filesystem::path source;
    Color baseColor = Color::White;
    float roughness = 0.0f;
    float metallic = 0.0f;
    Color emissive = Color::Black;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    std::uint32_t baseColorTexture = InvalidTextureIndex;
    std::uint32_t baseColorTexCoord = 0;
    std::uint32_t metallicRoughnessTexture = InvalidTextureIndex;
    std::uint32_t metallicRoughnessTexCoord = 0;
    std::uint32_t normalTexture = InvalidTextureIndex;
    std::uint32_t normalTexCoord = 0;
    std::uint32_t occlusionTexture = InvalidTextureIndex;
    std::uint32_t occlusionTexCoord = 0;
    std::uint32_t emissiveTexture = InvalidTextureIndex;
    std::uint32_t emissiveTexCoord = 0;
    // KHR_materials_pbrSpecularGlossiness. baseColor stores its diffuse input.
    bool specularGlossiness = false;
    Color specular = Color::White;
    float glossiness = 1.0f;
    std::uint32_t specularGlossinessTexture = InvalidTextureIndex;
    std::uint32_t specularGlossinessTexCoord = 0;
    // KHR_materials_transmission (thin surfaces; separate from alpha coverage).
    float transmission = 0.0f;
    std::uint32_t transmissionTexture = InvalidTextureIndex;
    std::uint32_t transmissionTexCoord = 0;
};

using MaterialHandle = std::shared_ptr<const Material>;

} // namespace Iryven
