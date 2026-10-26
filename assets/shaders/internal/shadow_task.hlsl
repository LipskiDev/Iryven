// IRYVEN_SHADER_STAGES: TASK
#include "shadow_common.hlsl"
// Explicit buffer offsets match the CPU resource layouts.

struct MeshletBounds
{
  float4 sphere;
};

struct ShadowTaskPayload
{
  uint meshletIndices[32];
  uint objectIndices[32];
  uint lightAndFace;
};

ByteAddressBuffer shadowDrawCommands : register(t0, space0);
ByteAddressBuffer shadowMeshletInstances : register(t1, space0);
ByteAddressBuffer meshletBoundingSpheres : register(t2, space0);
ByteAddressBuffer objectTransforms : register(t3, space0);
ByteAddressBuffer shadowLightPositions : register(t4, space0);
struct ShadowTaskConstants
{
  uint drawDataCommandIndex;
  uint drawDataMaxCandidatesPerLight;
};

[[vk::push_constant]]
ConstantBuffer<ShadowTaskConstants> shadowDrawData : register(b0, space2147483647);

static uint3 groupId;
static uint groupThreadIndex;
static uint4 precedingLaneMask;
struct StageInput
{
  uint3 groupId : SV_GroupID;
  uint groupThreadIndex : SV_GroupIndex;
};

groupshared ShadowTaskPayload meshletPayload;

void RunTask()
{
  ShadowDrawCommand command;
  command.groupCountX = shadowDrawCommands.Load<uint>(shadowDrawData.drawDataCommandIndex * 16 + 0);
  command.groupCountY = shadowDrawCommands.Load<uint>(shadowDrawData.drawDataCommandIndex * 16 + 4);
  command.groupCountZ = shadowDrawCommands.Load<uint>(shadowDrawData.drawDataCommandIndex * 16 + 8);
  command.lightAndFace = shadowDrawCommands.Load<uint>(shadowDrawData.drawDataCommandIndex * 16 + 12);
  uint lightIndex = ShadowLightIndex(command.lightAndFace);
  uint faceIndex = ShadowFaceIndex(command.lightAndFace);
  uint candidateIndex = (groupId.x * 32u) + groupThreadIndex;
  uint readOffset = (lightIndex * shadowDrawData.drawDataMaxCandidatesPerLight) + candidateIndex;
  ShadowMeshletInstance candidate;
  candidate.objectIndex = shadowMeshletInstances.Load<uint>(readOffset * 16 + 0);
  candidate.meshletIndex = shadowMeshletInstances.Load<uint>(readOffset * 16 + 4);
  candidate.padding0 = shadowMeshletInstances.Load<uint>(readOffset * 16 + 8);
  candidate.padding1 = shadowMeshletInstances.Load<uint>(readOffset * 16 + 12);
  float4 localSphere = meshletBoundingSpheres.Load<float4>(candidate.meshletIndex * 16 + 0);
  float4x4 model = float4x4(objectTransforms.Load<float4>(candidate.objectIndex * 64 + 0),
                            objectTransforms.Load<float4>(candidate.objectIndex * 64 + 16),
                            objectTransforms.Load<float4>(candidate.objectIndex * 64 + 32),
                            objectTransforms.Load<float4>(candidate.objectIndex * 64 + 48));
  float3 center = mul(float4(localSphere.xyz, 1.0f), model).xyz;
  float scale = max(max(length(model[0].xyz), length(model[1].xyz)), length(model[2].xyz));
  float radius = localSphere.w * scale;
  uint faceMask = ShadowCubeFaceMask((shadowLightPositions.Load<float4>(lightIndex * 16 + 0).xyz),
                                     (center - radius.xxx), (center + radius.xxx));
  bool visible = (faceMask & (1u << faceIndex)) != 0u;
  uint4 ballot = WaveActiveBallot(visible);
  uint outputIndex = countbits(ballot.x & precedingLaneMask.x) + countbits(ballot.y & precedingLaneMask.y) +
                     countbits(ballot.z & precedingLaneMask.z) + countbits(ballot.w & precedingLaneMask.w);
  if (visible)
  {
    meshletPayload.meshletIndices[outputIndex] = candidate.meshletIndex;
    meshletPayload.objectIndices[outputIndex] = candidate.objectIndex;
  }
  if (groupThreadIndex == 0u)
  {
    meshletPayload.lightAndFace = command.lightAndFace;
  }
  DispatchMesh(countbits(ballot.x) + countbits(ballot.y) + countbits(ballot.z) + countbits(ballot.w), 1u, 1u,
               meshletPayload);
}

[numthreads(32, 1, 1)] void main(StageInput stageInput)
{
  groupId = stageInput.groupId;
  groupThreadIndex = stageInput.groupThreadIndex;
  precedingLaneMask = (1u << (WaveGetLaneIndex() - uint4(0, 32, 64, 96))) - 1u;
  if (WaveGetLaneIndex() >= 32)
    precedingLaneMask.x = ~0u;
  if (WaveGetLaneIndex() >= 64)
    precedingLaneMask.y = ~0u;
  if (WaveGetLaneIndex() >= 96)
    precedingLaneMask.z = ~0u;
  if (WaveGetLaneIndex() < 32)
    precedingLaneMask.y = 0u;
  if (WaveGetLaneIndex() < 64)
    precedingLaneMask.z = 0u;
  if (WaveGetLaneIndex() < 96)
    precedingLaneMask.w = 0u;
  RunTask();
}
