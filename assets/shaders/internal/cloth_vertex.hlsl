// IRYVEN_SHADER_STAGES: VERTEX
struct ClothParticle
{
  float4 positionAndInverseMass;
  float4 previousPosition;
  float4 normal;
};

ByteAddressBuffer clothParticles : register(t0, space2);
#include "frame_uniform.hlsl"

struct DrawConstants
{
  row_major float4x4 drawModel;
  uint drawMaterialIndex;
  uint drawResolutionX;
  uint drawResolutionY;
  uint drawPadding;
};

[[vk::push_constant]]
ConstantBuffer<DrawConstants> drawConstants : register(b0, space2147483647);

static float4 clipPosition;
static int vertexIndex;
static float3 worldPosition;
static float3 worldNormal;
static float2 texCoord;
static float4 worldTangent;
static float4 vertexColor;

struct StageInput
{
  uint vertexIndex : SV_VertexID;
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
  float determinantSign = (dot(column0, cofactor0) < 0.0f) ? (-1.0f) : 1.0f;
  return normalize((((cofactor0 * normal.x) + (cofactor1 * normal.y)) + (cofactor2 * normal.z)) * determinantSign);
}

void RunVertex()
{
  uint particleIndex = uint(vertexIndex);
  ClothParticle particle;
  particle.positionAndInverseMass = clothParticles.Load<float4>(particleIndex * 48 + 0);
  particle.previousPosition = clothParticles.Load<float4>(particleIndex * 48 + 16);
  particle.normal = clothParticles.Load<float4>(particleIndex * 48 + 32);
  float4 transformedPosition = mul(float4(particle.positionAndInverseMass.xyz, 1.0f), drawConstants.drawModel);
  clipPosition = mul(transformedPosition, frameViewProjection);
  worldPosition = transformedPosition.xyz;
  worldNormal = TransformNormal(particle.normal.xyz, drawConstants.drawModel);
  uint width = max(drawConstants.drawResolutionX, 2u);
  uint height = max(drawConstants.drawResolutionY, 2u);
  texCoord =
      float2(float(particleIndex % width) / float(width - 1u), float(particleIndex / width) / float(height - 1u));
  float3 tangent =
      mul(float3(1.0f, 0.0f, 0.0f),
          float3x3(drawConstants.drawModel[0].xyz, drawConstants.drawModel[1].xyz, drawConstants.drawModel[2].xyz));
  worldTangent = float4(normalize(tangent), 1.0f);
  vertexColor = 1.0f.xxxx;
}

StageOutput main(StageInput stageInput)
{
  vertexIndex = int(stageInput.vertexIndex);
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
