// IRYVEN_SHADER_STAGES: FRAGMENT VERTEX
// Stage selection is supplied by ShaderCompiler or the build rules.

#if defined(IRYVEN_STAGE_FRAGMENT)
#include "lighting.hlsl"
// Explicit buffer offsets match the CPU resource layouts.
#include "shader_compat.hlsl"

#include "light_buffer.hlsl"
#include "light_depth_bins.hlsl"

IRYVEN_BINDING(4, 0)
ByteAddressBuffer lightDepthBinBuffer : register(t4, space0);
IRYVEN_BINDING(0, 0)
ByteAddressBuffer lightBuffer : register(t0, space0);
IRYVEN_BINDING(3, 0)
ByteAddressBuffer lightTileBuffer : register(t3, space0);
#include "frame_uniform.hlsl"

#ifdef __spirv__
struct ShadingConstantsData
{
    IRYVEN_OFFSET(0) row_major float4x4 shadingInverseViewProjection;
    IRYVEN_OFFSET(64) uint shadingLightCountView;
    IRYVEN_OFFSET(68) uint shadingShadowTierView;
};
IRYVEN_PUSH_CONSTANT ConstantBuffer<ShadingConstantsData> iryvenPush;
#define shadingInverseViewProjection iryvenPush.shadingInverseViewProjection
#define shadingLightCountView iryvenPush.shadingLightCountView
#define shadingShadowTierView iryvenPush.shadingShadowTierView
#else
IRYVEN_PUSH_CONSTANT cbuffer ShadingConstants : register(b0, space2147483647)
{
    row_major float4x4 shadingInverseViewProjection : packoffset(c0);
    uint shadingLightCountView : packoffset(c4);
    uint shadingShadowTierView : packoffset(c4.y);
};
#endif

IRYVEN_COMBINED IRYVEN_BINDING(5, 1)
TextureCubeArray<float4> pointShadowMap : register(t5, space1);
IRYVEN_COMBINED IRYVEN_BINDING(5, 1)
SamplerState pointShadowMapSampler : register(s5, space1);
IRYVEN_COMBINED IRYVEN_BINDING(1, 1)
Texture2D<float4> normalRoughness : register(t1, space1);
IRYVEN_COMBINED IRYVEN_BINDING(1, 1)
SamplerState normalRoughnessSampler : register(s1, space1);
IRYVEN_COMBINED IRYVEN_BINDING(4, 1)
Texture2D<float4> sceneDepth : register(t4, space1);
IRYVEN_COMBINED IRYVEN_BINDING(4, 1)
SamplerState sceneDepthSampler : register(s4, space1);
IRYVEN_COMBINED IRYVEN_BINDING(0, 1)
Texture2D<float4> baseMetallic : register(t0, space1);
IRYVEN_COMBINED IRYVEN_BINDING(0, 1)
SamplerState baseMetallicSampler : register(s0, space1);
IRYVEN_COMBINED IRYVEN_BINDING(2, 1)
Texture2D<float4> emissiveOcclusion : register(t2, space1);
IRYVEN_COMBINED IRYVEN_BINDING(2, 1)
SamplerState emissiveOcclusionSampler : register(s2, space1);
IRYVEN_COMBINED IRYVEN_BINDING(3, 1)
Texture2D<float4> specularBuffer : register(t3, space1);
IRYVEN_COMBINED IRYVEN_BINDING(3, 1)
SamplerState specularSampler : register(s3, space1);

static float4 fragmentPosition;
static float4 outputColor;

struct StageInput
{
    float4 fragmentPosition : SV_Position;
};

struct StageOutput
{
    IRYVEN_LOCATION(0) float4 outputColor : SV_Target0;
};

uint2 GetTextureDimensions(Texture2D<float4> texture, uint mipLevel, out uint mipCount)
{
    uint2 dimensions;
    texture.GetDimensions(mipLevel, dimensions.x, dimensions.y, mipCount);
    return dimensions;
}

uint2 LocalLightDepthRange(float3 surfacePosition, uint localLightCount)
{
    return IryvenLocalLightDepthRange(lightDepthBinBuffer, surfacePosition, localLightCount);
}

float ProjectShadowDepth(float distanceToFace, float nearPlane, float farPlane)
{
    return (farPlane / (farPlane - nearPlane)) - ((farPlane * nearPlane) / ((farPlane - nearPlane) * distanceToFace));
}

