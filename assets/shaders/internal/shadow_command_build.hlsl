// IRYVEN_SHADER_STAGES: COMPUTE
// Stage selection is supplied by ShaderCompiler or the build rules.

#if defined(IRYVEN_STAGE_COMPUTE)
#include "shadow_common.hlsl"
// Explicit buffer offsets match the CPU resource layouts.
#include "shader_compat.hlsl"

IRYVEN_BINDING(0, 0)
ByteAddressBuffer visibleMeshletCounts : register(t0, space0);
IRYVEN_BINDING(1, 0)
RWByteAddressBuffer shadowDrawCommands : register(u1, space0);
#ifdef __spirv__
struct ShadowCommandConstantsData
{
    IRYVEN_OFFSET(0) uint commandDataLightCount;
};
IRYVEN_PUSH_CONSTANT ConstantBuffer<ShadowCommandConstantsData> iryvenPush;
#define commandDataLightCount iryvenPush.commandDataLightCount
#else
IRYVEN_PUSH_CONSTANT cbuffer ShadowCommandData : register(b0, space2147483647)
{
    uint commandDataLightCount : packoffset(c0);
};
#endif

static uint3 dispatchThreadId;
struct StageInput
{
    uint3 dispatchThreadId : SV_DispatchThreadID;
};

void RunCompute()
{
    uint lightIndex = dispatchThreadId.x;
    if (lightIndex >= commandDataLightCount)
    {
        return;
    }
    uint groupCount = ((visibleMeshletCounts.Load<uint>(lightIndex * 4 + 0) + 32u) - 1u) / 32u;
    for (uint cubeFaceIndex = 0u; cubeFaceIndex < 6u; cubeFaceIndex++)
    {
        uint commandIndex = (lightIndex * 6u) + cubeFaceIndex;
        ShadowDrawCommand command = { groupCount, 1u, 1u, PackShadowLightFace(lightIndex, cubeFaceIndex) };
        shadowDrawCommands.Store<uint>(commandIndex * 16 + 0, command.groupCountX);
        shadowDrawCommands.Store<uint>(commandIndex * 16 + 4, command.groupCountY);
        shadowDrawCommands.Store<uint>(commandIndex * 16 + 8, command.groupCountZ);
        shadowDrawCommands.Store<uint>(commandIndex * 16 + 12, command.lightAndFace);
    }
}

[numthreads(32, 1, 1)]
void main(StageInput stageInput)
{
    dispatchThreadId = stageInput.dispatchThreadId;
    RunCompute();
}

#else
#error Select a supported IRYVEN_STAGE_* macro for this shader
#endif
