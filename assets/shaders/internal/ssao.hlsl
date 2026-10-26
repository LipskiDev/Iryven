// IRYVEN_SHADER_STAGES: COMPUTE
#include "shader_compat.hlsl"

Texture2D<float> sceneDepth : register(t0, space0);
Texture2D<float4> sceneNormalRoughness : register(t1, space0);
Texture2D<float4> noiseTexture : register(t2, space0);
struct SampleKernel
{
  float4 samples[64];
};
ConstantBuffer<SampleKernel> sampleKernel : register(b4, space0);
RWTexture2D<float4> ssao : register(u3, space0);

struct SSAOConstants
{
  float4x4 viewProjection;
  float4x4 inverseViewProjection;
  uint sampleCount;
  float radius;
  float bias;
  float padding;
};

ConstantBuffer<SSAOConstants> ssaoConstants : register(b5, space0);

#ifdef __spirv__ // Vulkan
static const float projectionY = -1.0f;
#else
static const float projectionY = 1.0f;
#endif


float3 ReconstructWorldPosition(float2 uv, float depth)
{
  float2 ndc = IryvenUvToNdc(uv, projectionY);
  float4 world = mul(ssaoConstants.inverseViewProjection, float4(ndc, depth, 1.0f));
  return world.xyz / world.w;
}

float3x3 ConstructTangentBasis(float3 normal, float3 noise)
{
  float3 tangent = noise - normal * dot(noise, normal);
  if (dot(tangent, tangent) < 1e-6f)
  {
    float3 axis = abs(normal.z) < 0.999f ? float3(0, 0, 1) : float3(0, 1, 0);
    tangent = cross(axis, normal);
  }
  tangent = normalize(tangent);
  return float3x3(tangent, cross(normal, tangent), normal);
}

float3 RotateSample(float3 samplePoint, float3x3 tangentBasis)
{
  return mul(samplePoint, tangentBasis);
}

bool SampleDepth(float3 samplePosition, uint2 depthSize,
                 out float candidateDepth, out float surfaceDepth)
{
  candidateDepth = surfaceDepth = 0.0f;
  float4 clip = mul(ssaoConstants.viewProjection, float4(samplePosition, 1.0f));
  if (!all(isfinite(clip)) || clip.w <= 0.0f)
    return false;
  float3 ndc = clip.xyz / clip.w;
  float2 uv = IryvenNdcToUv(ndc.xy, projectionY);
  if (any(uv < 0.0f) || any(uv >= 1.0f) || ndc.z < 0.0f || ndc.z > 1.0f)
    return false;

  uint2 pixel = uint2(uv * float2(depthSize));
  float depth = sceneDepth.Load(int3(pixel, 0));
  if (depth >= 1.0f)
    return false;

  float2 surfaceUv = (float2(pixel) + 0.5f) / float2(depthSize);
  float3 surfacePosition = ReconstructWorldPosition(surfaceUv, depth);
  float4 surfaceClip = mul(ssaoConstants.viewProjection, float4(surfacePosition, 1.0f));

  candidateDepth = clip.w;
  surfaceDepth = surfaceClip.w;
  return all(isfinite(surfacePosition)) && isfinite(surfaceDepth) && surfaceDepth > 0.0f;
}

[numthreads(16, 16, 1)] void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
  uint2 texCoord = dispatchThreadId.xy;
  uint2 outputSize;
  ssao.GetDimensions(outputSize.x, outputSize.y);
  if (any(texCoord >= outputSize))
    return;

  ssao[texCoord] = float4(1, 1, 1, 1);
  uint2 depthSize;
  sceneDepth.GetDimensions(depthSize.x, depthSize.y);
  float2 uv = (float2(texCoord) + 0.5f) / float2(outputSize);
  uint2 pixel = min(uint2(uv * float2(depthSize)), depthSize - 1);
  float depth = sceneDepth.Load(int3(pixel, 0));
  float3 normal = sceneNormalRoughness.Load(int3(pixel, 0)).xyz;
  if (depth >= 1.0f || !all(isfinite(normal)) || dot(normal, normal) < 1e-6f)
    return;
  normal = normalize(normal);
  float2 surfaceUv = (float2(pixel) + 0.5f) / float2(depthSize);
  float3 worldPosition = ReconstructWorldPosition(surfaceUv, depth);
  if (!all(isfinite(worldPosition)))
    return;
  float centerDepth = mul(ssaoConstants.viewProjection, float4(worldPosition, 1.0f)).w;

  uint2 noiseSize;
  noiseTexture.GetDimensions(noiseSize.x, noiseSize.y);

  float3 noise = noiseTexture.Load(int3(texCoord % noiseSize, 0)).xyz;

  float3x3 tangentBasis = ConstructTangentBasis(normal, noise);

  uint sampleCount = clamp(ssaoConstants.sampleCount, 1u, 64u);
  float radius = max(ssaoConstants.radius, 0.01f);
  float bias = max(ssaoConstants.bias, 0.0f);
  float occlusion = 0.0f;
  for (uint i = 0; i < sampleCount; ++i)
  {
    float fraction = float(i + 1) / float(sampleCount);
    float scale = lerp(0.1f, 1.0f, fraction * fraction);
    float3 direction = RotateSample(sampleKernel.samples[i].xyz, tangentBasis);
    float3 samplePosition = worldPosition + direction * (radius * scale);
    float candidateDepth, surfaceDepth;

    if (!SampleDepth(samplePosition, depthSize, candidateDepth, surfaceDepth))
      continue;

    float rangeWeight = smoothstep(0.0f, 1.0f,
        radius / max(abs(centerDepth - surfaceDepth), 1e-5f));
    occlusion += (surfaceDepth <= candidateDepth - bias ? 1.0f : 0.0f) * rangeWeight;
  }

  float ao = saturate(1.0f - occlusion / float(sampleCount));
  ssao[texCoord] = float4(ao, ao, ao, 1.0f);
}
