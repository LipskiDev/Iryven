// IRYVEN_SHADER_STAGES: VERTEX
// Stage selection is supplied by ShaderCompiler or the build rules.

#if defined(IRYVEN_STAGE_VERTEX)
// Explicit buffer offsets match the CPU resource layouts.
#include "shader_compat.hlsl"

#include "frame_uniform.hlsl"

#ifdef __spirv__
struct DrawConstantsData
{
    IRYVEN_OFFSET(0) row_major float4x4 drawReservedModel;
    IRYVEN_OFFSET(64) uint drawMaterialIndex;
    IRYVEN_OFFSET(68) uint drawReserved0;
    IRYVEN_OFFSET(72) uint drawReserved1;
    IRYVEN_OFFSET(76) uint drawPhase;
};
IRYVEN_PUSH_CONSTANT ConstantBuffer<DrawConstantsData> iryvenPush;
#define drawReservedModel iryvenPush.drawReservedModel
#define drawMaterialIndex iryvenPush.drawMaterialIndex
#define drawReserved0 iryvenPush.drawReserved0
#define drawReserved1 iryvenPush.drawReserved1
#define drawPhase iryvenPush.drawPhase
#else
IRYVEN_PUSH_CONSTANT cbuffer DrawConstants : register(b0, space2147483647)
{
    row_major float4x4 drawReservedModel : packoffset(c0);
    uint drawMaterialIndex : packoffset(c4);
    uint drawReserved0 : packoffset(c4.y);
    uint drawReserved1 : packoffset(c4.z);
    uint drawPhase : packoffset(c4.w);
};
#endif

static float4 clipPosition;
static float4 instanceModel0;
static float4 instanceModel1;
static float4 instanceModel2;
static float4 instanceModel3;
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
    IRYVEN_LOCATION(0) float3 inputPosition : SHADER_INPUT0;
    IRYVEN_LOCATION(1) float3 inputNormal : SHADER_INPUT1;
    IRYVEN_LOCATION(2) float2 inputTexCoord : SHADER_INPUT2;
    IRYVEN_LOCATION(3) float4 inputTangent : SHADER_INPUT3;
    IRYVEN_LOCATION(4) float4 inputColor : SHADER_INPUT4;
    IRYVEN_LOCATION(5) float4 instanceModel0 : SHADER_INPUT5;
    IRYVEN_LOCATION(6) float4 instanceModel1 : SHADER_INPUT6;
    IRYVEN_LOCATION(7) float4 instanceModel2 : SHADER_INPUT7;
    IRYVEN_LOCATION(8) float4 instanceModel3 : SHADER_INPUT8;
};

struct StageOutput
{
    IRYVEN_LOCATION(0) float3 worldPosition : TEXCOORD0;
    IRYVEN_LOCATION(1) float3 worldNormal : TEXCOORD1;
    IRYVEN_LOCATION(2) float2 texCoord : TEXCOORD2;
    IRYVEN_LOCATION(3) float4 worldTangent : TEXCOORD3;
    IRYVEN_LOCATION(4) float4 vertexColor : TEXCOORD4;
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
    float4x4 model = float4x4(float4(instanceModel0), float4(instanceModel1), float4(instanceModel2), float4(instanceModel3));
    float4 transformedPosition = mul(float4(inputPosition, 1.0f), model);
    clipPosition = mul(transformedPosition, frameViewProjection);
    worldPosition = transformedPosition.xyz;
    worldNormal = TransformNormal(inputNormal, model);
    texCoord = inputTexCoord;
    float3 transformedTangent = mul(inputTangent.xyz, float3x3(model[0].xyz, model[1].xyz, model[2].xyz));
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
    instanceModel0 = stageInput.instanceModel0;
    instanceModel1 = stageInput.instanceModel1;
    instanceModel2 = stageInput.instanceModel2;
    instanceModel3 = stageInput.instanceModel3;
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

#else
#error Select a supported IRYVEN_STAGE_* macro for this shader
#endif
