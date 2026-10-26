// IRYVEN_SHADER_STAGES: COMPUTE
#include "shadow_common.hlsl"

ByteAddressBuffer visibleMeshletCounts : register(t0, space0);
RWByteAddressBuffer shadowDrawCommands : register(u1, space0);
struct ShadowCommandConstants
{
  uint commandDataLightCount;
};

[[vk::push_constant]]
ConstantBuffer<ShadowCommandConstants> shadowCommandData : register(b0, space2147483647);

static uint3 dispatchThreadId;
struct StageInput
{
  uint3 dispatchThreadId : SV_DispatchThreadID;
};

void RunCompute()
{
  uint lightIndex = dispatchThreadId.x;
  if (lightIndex >= shadowCommandData.commandDataLightCount)
  {
    return;
  }
  uint groupCount = ((visibleMeshletCounts.Load<uint>(lightIndex * 4 + 0) + 32u) - 1u) / 32u;
  for (uint cubeFaceIndex = 0u; cubeFaceIndex < 6u; cubeFaceIndex++)
  {
    uint commandIndex = (lightIndex * 6u) + cubeFaceIndex;
    ShadowDrawCommand command = {groupCount, 1u, 1u, PackShadowLightFace(lightIndex, cubeFaceIndex)};
    shadowDrawCommands.Store<uint>(commandIndex * 16 + 0, command.groupCountX);
    shadowDrawCommands.Store<uint>(commandIndex * 16 + 4, command.groupCountY);
    shadowDrawCommands.Store<uint>(commandIndex * 16 + 8, command.groupCountZ);
    shadowDrawCommands.Store<uint>(commandIndex * 16 + 12, command.lightAndFace);
  }
}

[numthreads(32, 1, 1)] void main(StageInput stageInput)
{
  dispatchThreadId = stageInput.dispatchThreadId;
  RunCompute();
}
