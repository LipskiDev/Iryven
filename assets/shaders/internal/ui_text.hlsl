// IRYVEN_SHADER_STAGES: FRAGMENT VERTEX
// Stage selection is supplied by ShaderCompiler or the build rules.

#if defined(IRYVEN_STAGE_FRAGMENT)
// Explicit buffer offsets match the CPU resource layouts.
#include "shader_compat.hlsl"

IRYVEN_COMBINED IRYVEN_BINDING(0, 0)
Texture2D<float4> fontAtlas : register(t0, space0);
IRYVEN_COMBINED IRYVEN_BINDING(0, 0)
SamplerState fontAtlasSampler : register(s0, space0);

static float2 uv;
static float4 outputColor;
static float4 color;

struct StageInput
{
    IRYVEN_LOCATION(0) float2 uv : TEXCOORD0;
    IRYVEN_LOCATION(1) float4 color : TEXCOORD1;
};

struct StageOutput
{
    IRYVEN_LOCATION(0) float4 outputColor : SV_Target0;
};

uint2 GetTextureDimensions(Texture2D<float4> texture, uint mipLevel, out uint mipCount)
{
    uint2 dimensions;
    texture.GetDimensions(mipLevel, dimensions.x, dimensions.y, mipCount);
    return dimensions;
}

float Median(float red, float green, float blue)
{
    return max(min(red, green), min(max(red, green), blue));
}

float ScreenPixelRange(float2 coordinates)
{
    uint textureMipCount;
    float2 textureSizePixels = float2(int2(GetTextureDimensions(fontAtlas, uint(0), textureMipCount)));
    float2 unitRange = 4.0f.xx / textureSizePixels;
    float2 screenTextureSize = 1.0f.xx / sqrt((ddx(coordinates) * ddx(coordinates)) + (ddy(coordinates) * ddy(coordinates)));
    return max(0.5f * dot(unitRange, screenTextureSize), 1.0f);
}

void RunFragment()
{
    float3 multiChannelDistance = fontAtlas.Sample(fontAtlasSampler, uv).xyz;
    float signedDistance = Median(multiChannelDistance.x, multiChannelDistance.y, multiChannelDistance.z);
    float screenDistance = ScreenPixelRange(uv) * (signedDistance - 0.5f);
    float opacity = clamp(screenDistance + 0.5f, 0.0f, 1.0f);
    outputColor = float4(color.xyz, color.w * opacity);
}

StageOutput main(StageInput stageInput)
{
    uv = stageInput.uv;
    color = stageInput.color;
    RunFragment();
    StageOutput stageOutput;
    stageOutput.outputColor = outputColor;
    return stageOutput;
}

#elif defined(IRYVEN_STAGE_VERTEX)
// Explicit buffer offsets match the CPU resource layouts.
#include "shader_compat.hlsl"

static float4 clipPosition;
static float2 inputPosition;
static float2 uv;
static float2 inputUv;
static float4 color;
static float4 inputColor;

struct StageInput
{
    IRYVEN_LOCATION(0) float2 inputPosition : SHADER_INPUT0;
    IRYVEN_LOCATION(1) float2 inputUv : SHADER_INPUT1;
    IRYVEN_LOCATION(2) float4 inputColor : SHADER_INPUT2;
};

struct StageOutput
{
    IRYVEN_LOCATION(0) float2 uv : TEXCOORD0;
    IRYVEN_LOCATION(1) float4 color : TEXCOORD1;
    float4 clipPosition : SV_Position;
};

void RunVertex()
{
    clipPosition = float4(inputPosition, 0.0f, 1.0f);
    uv = inputUv;
    color = inputColor;
}

StageOutput main(StageInput stageInput)
{
    inputPosition = stageInput.inputPosition;
    inputUv = stageInput.inputUv;
    inputColor = stageInput.inputColor;
    RunVertex();
    StageOutput stageOutput;
    stageOutput.clipPosition = clipPosition;
    stageOutput.uv = uv;
    stageOutput.color = color;
    return stageOutput;
}

#else
#error Select a supported IRYVEN_STAGE_* macro for this shader
#endif
