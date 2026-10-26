// IRYVEN_SHADER_STAGES: COMPUTE

#include "shader_compat.hlsl"

#include "light_buffer.hlsl"

cbuffer CullingData : register(b1, space0)
{
  row_major float4x4 cullingView : packoffset(c0);
  row_major float4x4 cullingProjection : packoffset(c4);
  uint4 viewportAndTileExtent : packoffset(c8);
};

ByteAddressBuffer lightBuffer : register(t0, space0);
RWByteAddressBuffer shadowResolutionRequests : register(u2, space0);
RWByteAddressBuffer lightTileBuffer : register(u3, space0);

static uint3 groupId;
static uint groupThreadIndex;
struct StageInput
{
  uint3 groupId : SV_GroupID;
  uint groupThreadIndex : SV_GroupIndex;
};

groupshared uint tileMask[16];

bool OutsidePlane(float4 plane, float3 center, float radius)
{
  return dot(plane, float4(center, 1.0f)) < ((-(radius + 0.0001f)) * length(plane.xyz));
}

void RunCompute()
{
  uint2 tile = groupId.xy;
  uint lane = groupThreadIndex;
  if (any(tile >= viewportAndTileExtent.zw))
  {
    return;
  }
  if (lane < 16u)
  {
    tileMask[lane] = 0u;
  }
  GroupMemoryBarrierWithGroupSync();
  float2 firstTileCorner =
      IryvenUvToNdc(float2(tile * uint2(8u, 8u)) / float2(viewportAndTileExtent.xy), cullingProjection[1][1]);
  float2 lastTileCorner =
      IryvenUvToNdc(float2(min((tile + 1u) * 8u, viewportAndTileExtent.xy)) / float2(viewportAndTileExtent.xy),
                    cullingProjection[1][1]);
  float2 tileMinimumNdc = min(firstTileCorner, lastTileCorner);
  float2 tileMaximumNdc = max(firstTileCorner, lastTileCorner);
  float4x4 projectionRows = transpose(cullingProjection);
  float4 leftPlane = projectionRows[0] - (projectionRows[3] * tileMinimumNdc.x);
  float4 rightPlane = (projectionRows[3] * tileMaximumNdc.x) - projectionRows[0];
  float4 topPlane = projectionRows[1] - (projectionRows[3] * tileMinimumNdc.y);
  float4 bottomPlane = (projectionRows[3] * tileMaximumNdc.y) - projectionRows[1];
  for (uint localIndex = lane; localIndex < min(lightBuffer.Load<uint>(12), 512u); localIndex += 64u)
  {
    GpuLight light = LoadLight(lightBuffer, (lightBuffer.Load<uint>(8) + localIndex));
    float3 center = mul(float4(light.positionAndType.xyz, 1.0f), cullingView).xyz;
    float radius = max(light.directionAndRange.w, 0.0001f);
    bool intersectsTile = !OutsidePlane(leftPlane, center, radius) && !OutsidePlane(rightPlane, center, radius) &&
                          !OutsidePlane(topPlane, center, radius) && !OutsidePlane(bottomPlane, center, radius);
    if (intersectsTile)
    {
      uint previousTileMask;
      InterlockedOr(tileMask[localIndex / 32u], 1u << (localIndex % 32u), previousTileMask);
      float4 clip = mul(float4(center, 1.0f), cullingProjection);
      if (abs(clip.w) > 0.0001f)
      {
        float2 lightPixel = IryvenNdcToUv(clip.xy / clip.w, cullingProjection[1][1]) * float2(viewportAndTileExtent.xy);
        uint2 shadowTile = (tile * uint2(8u, 8u)) / uint2(64u, 64u);
        float2 tileCenter = (float2(shadowTile) + 0.5f.xx) * 64.0f;
        float distanceToTileCenter = length(lightPixel - tileCenter);
        float tileRadiusSquared = 2048.0f;
        float difference = (distanceToTileCenter * distanceToTileCenter) - tileRadiusSquared;
        if (difference > 0.0001f)
        {
          float solidAngle = 6.2831855f * (1.0f - (sqrt(difference) / distanceToTileCenter));
          float desiredShadowResolution = sqrt(51471.855f / max(6.0f * solidAngle, 1e-06f));
          uint previousResolutionRequest;
          shadowResolutionRequests.InterlockedMax(localIndex * 4 + 0, uint(ceil(desiredShadowResolution)),
                                                  previousResolutionRequest);
        }
      }
    }
  }
  GroupMemoryBarrierWithGroupSync();
  uint offset = ((tile.y * viewportAndTileExtent.z) + tile.x) * 16u;
  if (lane < 16u)
  {
    lightTileBuffer.Store<uint>((offset + lane) * 4 + 16, tileMask[lane]);
  }
  if (all(tile == uint2(0u, 0u)) && (lane == 0u))
  {
    lightTileBuffer.Store<uint4>(0, uint4(viewportAndTileExtent.zw, 8u, 16u));
  }
}

[numthreads(64, 1, 1)] void main(StageInput stageInput)
{
  groupId = stageInput.groupId;
  groupThreadIndex = stageInput.groupThreadIndex;
  RunCompute();
}
