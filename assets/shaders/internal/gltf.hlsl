// IRYVEN_SHADER_STAGES: VERTEX
// Stage selection is supplied by ShaderCompiler or the build rules.

#include "frame_uniform.hlsl"

struct DrawConstants
{
  row_major float4x4 drawModel;
  uint drawMaterialIndex;
  uint drawReserved0;
  uint drawReserved1;
  uint drawPhase;
};

[[vk::push_constant]]
ConstantBuffer<DrawConstants> drawConstants : register(b0, space2147483647);

static float4 clipPosition;
static float3 inputPosition;
static float3 worldPosition;
static float3 worldNormal;
static float3 inputNormal;
static float2 texCoord;
static float2 inputTexCoord;
static float4 inputTangent;
static float4 worldTangent;
static float4 vertexColor;
static float4 inputColor;

struct StageInput
{
  [[vk::location(0)]] float3 inputPosition : SHADER_INPUT0;
  [[vk::location(1)]] float3 inputNormal : SHADER_INPUT1;
  [[vk::location(2)]] float2 inputTexCoord : SHADER_INPUT2;
  [[vk::location(3)]] float4 inputTangent : SHADER_INPUT3;
  [[vk::location(4)]] float4 inputColor : SHADER_INPUT4;
};

struct StageOutput
{
  [[vk::location(0)]] float3 worldPosition : TEXCOORD0;
  [[vk::location(1)]] float3 worldNormal : TEXCOORD1;
  [[vk::location(2)]] float2 texCoord : TEXCOORD2;
  [[vk::location(3)]] float4 worldTangent : TEXCOORD3;
  [[vk::location(4)]] float4 vertexColor : TEXCOORD4;
  float4 clipPosition : SV_Position;
};

float3 TransformNormal(float3 normal, float4x4 model)
{
  float3 column0 = model[0].xyz;
  float3 column1 = model[1].xyz;
  float3 column2 = model[2].xyz;
  float3 cofactor0 = cross(column1, column2);
  float3 cofactor1 = cross(column2, column0);
  float3 cofactor2 = cross(column0, column1);
  float determinant = dot(column0, cofactor0);
  float determinantSign = (determinant < 0.0f) ? (-1.0f) : 1.0f;
  return normalize((((cofactor0 * normal.x) + (cofactor1 * normal.y)) + (cofactor2 * normal.z)) * determinantSign);
}

void RunVertex()
{
  float4 transformedPosition = mul(float4(inputPosition, 1.0f), drawConstants.drawModel);
  clipPosition = mul(transformedPosition, frameViewProjection);
  worldPosition = transformedPosition.xyz;
  worldNormal = TransformNormal(inputNormal, drawConstants.drawModel);
  texCoord = inputTexCoord;
  float3 transformedTangent =
      mul(inputTangent.xyz,
          float3x3(drawConstants.drawModel[0].xyz, drawConstants.drawModel[1].xyz, drawConstants.drawModel[2].xyz));
  float3 normalizedTangent;
  if (dot(transformedTangent, transformedTangent) > 0.0f)
  {
    normalizedTangent = normalize(transformedTangent);
  }
  else
  {
    normalizedTangent = float3(1.0f, 0.0f, 0.0f);
  }
  worldTangent = float4(normalizedTangent, inputTangent.w);
  vertexColor = inputColor;
}

StageOutput main(StageInput stageInput)
{
  inputPosition = stageInput.inputPosition;
  inputNormal = stageInput.inputNormal;
  inputTexCoord = stageInput.inputTexCoord;
  inputTangent = stageInput.inputTangent;
  inputColor = stageInput.inputColor;
  RunVertex();
  StageOutput stageOutput;
  stageOutput.clipPosition = clipPosition;
  stageOutput.worldPosition = worldPosition;
  stageOutput.worldNormal = worldNormal;
  stageOutput.texCoord = texCoord;
  stageOutput.worldTangent = worldTangent;
  stageOutput.vertexColor = vertexColor;
  return stageOutput;
}
