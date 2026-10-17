// IRYVEN_SHADER_STAGES: COMPUTE
// Stage selection is supplied by ShaderCompiler or the build rules.

#if defined(IRYVEN_STAGE_COMPUTE)
// Explicit buffer offsets match the CPU resource layouts.
#include "shader_compat.hlsl"

#ifdef __spirv__
struct DepthReductionConstantsData
{
    IRYVEN_OFFSET(0) uint2 reductionSourceExtent;
    IRYVEN_OFFSET(8) uint2 reductionDestinationExtent;
    IRYVEN_OFFSET(16) uint reductionDestinationMip;
    IRYVEN_OFFSET(20) uint reductionPadding0;
    IRYVEN_OFFSET(24) uint reductionPadding1;
    IRYVEN_OFFSET(28) uint reductionPadding2;
};
IRYVEN_PUSH_CONSTANT ConstantBuffer<DepthReductionConstantsData> iryvenPush;
#define reductionSourceExtent iryvenPush.reductionSourceExtent
#define reductionDestinationExtent iryvenPush.reductionDestinationExtent
#define reductionDestinationMip iryvenPush.reductionDestinationMip
#define reductionPadding0 iryvenPush.reductionPadding0
#define reductionPadding1 iryvenPush.reductionPadding1
#define reductionPadding2 iryvenPush.reductionPadding2
#else
IRYVEN_PUSH_CONSTANT cbuffer ReductionData : register(b0, space2147483647)
{
    uint2 reductionSourceExtent : packoffset(c0);
    uint2 reductionDestinationExtent : packoffset(c0.z);
    uint reductionDestinationMip : packoffset(c1);
    uint reductionPadding0 : packoffset(c1.y);
    uint reductionPadding1 : packoffset(c1.z);
    uint reductionPadding2 : packoffset(c1.w);
};
#endif

IRYVEN_COMBINED IRYVEN_BINDING(0, 0)
Texture2D<float4> sourceDepth : register(t0, space0);
IRYVEN_COMBINED IRYVEN_BINDING(0, 0)
SamplerState sourceDepthSampler : register(s0, space0);
IRYVEN_BINDING(1, 0)
RWTexture2D<float> destinationDepth : register(u1, space0);

static uint3 dispatchThreadId;
struct StageInput
{
    uint3 dispatchThreadId : SV_DispatchThreadID;
};

float ReduceDepth(int2 destinationPixel)
{
    if (reductionDestinationMip == 0u)
    {
        return sourceDepth.Load(int3(destinationPixel, 0)).x;
    }
    int2 sourceExtent = int2(reductionSourceExtent);
    int2 destinationSize = int2(reductionDestinationExtent);
    int2 sourceMinimumPixel = (destinationPixel * sourceExtent) / destinationSize;
    int2 sourceMaximumPixel = ((((destinationPixel + int2(1, 1)) * sourceExtent) + destinationSize) - int2(1, 1)) / destinationSize;
    float maximumDepth = 0.0f;
    for (int sourcePixelY = sourceMinimumPixel.y; sourcePixelY < sourceMaximumPixel.y; sourcePixelY++)
    {
        for (int sourcePixelX = sourceMinimumPixel.x; sourcePixelX < sourceMaximumPixel.x; sourcePixelX++)
        {
            maximumDepth = max(maximumDepth, sourceDepth.Load(int3(int2(sourcePixelX, sourcePixelY), 0)).x);
        }
    }
    return maximumDepth;
}

void RunCompute()
{
    int2 pixel = int2(dispatchThreadId.xy);
    int2 destinationExtent = int2(reductionDestinationExtent);
    if (any(bool2(pixel.x >= destinationExtent.x, pixel.y >= destinationExtent.y)))
    {
        return;
    }
    destinationDepth[pixel] = ReduceDepth(pixel).x;
}

[numthreads(8, 8, 1)]
void main(StageInput stageInput)
{
    dispatchThreadId = stageInput.dispatchThreadId;
    RunCompute();
}

#else
#error Select a supported IRYVEN_STAGE_* macro for this shader
#endif
