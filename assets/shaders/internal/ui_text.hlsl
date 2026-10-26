// IRYVEN_SHADER_STAGES: FRAGMENT
// Explicit buffer offsets match the CPU resource layouts.

[[vk::combinedImageSampler]] Texture2D<float4> fontAtlas : register(t0, space0);
[[vk::combinedImageSampler]] SamplerState fontAtlasSampler : register(s0, space0);

static float2 uv;
static float4 outputColor;
static float4 color;

struct StageInput
{
  [[vk::location(0)]] float2 uv : TEXCOORD0;
  [[vk::location(1)]] float4 color : TEXCOORD1;
};

struct StageOutput
{
  [[vk::location(0)]] float4 outputColor : SV_Target0;
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
  float2 screenTextureSize =
      1.0f.xx / sqrt((ddx(coordinates) * ddx(coordinates)) + (ddy(coordinates) * ddy(coordinates)));
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