float EvaluatePointShadow(GpuLight light, float3 worldPosition, float3 worldNormal)
{
    if (light.shadowInfo.x == 0u)
    {
        return 1.0f;
    }
    float3 lightToFragment = worldPosition - light.positionAndType.xyz;
    float3 fragmentToLight = -lightToFragment;
    float distanceToLight = length(lightToFragment);
    if (distanceToLight <= 0.0001f)
    {
        return 1.0f;
    }
    float3 surfaceToLight = fragmentToLight / distanceToLight.xxx;
    float normalDotLight = max(dot(worldNormal, surfaceToLight), 0.0f);
    if (normalDotLight <= 0.0f)
    {
        return 1.0f;
    }
    float distanceToFace = max(abs(lightToFragment.x), max(abs(lightToFragment.y), abs(lightToFragment.z)));
    float farPlane = max(light.directionAndRange.w, 0.1001f);
    float currentDepth = ProjectShadowDepth(distanceToFace, 0.1f, farPlane);
    float4 shadowCoordinate = float4(lightToFragment, float(light.shadowInfo.z));
    float storedDepth = pointShadowMap.SampleLevel(pointShadowMapSampler, shadowCoordinate, float(light.shadowInfo.y)).x;
    float bias = max(0.005f * (1.0f - normalDotLight), 0.0005f);
    return float((currentDepth - bias) < storedDepth);
}

float3 EvaluateDirectLighting(
    float3 worldPosition,
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
        localLightRange = LocalLightDepthRange(worldPosition, (lightBuffer.Load<uint>(12)));
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
    float shadowVisibility;
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
            float3 surfaceToLightOffset = light.positionAndType.xyz - worldPosition;
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
        if (lightType == 1u)
        {
            shadowVisibility = EvaluatePointShadow(light, worldPosition, normal);
        }
        else
        {
            shadowVisibility = 1.0f;
        }
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

void RunFragment()
{
    int2 pixel = int2(fragmentPosition.xy);
    float4 normalRoughnessSample = normalRoughness.Load(int3(pixel, 0));
    if (dot(normalRoughnessSample.xyz, normalRoughnessSample.xyz) < 0.25f)
    {
        discard;
    }
    float depth = sceneDepth.Load(int3(pixel, 0)).x;
    uint textureMipCount;
    float2 uv = fragmentPosition.xy / float2(int2(GetTextureDimensions(sceneDepth, uint(0), textureMipCount)));
    float4 position = mul(float4(IryvenUvToNdc(uv), depth, 1.0f), shadingInverseViewProjection);
    float3 worldPosition = position.xyz / position.w.xxx;
    float4 baseColorMetallicSample = baseMetallic.Load(int3(pixel, 0));
    float4 emissiveOcclusionSample = emissiveOcclusion.Load(int3(pixel, 0));
    float4 reflectanceAtNormal = specularBuffer.Load(int3(pixel, 0));
    float3 lighting = EvaluateDirectLighting(
        worldPosition,
        (normalize(normalRoughnessSample.xyz)),
        (normalize(cameraPosition.xyz - worldPosition)),
        baseColorMetallicSample.xyz,
        baseColorMetallicSample.w,
        normalRoughnessSample.w,
        emissiveOcclusionSample.w,
        reflectanceAtNormal.xyz,
        (reflectanceAtNormal.w > 0.5f),
        0.0f,
        (shadingLightCountView != 0u),
        (shadingShadowTierView != 0u));
    bool showLightingDebug = (shadingLightCountView != 0u) || (shadingShadowTierView != 0u);
    float3 emissiveContribution;
    if (showLightingDebug)
    {
        emissiveContribution = 0.0f.xxx;
    }
    else
    {
        emissiveContribution = emissiveOcclusionSample.xyz;
    }
    outputColor = float4(lighting + emissiveContribution, 1.0f);
}

StageOutput main(StageInput stageInput)
{
    fragmentPosition = stageInput.fragmentPosition;
    fragmentPosition.w = 1.0 / fragmentPosition.w;
    RunFragment();
    StageOutput stageOutput;
    stageOutput.outputColor = outputColor;
    return stageOutput;
}

#elif defined(IRYVEN_STAGE_VERTEX)
// Explicit buffer offsets match the CPU resource layouts.
#include "shader_compat.hlsl"

static float4 clipPosition;
static int vertexIndex;
struct StageInput
{
    uint vertexIndex : SV_VertexID;
};

struct StageOutput
{
    float4 clipPosition : SV_Position;
};

void RunVertex()
{
    float2 fullscreenTriangleCoordinate = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    clipPosition = float4((fullscreenTriangleCoordinate * 2.0f) - 1.0f.xx, 0.0f, 1.0f);
}

StageOutput main(StageInput stageInput)
{
    vertexIndex = int(stageInput.vertexIndex);
    RunVertex();
    StageOutput stageOutput;
    stageOutput.clipPosition = clipPosition;
    return stageOutput;
}

#else
#error Select a supported IRYVEN_STAGE_* macro for this shader
#endif
