// IRYVEN_SHADER_STAGES: TASK
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

ByteAddressBuffer meshletBuffer : register(t1, space2);
cbuffer HiZHistory : register(b1, space3)
{
  row_major float4x4 hiZViewProjection : packoffset(c0);
  uint4 hiZInfo : packoffset(c4);
};

struct DrawConstants
{
  row_major float4x4 drawModel;
  uint4 drawMeshletInfo;
};

[[vk::push_constant]]
ConstantBuffer<DrawConstants> drawConstants : register(b0, space2147483647);

[[vk::combinedImageSampler]] Texture2D<float4> hiZDepth : register(t0, space3);
[[vk::combinedImageSampler]] SamplerState hiZDepthSampler : register(s0, space3);

static uint3 groupId;
struct StageInput
{
  uint3 groupId : SV_GroupID;
};

groupshared TaskPayload meshletPayload;

bool IsMeshletVisible(Meshlet meshlet)
{
  float4x4 cullingMatrixRows = transpose(mul(drawConstants.drawModel, cullingViewProjection));
  // Left, right, bottom, top, near, far clip planes.
  float4 planes[6] = {cullingMatrixRows[3] + cullingMatrixRows[0],
                      cullingMatrixRows[3] - cullingMatrixRows[0],
                      cullingMatrixRows[3] + cullingMatrixRows[1],
                      cullingMatrixRows[3] - cullingMatrixRows[1],
                      cullingMatrixRows[2],
                      cullingMatrixRows[3] - cullingMatrixRows[2]};
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
  uint meshletId = drawConstants.drawMeshletInfo.x + meshletPayload.meshletIndex;
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

[numthreads(1, 1, 1)] void main(StageInput stageInput)
{
  groupId = stageInput.groupId;
  RunTask();
}
