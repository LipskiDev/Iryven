// IRYVEN_SHADER_STAGES: FRAGMENT VERTEX
// Stage selection is supplied by ShaderCompiler or the build rules.

#if defined(IRYVEN_STAGE_FRAGMENT)
// Explicit buffer offsets match the CPU resource layouts.
#include "shader_compat.hlsl"

IRYVEN_COMBINED IRYVEN_BINDING(0, 0)
Texture2D<float4> hdrSceneColor : register(t0, space0);
IRYVEN_COMBINED IRYVEN_BINDING(0, 0)
SamplerState hdrSceneColorSampler : register(s0, space0);

static float2 textureCoordinates;
static float4 outputColor;

struct StageInput
{
    IRYVEN_LOCATION(0) float2 textureCoordinates : TEXCOORD0;
};

struct StageOutput
{
    IRYVEN_LOCATION(0) float4 outputColor : SV_Target0;
};

void RunFragment()
{
    float3 hdrColor = hdrSceneColor.Sample(hdrSceneColorSampler, textureCoordinates).xyz;
    float3 mappedColor = hdrColor / (1.0f.xxx + hdrColor);
    outputColor = float4(mappedColor, 1.0f);
}

StageOutput main(StageInput stageInput)
{
    textureCoordinates = stageInput.textureCoordinates;
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
static float2 textureCoordinates;

struct StageInput
{
    uint vertexIndex : SV_VertexID;
};

struct StageOutput
{
    IRYVEN_LOCATION(0) float2 textureCoordinates : TEXCOORD0;
    float4 clipPosition : SV_Position;
};

void RunVertex()
{
    float2 position = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
    textureCoordinates = IryvenNdcToUv(position * 2.0f - 1.0f);
    clipPosition = float4((position * 2.0f) - 1.0f.xx, 0.0f, 1.0f);
}

StageOutput main(StageInput stageInput)
{
    vertexIndex = int(stageInput.vertexIndex);
    RunVertex();
    StageOutput stageOutput;
    stageOutput.clipPosition = clipPosition;
    stageOutput.textureCoordinates = textureCoordinates;
    return stageOutput;
}

#else
#error Select a supported IRYVEN_STAGE_* macro for this shader
#endif
