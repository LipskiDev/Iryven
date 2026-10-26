// IRYVEN_SHADER_STAGES: VERTEX
// Explicit buffer offsets match the CPU resource layouts.

static float4 clipPosition;
static float2 inputPosition;
static float2 uv;
static float2 inputUv;
static float4 color;
static float4 inputColor;

struct StageInput
{
  [[vk::location(0)]] float2 inputPosition : SHADER_INPUT0;
  [[vk::location(1)]] float2 inputUv : SHADER_INPUT1;
  [[vk::location(2)]] float4 inputColor : SHADER_INPUT2;
};

struct StageOutput
{
  [[vk::location(0)]] float2 uv : TEXCOORD0;
  [[vk::location(1)]] float4 color : TEXCOORD1;
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
