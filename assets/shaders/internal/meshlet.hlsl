// IRYVEN_SHADER_STAGES: FRAGMENT
#include "lighting.hlsl"

#include "gltf_material.hlsl"

#include "light_buffer.hlsl"
#include "light_depth_bins.hlsl"

ByteAddressBuffer lightDepthBinBuffer : register(t4, space0);
ByteAddressBuffer lightBuffer : register(t0, space0);
ByteAddressBuffer lightTileBuffer : register(t3, space0);
ByteAddressBuffer materialBuffer : register(t2, space0);
#include "frame_uniform.hlsl"

struct DrawConstants
{
  row_major float4x4 drawModel;
  uint4 drawMeshletInfo;
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> drawConstants : register(b0, space2147483647);

Texture2D<float4> bindlessTextures[] : register(t0, space1);
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
  float2 texCoord : TEXCOORD0;
  float4 vertexColor : TEXCOORD1;
  float3 worldNormal : TEXCOORD2;
  float4 worldTangent : TEXCOORD3;
  uint meshletId : TEXCOORD4;
  float3 worldPosition : TEXCOORD5;
  float3 meshletCenter : TEXCOORD6;
  float4 fragmentPosition : SV_Position;
};

struct StageOutput
{
  float4 outputColor : SV_Target0;
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

float3 EvaluateDirectLighting(float3 surfacePosition, float3 normal, float3 viewDirection, float3 baseColor,
                              float metallic, float roughness, float ambientOcclusion, float3 reflectanceAtNormal,
                              bool specularGlossiness, float transmission, bool countLights, bool showShadowTiers)
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
          tileLightMask = (lightTileBuffer.Load<uint>((tileMaskOffset + lightMaskWord) * 4 + 16) &
                           (4294967295u << (firstLightIndex - firstLightInWord))) &
                          (4294967295u >> (32u - (rangeEnd - firstLightInWord)));
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
    float3 specularReflection =
        (fresnel * (distribution * geometry)) / max((4.0f * normalDotView) * normalDotLight, 0.0001f).xxx;
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
    ambientOcclusion = lerp(1.0f, SampleBindlessTexture(material.occlusionTextureIndex).x,
                            clamp(material.occlusionStrength, 0.0f, 1.0f));
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
  float3 lighting = EvaluateDirectLighting(worldPosition, normal, viewDirection, baseColor.xyz, metallic,
                                           (clamp(filteredRoughness, 0.045f, 1.0f)), ambientOcclusion,
                                           reflectanceAtNormal, specularGlossiness, transmission, false, false);
  if (phase == 1u)
  {
    float3 fresnel = FresnelSchlick((max(dot(normal, viewDirection), 0.0f)), reflectanceAtNormal);
    outputColor = float4(
        clamp(((baseColor.xyz * (1.0f - metallic)) * transmission) * (1.0f.xxx - fresnel), 0.0f.xxx, 1.0f.xxx), 1.0f);
  }
  else
  {
    outputColor = float4(lighting + (material.emissiveFactor.xyz * sampledEmissive), baseColor.w);
  }
}

#include "meshlet_debug.hlsl"

void RunFragment()
{
  ShadeMaterial(drawConstants.drawMeshletInfo.y, drawConstants.drawMeshletInfo.w);
  if (drawConstants.drawMeshletInfo.z != 0u)
  {
    outputColor = float4(drawConstants.drawMeshletInfo.w == 1u ? 0.0f.xxx : MeshletColor(meshletId), 1.0f);
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
