// IRYVEN_SHADER_STAGES: FRAGMENT
// Stage selection is supplied by ShaderCompiler or the build rules.

#if defined(IRYVEN_STAGE_FRAGMENT)
// Explicit buffer offsets match the CPU resource layouts.
#include "shader_compat.hlsl"

#include "gltf_material.hlsl"

#include "light_buffer.hlsl"

IRYVEN_BINDING(2, 0)
ByteAddressBuffer materialBuffer : register(t2, space0);
IRYVEN_BINDING(0, 0)
ByteAddressBuffer lightBuffer : register(t0, space0);
IRYVEN_BINDING(3, 0)
ByteAddressBuffer lightTileBuffer : register(t3, space0);
IRYVEN_BINDING(4, 0)
ByteAddressBuffer lightDepthBinBuffer : register(t4, space0);
#include "frame_uniform.hlsl"

#ifdef __spirv__
struct DrawConstantsData
{
    IRYVEN_OFFSET(0) row_major float4x4 drawModel;
    IRYVEN_OFFSET(64) uint4 drawMeshletInfo;
};
IRYVEN_PUSH_CONSTANT ConstantBuffer<DrawConstantsData> iryvenPush;
#define drawModel iryvenPush.drawModel
#define drawMeshletInfo iryvenPush.drawMeshletInfo
#else
IRYVEN_PUSH_CONSTANT cbuffer DrawConstants : register(b0, space2147483647)
{
    row_major float4x4 drawModel : packoffset(c0);
    uint4 drawMeshletInfo : packoffset(c4);
};
#endif

IRYVEN_BINDING(0, 1)
Texture2D<float4> bindlessTextures[] : register(t0, space1);
IRYVEN_BINDING(1, 1)
SamplerState bindlessSamplers[] : register(s1, space1);

static float2 texCoord;
static float3 worldNormal;
static float4 worldTangent;
static float3 worldPosition;
static float4 vertexColor;
static float4 gBaseMetallic;
static float4 gNormalRoughness;
static float4 gEmissiveOcclusion;
static float4 gSpecular;
static uint meshletId;
static float3 meshletCenter;

struct StageInput
{
    IRYVEN_LOCATION(0) float2 texCoord : TEXCOORD0;
    IRYVEN_LOCATION(1) float4 vertexColor : TEXCOORD1;
    IRYVEN_LOCATION(2) float3 worldNormal : TEXCOORD2;
    IRYVEN_LOCATION(3) float4 worldTangent : TEXCOORD3;
    IRYVEN_PER_PRIMITIVE IRYVEN_LOCATION(4) nointerpolation uint meshletId : TEXCOORD4;
    IRYVEN_LOCATION(5) float3 worldPosition : TEXCOORD5;
    IRYVEN_PER_PRIMITIVE IRYVEN_LOCATION(6) nointerpolation float3 meshletCenter : TEXCOORD6;
};

struct StageOutput
{
    IRYVEN_LOCATION(0) float4 gBaseMetallic : SV_Target0;
    IRYVEN_LOCATION(1) float4 gNormalRoughness : SV_Target1;
    IRYVEN_LOCATION(2) float4 gEmissiveOcclusion : SV_Target2;
    IRYVEN_LOCATION(3) float4 gSpecular : SV_Target3;
};

float4 SampleBindlessTexture(uint textureReference)
{
    uint textureSlot = textureReference & 4095u;
    uint samplerSlot = textureReference >> 12u;
    return bindlessTextures[NonUniformResourceIndex(textureSlot)].Sample(
        bindlessSamplers[NonUniformResourceIndex(samplerSlot)], texCoord);
}

float3 EvaluateMaterialNormal(GpuMaterial material)
{
    float3 geometricNormal = normalize(worldNormal);
    if ((material.textureFlags & MATERIAL_HAS_NORMAL_TEXTURE) == 0u)
    {
        return geometricNormal;
    }
    float3 tangent;
    float3 bitangent;
    if (dot(worldTangent.xyz, worldTangent.xyz) > 1e-06f)
    {
        tangent = normalize(worldTangent.xyz - (geometricNormal * dot(geometricNormal, worldTangent.xyz)));
        float handedness = (worldTangent.w < 0.0f) ? (-1.0f) : 1.0f;
        bitangent = normalize(cross(geometricNormal, tangent)) * handedness;
    }
    else
    {
        float3 positionDx = ddx(worldPosition);
        float3 positionDy = ddy(worldPosition);
        float2 uvDx = ddx(texCoord);
        float2 uvDy = ddy(texCoord);
        float determinant = (uvDx.x * uvDy.y) - (uvDx.y * uvDy.x);
        if (abs(determinant) < 1e-06f)
        {
            return geometricNormal;
        }
        tangent = normalize(((positionDx * uvDy.y) - (positionDy * uvDx.y)) / determinant.xxx);
        tangent = normalize(tangent - (geometricNormal * dot(geometricNormal, tangent)));
        bitangent = normalize(cross(geometricNormal, tangent)) * ((determinant < 0.0f) ? (-1.0f) : 1.0f);
    }
    float3 tangentNormal = (SampleBindlessTexture(material.normalTextureIndex).xyz * 2.0f) - 1.0f.xxx;
    tangentNormal.xy *= material.normalScale;
    tangentNormal = normalize(tangentNormal);
    return normalize(((tangent * tangentNormal.x) + (bitangent * tangentNormal.y)) + (geometricNormal * tangentNormal.z));
}

