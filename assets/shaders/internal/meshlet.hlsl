// IRYVEN_SHADER_STAGES: FRAGMENT MESH TASK
// Stage selection is supplied by ShaderCompiler or the build rules.

#if defined(IRYVEN_STAGE_FRAGMENT)
#include "lighting.hlsl"
// Explicit buffer offsets match the CPU resource layouts.
#include "shader_compat.hlsl"

#include "gltf_material.hlsl"

#include "light_buffer.hlsl"
#include "light_depth_bins.hlsl"

IRYVEN_BINDING(4, 0)
ByteAddressBuffer lightDepthBinBuffer : register(t4, space0);
IRYVEN_BINDING(0, 0)
ByteAddressBuffer lightBuffer : register(t0, space0);
IRYVEN_BINDING(3, 0)
ByteAddressBuffer lightTileBuffer : register(t3, space0);
IRYVEN_BINDING(2, 0)
ByteAddressBuffer materialBuffer : register(t2, space0);
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

static float4 fragmentPosition;
static float2 texCoord;
static float3 worldNormal;
static float4 worldTangent;
static float3 worldPosition;
static float4 vertexColor;
static float4 outputColor;
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
    float4 fragmentPosition : SV_Position;
};

struct StageOutput
{
    IRYVEN_LOCATION(0) float4 outputColor : SV_Target0;
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

uint2 LocalLightDepthRange(float3 surfacePosition, uint localLightCount)
{
    return IryvenLocalLightDepthRange(lightDepthBinBuffer, surfacePosition, localLightCount);
}

float3 EvaluateDirectLighting(
    float3 surfacePosition,
    float3 normal,
    float3 viewDirection,
    float3 baseColor,
    float metallic,
    float roughness,
    float ambientOcclusion,
    float3 reflectanceAtNormal,
    bool specularGlossiness,
    float transmission,
    bool countLights,
    bool showShadowTiers)
{
    float diffuseWeight;
    if (specularGlossiness)
    {
        diffuseWeight = 1.0f - max(max(reflectanceAtNormal.x, reflectanceAtNormal.y), reflectanceAtNormal.z);
    }
    else
    {
        diffuseWeight = 1.0f - metallic;
    }
    float3 lighting;
    if (showShadowTiers)
    {
        lighting = 0.0f.xxx;
    }
    else
    {
        lighting = (((baseColor * diffuseWeight) * (1.0f - transmission)) * 0.05f) * ambientOcclusion;
    }
    float normalDotView = max(dot(normal, viewDirection), 0.0001f);
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float roughnessRadius = roughness + 1.0f;
    float geometryFactor = (roughnessRadius * roughnessRadius) / 8.0f;
    float viewGeometry = GeometrySchlickGGX(normalDotView, geometryFactor);
    float3 diffuseBase = (baseColor * (diffuseWeight * (1.0f - transmission))) / 3.1415927f.xxx;
    uint2 localLightRange;
    if (lightBuffer.Load<uint>(12) == 0u)
    {
        localLightRange = uint2(0u, 0u);
    }
    else
    {
        localLightRange = LocalLightDepthRange(surfacePosition, (lightBuffer.Load<uint>(12)));
    }
    uint2 lightTile = uint2(fragmentPosition.xy) / uint2(8u, 8u);
    uint tileMaskOffset = ((lightTile.y * lightTileBuffer.Load<uint>(0)) + lightTile.x) * 16u;
    uint lightMaskWord = localLightRange.x / 32u;
    uint lightMaskWordEnd;
    if (localLightRange.x < localLightRange.y)
    {
        lightMaskWordEnd = (localLightRange.y + 31u) / 32u;
    }
    else
    {
        lightMaskWordEnd = lightMaskWord;
    }
    uint affectingLights = 0u;
    float dominantLightImpact = -1.0f;
    uint dominantShadowTier = 4u;
    uint remainingLightBits = 0u;
    uint tileLightMask = 0u;
    uint firstLightInWord = 0u;
    uint nextDirectionalLight = 0u;
    uint lightIndex;
    float3 toLight;
    float3 diffuseEnergyWeight;
    uint lightShadowTier;
    for (;;)
    {
        bool lightIsSelected = true;
        if (nextDirectionalLight < lightBuffer.Load<uint>(4))
        {
            nextDirectionalLight = nextDirectionalLight + uint(1);
            lightIndex = nextDirectionalLight;
        }
        else
        {
            while ((remainingLightBits == 0u) && (lightMaskWord < lightMaskWordEnd))
            {
                firstLightInWord = lightMaskWord * 32u;
                uint firstLightIndex = max(localLightRange.x, firstLightInWord);
                uint rangeEnd = min(localLightRange.y, (firstLightInWord + 32u));
                tileLightMask = 0u;
                bool canReadLightMask = firstLightIndex < rangeEnd && !IsHelperLane();
                if (canReadLightMask)
                {
                    tileLightMask = (lightTileBuffer.Load<uint>((tileMaskOffset + lightMaskWord) * 4 + 16) & (4294967295u << (firstLightIndex - firstLightInWord))) & (4294967295u >> (32u - (rangeEnd - firstLightInWord)));
                }
                remainingLightBits = tileLightMask;
                lightMaskWord++;
            }
            if (remainingLightBits == 0u)
            {
                break;
            }
            lightIndex = (lightBuffer.Load<uint>(8) + firstLightInWord) + uint(int(firstbitlow(remainingLightBits)));
            lightIsSelected = (tileLightMask & (remainingLightBits & (0u - remainingLightBits))) != 0u;
            remainingLightBits &= (remainingLightBits - 1u);
        }
        GpuLight light = LoadLight(lightBuffer, lightIndex);
        if (!lightIsSelected)
        {
            continue;
        }
        if (countLights)
        {
            float3 lightRadiance = light.colorAndIntensity.xyz * light.colorAndIntensity.w;
            if (any(bool3(lightRadiance.x > 0.0f.xxx.x, lightRadiance.y > 0.0f.xxx.y, lightRadiance.z > 0.0f.xxx.z)))
            {
                affectingLights++;
            }
            continue;
        }
        uint lightType = uint(light.positionAndType.w);
        float attenuation = 1.0f;
        if (lightType == 0u)
        {
            toLight = normalize(-light.directionAndRange.xyz);
        }
        else
        {
            float3 surfaceToLightOffset = light.positionAndType.xyz - surfacePosition;
            float distanceToLight = length(surfaceToLightOffset);
            if (distanceToLight > 0.0f)
            {
                toLight = surfaceToLightOffset / distanceToLight.xxx;
            }
            else
            {
                toLight = float3(0.0f, 1.0f, 0.0f);
            }
            float lightRange = max(light.directionAndRange.w, 0.0001f);
            float rangeFactor = clamp(1.0f - (distanceToLight / lightRange), 0.0f, 1.0f);
            attenuation = (rangeFactor * rangeFactor) / max(distanceToLight * distanceToLight, 0.0001f);
            if (lightType == 2u)
            {
                float coneCosine = dot(normalize(light.directionAndRange.xyz), -toLight);
                attenuation *= smoothstep(light.spotAngles.y, light.spotAngles.x, coneCosine);
            }
        }
        if (attenuation <= 0.0f)
        {
            continue;
        }
        float normalDotLight = max(dot(normal, toLight), 0.0f);
        if (normalDotLight <= 0.0f)
        {
            continue;
        }
        float3 halfVector = normalize(viewDirection + toLight);
        float normalDotHalf = max(dot(normal, halfVector), 0.0f);
        float viewDotHalf = max(dot(viewDirection, halfVector), 0.0f);
        float distribution = DistributionGGX(normalDotHalf, alphaSquared);
        float geometry = viewGeometry * GeometrySchlickGGX(normalDotLight, geometryFactor);
        float3 fresnel = FresnelSchlick(viewDotHalf, reflectanceAtNormal);
        float3 specularReflection = (fresnel * (distribution * geometry)) / max((4.0f * normalDotView) * normalDotLight, 0.0001f).xxx;
        if (specularGlossiness)
        {
            diffuseEnergyWeight = 1.0f.xxx;
        }
        else
        {
            diffuseEnergyWeight = 1.0f.xxx - fresnel;
        }
        float3 diffuse = diffuseEnergyWeight * diffuseBase;
        float3 radiance = (light.colorAndIntensity.xyz * light.colorAndIntensity.w) * attenuation;
        float shadowVisibility = (lightType == 1u) ? 1.0f : 1.0f;
        float3 contribution = (((diffuse + specularReflection) * radiance) * normalDotLight) * shadowVisibility;
        if (showShadowTiers)
        {
            float impact = dot(contribution, float3(0.2126f, 0.7152f, 0.0722f));
            if (impact > dominantLightImpact)
            {
                dominantLightImpact = impact;
                bool hasPointShadow = (lightType == 1u) && (light.shadowInfo.x != 0u);
                if (hasPointShadow)
                {
                    lightShadowTier = light.shadowInfo.y;
                }
                else
                {
                    lightShadowTier = 3u;
                }
                dominantShadowTier = lightShadowTier;
            }
        }
        lighting += contribution;
        continue;
    }
    if (countLights)
    {
        if (affectingLights == 0u)
        {
            return 0.0f.xxx;
        }
        if (affectingLights <= 4u)
        {
            return float3(0.0f, 0.0f, 1.0f);
        }
        if (affectingLights <= 8u)
        {
            return float3(0.0f, 1.0f, 1.0f);
        }
        if (affectingLights <= 16u)
        {
            return float3(0.0f, 1.0f, 0.0f);
        }
        if (affectingLights <= 32u)
        {
            return float3(1.0f, 1.0f, 0.0f);
        }
        if (affectingLights <= 64u)
        {
            return float3(1.0f, 0.0f, 0.0f);
        }
        return float3(1.0f, 0.0f, 1.0f);
    }
    if (showShadowTiers)
    {
        if (dominantShadowTier == 0u)
        {
            return float3(1.0f, 0.2f, 0.15f);
        }
        if (dominantShadowTier == 1u)
        {
            return float3(1.0f, 0.8f, 0.1f);
        }
        if (dominantShadowTier == 2u)
        {
            return float3(0.15f, 0.45f, 1.0f);
        }
        if (dominantShadowTier == 3u)
        {
            return float3(0.65f, 0.25f, 1.0f);
        }
        return 0.0f.xxx;
    }
    return lighting;
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
    float3 viewDirection = normalize(cameraPosition.xyz - worldPosition);
    float3 lighting = EvaluateDirectLighting(
        worldPosition,
        normal,
        viewDirection,
        baseColor.xyz,
        metallic,
        (clamp(filteredRoughness, 0.045f, 1.0f)),
        ambientOcclusion,
        reflectanceAtNormal,
        specularGlossiness,
        transmission,
        false,
        false);
    if (phase == 1u)
    {
        float3 fresnel = FresnelSchlick((max(dot(normal, viewDirection), 0.0f)), reflectanceAtNormal);
        outputColor = float4(clamp(((baseColor.xyz * (1.0f - metallic)) * transmission) * (1.0f.xxx - fresnel), 0.0f.xxx, 1.0f.xxx), 1.0f);
    }
    else
    {
        outputColor = float4(lighting + (material.emissiveFactor.xyz * sampledEmissive), baseColor.w);
    }
}

float3 MeshletColor(uint meshletHash)
{
    meshletHash ^= (meshletHash >> uint(16));
    meshletHash *= 2146121005u;
    meshletHash ^= (meshletHash >> uint(15));
    meshletHash *= 2221713035u;
    meshletHash ^= (meshletHash >> uint(16));
    return 0.25f.xxx + ((float3(float(meshletHash & 255u), float((meshletHash >> uint(8)) & 255u), float((meshletHash >> uint(16)) & 255u)) * 0.75f) / 255.0f.xxx);
}

void RunFragment()
{
    ShadeMaterial(drawMeshletInfo.y, drawMeshletInfo.w);
    if (false)
    {
        float3 color;
        if (false)
        {
            float3 hashedMeshletColor = MeshletColor(meshletId);
            color = hashedMeshletColor;
        }
        else
        {
            color = clamp((meshletCenter + 100.0f.xxx) / 200.0f.xxx, 0.0f.xxx, 1.0f.xxx);
        }
        outputColor = float4((drawMeshletInfo.w == 1u) ? 0.0f.xxx : color, 1.0f);
    }
}

StageOutput main(StageInput stageInput)
{
    fragmentPosition = stageInput.fragmentPosition;
    fragmentPosition.w = 1.0 / fragmentPosition.w;
    texCoord = stageInput.texCoord;
    worldNormal = stageInput.worldNormal;
    worldTangent = stageInput.worldTangent;
    worldPosition = stageInput.worldPosition;
    vertexColor = stageInput.vertexColor;
    meshletId = stageInput.meshletId;
    meshletCenter = stageInput.meshletCenter;
    RunFragment();
    StageOutput stageOutput;
    stageOutput.outputColor = outputColor;
    return stageOutput;
}

#elif defined(IRYVEN_STAGE_MESH)
// Explicit buffer offsets match the CPU resource layouts.
#include "shader_compat.hlsl"

struct TaskPayload
{
    uint meshletIndex;
};

struct Meshlet
{
    uint vertexOffset;
    uint triangleOffset;
    uint vertexCount;
    uint triangleCount;
    float3 center;
    float radius;
    float3 coneApex;
    float3 coneAxis;
    float coneCutoff;
};

IRYVEN_BINDING(1, 2)
ByteAddressBuffer meshletBuffer : register(t1, space2);
IRYVEN_BINDING(2, 2)
ByteAddressBuffer meshletVertexIndices : register(t2, space2);
IRYVEN_BINDING(0, 2)
ByteAddressBuffer vertexBuffer : register(t0, space2);
#include "frame_uniform.hlsl"

IRYVEN_BINDING(3, 2)
ByteAddressBuffer meshletTriangleIndices : register(t3, space2);
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

static uint groupThreadIndex;
struct StageInput
{
    uint groupThreadIndex : SV_GroupIndex;
};

struct MeshVertex
{
    IRYVEN_LOCATION(0) float2 outTexCoord : TEXCOORD0;
    IRYVEN_LOCATION(1) float4 outColor : TEXCOORD1;
    IRYVEN_LOCATION(2) float3 outNormal : TEXCOORD2;
    IRYVEN_LOCATION(3) float4 outTangent : TEXCOORD3;
    IRYVEN_LOCATION(5) float3 outWorldPos : TEXCOORD5;
    float4 clipPosition : SV_Position;
};

struct MeshPrimitive
{
    IRYVEN_LOCATION(4) nointerpolation uint outMeshletId : TEXCOORD4;
    IRYVEN_LOCATION(6) nointerpolation float3 outMeshletCenter : TEXCOORD6;
};

// Returns the determinant of a 2x2 matrix.
float Determinant2x2(float topLeft, float topRight, float bottomLeft, float bottomRight)
{
    return topLeft * bottomRight - bottomLeft * topRight;
}

// Invert the matrix using its adjugate and determinant.
float3x3 InverseMatrix3x3(float3x3 matrix)
{
    float3x3 adjugateMatrix;	// The adjoint matrix (inverse after dividing by determinant)

    // Create the transpose of the cofactors, as the classical adjoint of the matrix.
    adjugateMatrix[0][0] =  Determinant2x2(matrix[1][1], matrix[1][2], matrix[2][1], matrix[2][2]);
    adjugateMatrix[0][1] = -Determinant2x2(matrix[0][1], matrix[0][2], matrix[2][1], matrix[2][2]);
    adjugateMatrix[0][2] =  Determinant2x2(matrix[0][1], matrix[0][2], matrix[1][1], matrix[1][2]);

    adjugateMatrix[1][0] = -Determinant2x2(matrix[1][0], matrix[1][2], matrix[2][0], matrix[2][2]);
    adjugateMatrix[1][1] =  Determinant2x2(matrix[0][0], matrix[0][2], matrix[2][0], matrix[2][2]);
    adjugateMatrix[1][2] = -Determinant2x2(matrix[0][0], matrix[0][2], matrix[1][0], matrix[1][2]);

    adjugateMatrix[2][0] =  Determinant2x2(matrix[1][0], matrix[1][1], matrix[2][0], matrix[2][1]);
    adjugateMatrix[2][1] = -Determinant2x2(matrix[0][0], matrix[0][1], matrix[2][0], matrix[2][1]);
    adjugateMatrix[2][2] =  Determinant2x2(matrix[0][0], matrix[0][1], matrix[1][0], matrix[1][1]);

    // Calculate the determinant from the cofactors of the first row.
    float matrixDeterminant = (adjugateMatrix[0][0] * matrix[0][0]) + (adjugateMatrix[0][1] * matrix[1][0]) + (adjugateMatrix[0][2] * matrix[2][0]);

    // Divide the classical adjoint matrix by the determinant.
    // If determinant is zero, matrix is not invertable, so leave it unchanged.
    return (matrixDeterminant != 0.0f) ? (adjugateMatrix * (1.0f / matrixDeterminant)) : matrix;
}

[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void main(
    StageInput stageInput,
    in payload TaskPayload meshletPayload,
    out vertices MeshVertex outputVertices[64],
    out indices uint3 outputTriangles[128],
    out primitives MeshPrimitive outputPrimitives[128])
{
    groupThreadIndex = stageInput.groupThreadIndex;

    uint meshletId = drawMeshletInfo.x + meshletPayload.meshletIndex;
    Meshlet meshlet;
    meshlet.vertexOffset = meshletBuffer.Load<uint>(meshletId * 64 + 0);
    meshlet.triangleOffset = meshletBuffer.Load<uint>(meshletId * 64 + 4);
    meshlet.vertexCount = meshletBuffer.Load<uint>(meshletId * 64 + 8);
    meshlet.triangleCount = meshletBuffer.Load<uint>(meshletId * 64 + 12);
    meshlet.center = meshletBuffer.Load<float3>(meshletId * 64 + 16);
    meshlet.radius = meshletBuffer.Load<float>(meshletId * 64 + 28);
    meshlet.coneApex = meshletBuffer.Load<float3>(meshletId * 64 + 32);
    meshlet.coneAxis = meshletBuffer.Load<float3>(meshletId * 64 + 48);
    meshlet.coneCutoff = meshletBuffer.Load<float>(meshletId * 64 + 60);
    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);
    float3x3 modelBasis = float3x3(drawModel[0].xyz, drawModel[1].xyz, drawModel[2].xyz);
    float3x3 normalMatrix = transpose(InverseMatrix3x3(modelBasis));
    float orientation = (determinant(modelBasis) < 0.0f) ? (-1.0f) : 1.0f;
    for (uint vertex = groupThreadIndex; vertex < meshlet.vertexCount; vertex += 32u)
    {
        uint sourceVertex = meshletVertexIndices.Load<uint>((meshlet.vertexOffset + vertex) * 4 + 0);
        uint vertexElementOffset = sourceVertex * 16u;
        float3 position = float3(vertexBuffer.Load<float>((vertexElementOffset + 0u) * 4 + 0), vertexBuffer.Load<float>((vertexElementOffset + 1u) * 4 + 0), vertexBuffer.Load<float>((vertexElementOffset + 2u) * 4 + 0));
        float3 normal = float3(vertexBuffer.Load<float>((vertexElementOffset + 3u) * 4 + 0), vertexBuffer.Load<float>((vertexElementOffset + 4u) * 4 + 0), vertexBuffer.Load<float>((vertexElementOffset + 5u) * 4 + 0));
        float2 texCoord = float2(vertexBuffer.Load<float>((vertexElementOffset + 6u) * 4 + 0), vertexBuffer.Load<float>((vertexElementOffset + 7u) * 4 + 0));
        float4 tangent = float4(vertexBuffer.Load<float>((vertexElementOffset + 8u) * 4 + 0), vertexBuffer.Load<float>((vertexElementOffset + 9u) * 4 + 0), vertexBuffer.Load<float>((vertexElementOffset + 10u) * 4 + 0), vertexBuffer.Load<float>((vertexElementOffset + 11u) * 4 + 0));
        float4 color = float4(vertexBuffer.Load<float>((vertexElementOffset + 12u) * 4 + 0), vertexBuffer.Load<float>((vertexElementOffset + 13u) * 4 + 0), vertexBuffer.Load<float>((vertexElementOffset + 14u) * 4 + 0), vertexBuffer.Load<float>((vertexElementOffset + 15u) * 4 + 0));
        float4 worldPosition = mul(float4(position, 1.0f), drawModel);
        outputVertices[vertex].clipPosition = mul(worldPosition, frameViewProjection);
        outputVertices[vertex].outTexCoord = texCoord;
        outputVertices[vertex].outColor = color;
        outputVertices[vertex].outNormal = mul(normal, normalMatrix);
        outputVertices[vertex].outTangent = float4(mul(tangent.xyz, modelBasis), tangent.w * orientation);
        outputVertices[vertex].outWorldPos = worldPosition.xyz;
    }
    for (uint triangleIndex = groupThreadIndex; triangleIndex < meshlet.triangleCount; triangleIndex += 32u)
    {
        uint sourceOffset = meshlet.triangleOffset + (triangleIndex * 3u);
        outputTriangles[triangleIndex] = uint3(meshletTriangleIndices.Load<uint>(sourceOffset * 4 + 0), meshletTriangleIndices.Load<uint>((sourceOffset + 1u) * 4 + 0), meshletTriangleIndices.Load<uint>((sourceOffset + 2u) * 4 + 0));
        outputPrimitives[triangleIndex].outMeshletId = meshletId;
        outputPrimitives[triangleIndex].outMeshletCenter = mul(float4(meshlet.center, 1.0f), drawModel).xyz;
    }

}

#elif defined(IRYVEN_STAGE_TASK)
// Explicit buffer offsets match the CPU resource layouts.
#include "shader_compat.hlsl"

struct Meshlet
{
    uint vertexOffset;
    uint triangleOffset;
    uint vertexCount;
    uint triangleCount;
    float3 center;
    float radius;
    float3 coneApex;
    float3 coneAxis;
    float coneCutoff;
};

struct TaskPayload
{
    uint meshletIndex;
};

#include "frame_uniform.hlsl"

IRYVEN_BINDING(1, 2)
ByteAddressBuffer meshletBuffer : register(t1, space2);
IRYVEN_BINDING(1, 3)
cbuffer HiZHistory : register(b1, space3)
{
    row_major float4x4 hiZViewProjection : packoffset(c0);
    uint4 hiZInfo : packoffset(c4);
};

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

IRYVEN_COMBINED IRYVEN_BINDING(0, 3)
Texture2D<float4> hiZDepth : register(t0, space3);
IRYVEN_COMBINED IRYVEN_BINDING(0, 3)
SamplerState hiZDepthSampler : register(s0, space3);

static uint3 groupId;
struct StageInput
{
    uint3 groupId : SV_GroupID;
};

groupshared TaskPayload meshletPayload;

bool IsMeshletVisible(Meshlet meshlet)
{
    float4x4 cullingMatrixRows = transpose(mul(drawModel, cullingViewProjection));
    // Left, right, bottom, top, near, far clip planes.
    float4 planes[6] = {
        cullingMatrixRows[3] + cullingMatrixRows[0],
        cullingMatrixRows[3] - cullingMatrixRows[0],
        cullingMatrixRows[3] + cullingMatrixRows[1],
        cullingMatrixRows[3] - cullingMatrixRows[1],
        cullingMatrixRows[2],
        cullingMatrixRows[3] - cullingMatrixRows[2]
    };
    for (int planeIndex = 0; planeIndex < 6; planeIndex++)
    {
        float4 plane = planes[planeIndex];
        float distanceToPlane = dot(float4(meshlet.center, 1.0f), plane);
        if (distanceToPlane < ((-meshlet.radius) * length(plane.xyz)))
        {
            return false;
        }
    }
    return true;
}

void RunTask()
{
    uint meshletIndex = groupId.x;
    meshletPayload.meshletIndex = meshletIndex;
    uint meshletId = drawMeshletInfo.x + meshletPayload.meshletIndex;
    Meshlet meshlet;
    meshlet.vertexOffset = meshletBuffer.Load<uint>(meshletId * 64 + 0);
    meshlet.triangleOffset = meshletBuffer.Load<uint>(meshletId * 64 + 4);
    meshlet.vertexCount = meshletBuffer.Load<uint>(meshletId * 64 + 8);
    meshlet.triangleCount = meshletBuffer.Load<uint>(meshletId * 64 + 12);
    meshlet.center = meshletBuffer.Load<float3>(meshletId * 64 + 16);
    meshlet.radius = meshletBuffer.Load<float>(meshletId * 64 + 28);
    meshlet.coneApex = meshletBuffer.Load<float3>(meshletId * 64 + 32);
    meshlet.coneAxis = meshletBuffer.Load<float3>(meshletId * 64 + 48);
    meshlet.coneCutoff = meshletBuffer.Load<float>(meshletId * 64 + 60);
    bool visible = IsMeshletVisible(meshlet);
    meshletPayload.meshletIndex = meshletIndex;
    DispatchMesh(uint(visible), 1u, 1u, meshletPayload);
}

[numthreads(1, 1, 1)]
void main(StageInput stageInput)
{
    groupId = stageInput.groupId;
    RunTask();
}

#else
#error Select a supported IRYVEN_STAGE_* macro for this shader
#endif
