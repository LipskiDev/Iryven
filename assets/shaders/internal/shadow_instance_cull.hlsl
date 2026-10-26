// IRYVEN_SHADER_STAGES: COMPUTE
// Stage selection is supplied by ShaderCompiler or the build rules.

// Explicit buffer offsets match the CPU resource layouts.

struct ShadowLight
{
  float4 positionAndRange;
  uint4 localIndexAndShadowState;  // x: local light index; y: shadow enabled.
};

struct ShadowObject
{
  float4 centerAndRadius;
  uint4 meshletCountAndOffsets;  // x: meshlet count; remaining components reserved.
};

cbuffer ShadowCullData : register(b0, space0)
{
  uint4 shadowCullCounts : packoffset(c0);  // Lights, objects, visible tiles, words per tile.
};

ByteAddressBuffer shadowLightBuffer : register(t1, space0);
ByteAddressBuffer lightTileBuffer : register(t3, space0);
ByteAddressBuffer shadowObjectBuffer : register(t2, space0);
RWByteAddressBuffer shadowMeshTaskCommands : register(u4, space0);

static uint3 groupId;
static uint3 groupThreadId;
struct StageInput
{
  uint3 groupId : SV_GroupID;
  uint3 groupThreadId : SV_GroupThreadID;
};

groupshared uint lightTouchesVisibleCluster;

void RunCompute()
{
  uint lightIndex = groupId.x;
  uint lane = groupThreadId.x;
  if (lightIndex >= shadowCullCounts.x)
  {
    return;
  }
  if (shadowLightBuffer.Load<uint>(lightIndex * 32 + 20) == 0u)
  {
    return;
  }
  if (lane == 0u)
  {
    lightTouchesVisibleCluster = 0u;
  }
  GroupMemoryBarrierWithGroupSync();
  uint localLightIndex = shadowLightBuffer.Load<uint>(lightIndex * 32 + 16);
  uint word = localLightIndex >> 5u;
  uint bit = 1u << (localLightIndex & 31u);
  if (word < shadowCullCounts.w)
  {
    for (uint tile = lane; tile < shadowCullCounts.z; tile += 64u)
    {
      if ((lightTileBuffer.Load<uint>(((tile * shadowCullCounts.w) + word) * 4 + 16) & bit) != 0u)
      {
        uint previousVisibilityMask;
        InterlockedOr(lightTouchesVisibleCluster, 1u, previousVisibilityMask);
        break;
      }
    }
  }
  GroupMemoryBarrierWithGroupSync();
  ShadowLight light;
  light.positionAndRange = shadowLightBuffer.Load<float4>(lightIndex * 32 + 0);
  light.localIndexAndShadowState = shadowLightBuffer.Load<uint4>(lightIndex * 32 + 16);
  uint meshletCount;
  for (uint objectIndex = lane; objectIndex < shadowCullCounts.y; objectIndex += 64u)
  {
    ShadowObject object;
    object.centerAndRadius = shadowObjectBuffer.Load<float4>(objectIndex * 32 + 0);
    object.meshletCountAndOffsets = shadowObjectBuffer.Load<uint4>(objectIndex * 32 + 16);
    float3 delta = object.centerAndRadius.xyz - light.positionAndRange.xyz;
    float combinedRadius = object.centerAndRadius.w + light.positionAndRange.w;
    bool affected = (lightTouchesVisibleCluster != 0u) && (dot(delta, delta) <= (combinedRadius * combinedRadius));
    if (affected)
    {
      meshletCount = object.meshletCountAndOffsets.x;
    }
    else
    {
      meshletCount = 0u;
    }
    shadowMeshTaskCommands.Store<uint4>(((lightIndex * shadowCullCounts.y) + objectIndex) * 16 + 0,
                                        uint4(meshletCount, 6u, 1u, 0u));
  }
}

[numthreads(64, 1, 1)] void main(StageInput stageInput)
{
  groupId = stageInput.groupId;
  groupThreadId = stageInput.groupThreadId;
  RunCompute();
}