void ShadeMaterial(uint materialIndex, uint phase)
{
    GpuMaterial material = LoadMaterial(materialBuffer, materialIndex);
    uint materialTextureFlags = material.textureFlags;
    float4 sampledBaseColor;
    if ((materialTextureFlags & MATERIAL_HAS_BASE_COLOR_TEXTURE) != 0u)
    {
        sampledBaseColor = SampleBindlessTexture(material.baseColorTextureIndex);
    }
    else
    {
        sampledBaseColor = 1.0f.xxxx;
    }
    float4 baseColor = (material.baseColorFactor * vertexColor) * sampledBaseColor;
    uint alphaMode = material.alphaMode;
    bool failsAlphaTest = (alphaMode == 1u) && (baseColor.w < material.alphaCutoff);
    if (failsAlphaTest)
    {
        discard;
    }
    float4 sampledMetallicRoughness;
    if ((materialTextureFlags & MATERIAL_HAS_METALLIC_ROUGHNESS_TEXTURE) != 0u)
    {
        sampledMetallicRoughness = SampleBindlessTexture(material.metallicRoughnessTextureIndex);
    }
    else
    {
        sampledMetallicRoughness = 1.0f.xxxx;
    }
    float metallic = clamp(material.metallicFactor * sampledMetallicRoughness.z, 0.0f, 1.0f);
    float roughness = clamp(material.roughnessFactor * sampledMetallicRoughness.y, 0.045f, 1.0f);
    bool specularGlossiness = material.materialWorkflow == 1u;
    float3 reflectanceAtNormal = lerp(0.04f.xxx, baseColor.xyz, metallic.xxx);
    if (specularGlossiness)
    {
        float4 sampledSpecularGlossiness;
        if ((materialTextureFlags & MATERIAL_HAS_SPECULAR_GLOSSINESS_TEXTURE) != 0u)
        {
            sampledSpecularGlossiness = SampleBindlessTexture(material.specularGlossinessTextureIndex);
        }
        else
        {
            sampledSpecularGlossiness = 1.0f.xxxx;
        }
        reflectanceAtNormal = clamp(material.specularFactor * sampledSpecularGlossiness.xyz, 0.0f.xxx, 1.0f.xxx);
        roughness = clamp(1.0f - (material.glossinessFactor * sampledSpecularGlossiness.w), 0.045f, 1.0f);
        metallic = 0.0f;
    }
    float sampledTransmission;
    if ((materialTextureFlags & MATERIAL_HAS_TRANSMISSION_TEXTURE) != 0u)
    {
        sampledTransmission = SampleBindlessTexture(material.transmissionTextureIndex).x;
    }
    else
    {
        sampledTransmission = 1.0f;
    }
    float transmission = clamp(material.transmissionFactor * sampledTransmission, 0.0f, 1.0f);
    if (specularGlossiness)
    {
        transmission = 0.0f;
    }
    float3 normal = EvaluateMaterialNormal(material);
    float filteredRoughness = CleanRoughness(normal, roughness);
    float ambientOcclusion;
    if ((materialTextureFlags & MATERIAL_HAS_OCCLUSION_TEXTURE) != 0u)
    {
        ambientOcclusion = lerp(1.0f, SampleBindlessTexture(material.occlusionTextureIndex).x, clamp(material.occlusionStrength, 0.0f, 1.0f));
    }
    else
    {
        ambientOcclusion = 1.0f;
    }
    float3 sampledEmissive;
    if ((materialTextureFlags & MATERIAL_HAS_EMISSIVE_TEXTURE) != 0u)
    {
        sampledEmissive = SampleBindlessTexture(material.emissiveTextureIndex).xyz;
    }
    else
    {
        sampledEmissive = 1.0f.xxx;
    }
    gBaseMetallic = float4(baseColor.xyz, metallic);
    gNormalRoughness = float4(normal, clamp(filteredRoughness, 0.045f, 1.0f));
    gEmissiveOcclusion = float4(material.emissiveFactor.xyz * sampledEmissive, ambientOcclusion);
    gSpecular = float4(reflectanceAtNormal, float(specularGlossiness));
}

void RunFragment()
{
    ShadeMaterial(drawMeshletInfo.y, 0u);
}

StageOutput main(StageInput stageInput)
{
    texCoord = stageInput.texCoord;
    worldNormal = stageInput.worldNormal;
    worldTangent = stageInput.worldTangent;
    worldPosition = stageInput.worldPosition;
    vertexColor = stageInput.vertexColor;
    meshletId = stageInput.meshletId;
    meshletCenter = stageInput.meshletCenter;
    RunFragment();
    StageOutput stageOutput;
    stageOutput.gBaseMetallic = gBaseMetallic;
    stageOutput.gNormalRoughness = gNormalRoughness;
    stageOutput.gEmissiveOcclusion = gEmissiveOcclusion;
    stageOutput.gSpecular = gSpecular;
    return stageOutput;
}

#else
#error Select a supported IRYVEN_STAGE_* macro for this shader
#endif
