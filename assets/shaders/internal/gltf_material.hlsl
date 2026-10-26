#ifndef IRYVEN_GLTF_MATERIAL_HLSL
#define IRYVEN_GLTF_MATERIAL_HLSL

static const uint MATERIAL_HAS_BASE_COLOR_TEXTURE = 1u;
static const uint MATERIAL_HAS_METALLIC_ROUGHNESS_TEXTURE = 2u;
static const uint MATERIAL_HAS_NORMAL_TEXTURE = 4u;
static const uint MATERIAL_HAS_OCCLUSION_TEXTURE = 8u;
static const uint MATERIAL_HAS_EMISSIVE_TEXTURE = 16u;
static const uint MATERIAL_HAS_SPECULAR_GLOSSINESS_TEXTURE = 32u;
static const uint MATERIAL_HAS_TRANSMISSION_TEXTURE = 64u;

struct GpuMaterial
{
    float4 baseColorFactor;
    float3 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    uint baseColorTextureIndex;
    uint metallicRoughnessTextureIndex;
    uint normalTextureIndex;
    uint occlusionTextureIndex;
    uint emissiveTextureIndex;
    uint textureFlags;
    float3 specularFactor;
    float glossinessFactor;
    float transmissionFactor;
    float alphaCutoff;
    uint specularGlossinessTextureIndex;
    uint transmissionTextureIndex;
    uint materialWorkflow;
    uint alphaMode;
};

GpuMaterial LoadMaterial(ByteAddressBuffer materialBuffer, uint materialIndex)
{
    uint materialByteOffset = materialIndex * 128u;
    float4 surfaceFactors = materialBuffer.Load<float4>(materialByteOffset + 32u);
    uint4 surfaceTextureIndices = materialBuffer.Load<uint4>(materialByteOffset + 48u);
    uint4 emissiveTextureAndFlags = materialBuffer.Load<uint4>(materialByteOffset + 64u);
    float4 specularGlossinessFactors = materialBuffer.Load<float4>(materialByteOffset + 80u);
    float4 transmissionAndAlphaCutoff = materialBuffer.Load<float4>(materialByteOffset + 96u);
    uint4 extensionTexturesAndModes = materialBuffer.Load<uint4>(materialByteOffset + 112u);

    GpuMaterial material;
    material.baseColorFactor = materialBuffer.Load<float4>(materialByteOffset);
    material.emissiveFactor = materialBuffer.Load<float4>(materialByteOffset + 16u).xyz;
    material.metallicFactor = surfaceFactors.x;
    material.roughnessFactor = surfaceFactors.y;
    material.normalScale = surfaceFactors.z;
    material.occlusionStrength = surfaceFactors.w;
    material.baseColorTextureIndex = surfaceTextureIndices.x;
    material.metallicRoughnessTextureIndex = surfaceTextureIndices.y;
    material.normalTextureIndex = surfaceTextureIndices.z;
    material.occlusionTextureIndex = surfaceTextureIndices.w;
    material.emissiveTextureIndex = emissiveTextureAndFlags.x;
    material.textureFlags = emissiveTextureAndFlags.y;
    material.specularFactor = specularGlossinessFactors.xyz;
    material.glossinessFactor = specularGlossinessFactors.w;
    material.transmissionFactor = transmissionAndAlphaCutoff.x;
    material.alphaCutoff = transmissionAndAlphaCutoff.y;
    material.specularGlossinessTextureIndex = extensionTexturesAndModes.x;
    material.transmissionTextureIndex = extensionTexturesAndModes.y;
    material.materialWorkflow = extensionTexturesAndModes.z;
    material.alphaMode = extensionTexturesAndModes.w;
    return material;
}

float CleanRoughness(float3 surfaceNormal, float perceptualRoughness)
{
    float3 ddxNormal = ddx(surfaceNormal);
    float3 ddyNormal = ddy(surfaceNormal);
    float pixelVariance = dot(ddxNormal, ddxNormal) + dot(ddyNormal, ddyNormal);
    float alpha = perceptualRoughness * perceptualRoughness;
    float kernelAlpha = min(0.25f, pixelVariance);
    float cleanedAlpha = sqrt(alpha + kernelAlpha);
    return cleanedAlpha;
}
#endif
