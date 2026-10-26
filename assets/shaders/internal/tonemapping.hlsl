// IRYVEN_SHADER_STAGES: FRAGMENT
// Explicit buffer offsets match the CPU resource layouts.

struct TonemappingConstants
{
  uint showGBuffer;
};

[[vk::push_constant]]
ConstantBuffer<TonemappingConstants> tonemappingConstants : register(b0, space2147483647);

[[vk::combinedImageSampler]] Texture2D<float4> hdrSceneColor : register(t0, space0);
[[vk::combinedImageSampler]] SamplerState hdrSceneColorSampler : register(s0, space0);

static float2 textureCoordinates;
static float4 outputColor;

struct StageInput
{
  float4 fragmentPosition : SV_Position;
};

struct StageOutput
{
  [[vk::location(0)]] float4 outputColor : SV_Target0;
};

void RunFragment()
{
  float3 hdrColor = hdrSceneColor.Sample(hdrSceneColorSampler, textureCoordinates).xyz;
  float3 mappedColor = hdrColor / (1.0f.xxx + hdrColor);
  outputColor = float4(tonemappingConstants.showGBuffer != 0u ? hdrColor : mappedColor, 1.0f);
}

StageOutput main(StageInput stageInput)
{
  uint width, height;
  hdrSceneColor.GetDimensions(width, height);
  textureCoordinates = stageInput.fragmentPosition.xy / float2(width, height);
  RunFragment();
  StageOutput stageOutput;
  stageOutput.outputColor = outputColor;
  return stageOutput;
}
