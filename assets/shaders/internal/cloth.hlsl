// IRYVEN_SHADER_STAGES: COMPUTE VERTEX
// Stage selection is supplied by ShaderCompiler or the build rules.

#if defined(IRYVEN_STAGE_COMPUTE)
// Explicit buffer offsets match the CPU resource layouts.
#include "shader_compat.hlsl"

struct ClothParticle
{
    float4 positionAndInverseMass;
    float4 previousPosition;
    float4 normal;
};

IRYVEN_BINDING(0, 0)
ByteAddressBuffer inputParticles : register(t0, space0);
IRYVEN_BINDING(1, 0)
RWByteAddressBuffer outputParticles : register(u1, space0);
#ifdef __spirv__
struct ClothSimulationConstantsData
{
    IRYVEN_OFFSET(0) uint constantsResolutionX;
    IRYVEN_OFFSET(4) uint constantsResolutionY;
    IRYVEN_OFFSET(8) float constantsClothWidth;
    IRYVEN_OFFSET(12) float constantsClothHeight;
    IRYVEN_OFFSET(16) float constantsDeltaTime;
    IRYVEN_OFFSET(20) float constantsStiffness;
    IRYVEN_OFFSET(24) float constantsDamping;
    IRYVEN_OFFSET(28) float constantsGravityScale;
    IRYVEN_OFFSET(32) uint constantsPhase;
    IRYVEN_OFFSET(36) uint constantsIteration;
    IRYVEN_OFFSET(40) uint constantsSolverIterations;
    IRYVEN_OFFSET(44) uint constantsParticleCount;
    IRYVEN_OFFSET(48) float constantsTime;
    IRYVEN_OFFSET(56) float2 constantsPadding;
};
IRYVEN_PUSH_CONSTANT ConstantBuffer<ClothSimulationConstantsData> iryvenPush;
#define constantsResolutionX iryvenPush.constantsResolutionX
#define constantsResolutionY iryvenPush.constantsResolutionY
#define constantsClothWidth iryvenPush.constantsClothWidth
#define constantsClothHeight iryvenPush.constantsClothHeight
#define constantsDeltaTime iryvenPush.constantsDeltaTime
#define constantsStiffness iryvenPush.constantsStiffness
#define constantsDamping iryvenPush.constantsDamping
#define constantsGravityScale iryvenPush.constantsGravityScale
#define constantsPhase iryvenPush.constantsPhase
#define constantsIteration iryvenPush.constantsIteration
#define constantsSolverIterations iryvenPush.constantsSolverIterations
#define constantsParticleCount iryvenPush.constantsParticleCount
#define constantsTime iryvenPush.constantsTime
#define constantsPadding iryvenPush.constantsPadding
#else
IRYVEN_PUSH_CONSTANT cbuffer ClothSimulationConstants : register(b0, space2147483647)
{
    uint constantsResolutionX : packoffset(c0);
    uint constantsResolutionY : packoffset(c0.y);
    float constantsClothWidth : packoffset(c0.z);
    float constantsClothHeight : packoffset(c0.w);
    float constantsDeltaTime : packoffset(c1);
    float constantsStiffness : packoffset(c1.y);
    float constantsDamping : packoffset(c1.z);
    float constantsGravityScale : packoffset(c1.w);
    uint constantsPhase : packoffset(c2);
    uint constantsIteration : packoffset(c2.y);
    uint constantsSolverIterations : packoffset(c2.z);
    uint constantsParticleCount : packoffset(c2.w);
    float constantsTime : packoffset(c3);
    float2 constantsPadding : packoffset(c3.z);
};
#endif

static uint3 dispatchThreadId;
struct StageInput
{
    uint3 dispatchThreadId : SV_DispatchThreadID;
};

void Integrate(uint particleIndex)
{
    ClothParticle particle;
    particle.positionAndInverseMass = inputParticles.Load<float4>(particleIndex * 48 + 0);
    particle.previousPosition = inputParticles.Load<float4>(particleIndex * 48 + 16);
    particle.normal = inputParticles.Load<float4>(particleIndex * 48 + 32);
    float inverseMass = particle.positionAndInverseMass.w;
    if (inverseMass == 0.0f)
    {
        outputParticles.Store<float4>(particleIndex * 48 + 0, particle.positionAndInverseMass);
        outputParticles.Store<float4>(particleIndex * 48 + 16, particle.previousPosition);
        outputParticles.Store<float4>(particleIndex * 48 + 32, particle.normal);
        return;
    }
    float3 position = particle.positionAndInverseMass.xyz;
    float3 previousPosition = particle.previousPosition.xyz;
    float3 velocity = position - previousPosition;
    velocity *= (1.0f - constantsDamping);
    float3 acceleration = float3(0.0f, -9.81f, 0.0f) * constantsGravityScale;
    float deltaTime = constantsDeltaTime;
    float3 newPosition = (position + velocity) + ((acceleration * deltaTime) * deltaTime);
    particle.previousPosition.xyz = position;
    particle.positionAndInverseMass.xyz = newPosition;
    outputParticles.Store<float4>(particleIndex * 48 + 0, particle.positionAndInverseMass);
    outputParticles.Store<float4>(particleIndex * 48 + 16, particle.previousPosition);
    outputParticles.Store<float4>(particleIndex * 48 + 32, particle.normal);
}

float3 ConstraintCorrection(float3 position, float3 neighborPosition, float restLength)
{
    float3 delta = neighborPosition - position;
    float constraintDistance = length(delta);
    if (constraintDistance < 1e-06f)
    {
        return 0.0f.xxx;
    }
    float error = constraintDistance - restLength;
    return ((normalize(delta) * error) * 0.5f) * constantsStiffness;
}

void SolveConstraints(uint particleIndex)
{
    ClothParticle particle;
    particle.positionAndInverseMass = outputParticles.Load<float4>(particleIndex * 48 + 0);
    particle.previousPosition = outputParticles.Load<float4>(particleIndex * 48 + 16);
    particle.normal = outputParticles.Load<float4>(particleIndex * 48 + 32);
    float inverseMass = particle.positionAndInverseMass.w;
    if (inverseMass == 0.0f)
    {
        return;
    }
    float3 position = particle.positionAndInverseMass.xyz;
    uint particleColumn = particleIndex % constantsResolutionX;
    uint particleRow = particleIndex / constantsResolutionX;
    float horizontalRestLength = constantsClothWidth / float(constantsResolutionX - 1u);
    float verticalRestLength = constantsClothHeight / float(constantsResolutionY - 1u);
    float3 totalCorrection = 0.0f.xxx;
    if (particleColumn > 0u)
    {
        uint leftNeighborIndex = particleIndex - 1u;
        ClothParticle leftNeighbor;
        leftNeighbor.positionAndInverseMass = outputParticles.Load<float4>(leftNeighborIndex * 48 + 0);
        leftNeighbor.previousPosition = outputParticles.Load<float4>(leftNeighborIndex * 48 + 16);
        leftNeighbor.normal = outputParticles.Load<float4>(leftNeighborIndex * 48 + 32);
        totalCorrection += ConstraintCorrection(position, leftNeighbor.positionAndInverseMass.xyz, horizontalRestLength);
    }
    if ((particleColumn + 1u) < constantsResolutionX)
    {
        uint rightNeighborIndex = particleIndex + 1u;
        ClothParticle rightNeighbor;
        rightNeighbor.positionAndInverseMass = outputParticles.Load<float4>(rightNeighborIndex * 48 + 0);
        rightNeighbor.previousPosition = outputParticles.Load<float4>(rightNeighborIndex * 48 + 16);
        rightNeighbor.normal = outputParticles.Load<float4>(rightNeighborIndex * 48 + 32);
        totalCorrection += ConstraintCorrection(position, rightNeighbor.positionAndInverseMass.xyz, horizontalRestLength);
    }
    if (particleRow > 0u)
    {
        uint upperNeighborIndex = particleIndex - constantsResolutionX;
        ClothParticle upperNeighbor;
        upperNeighbor.positionAndInverseMass = outputParticles.Load<float4>(upperNeighborIndex * 48 + 0);
        upperNeighbor.previousPosition = outputParticles.Load<float4>(upperNeighborIndex * 48 + 16);
        upperNeighbor.normal = outputParticles.Load<float4>(upperNeighborIndex * 48 + 32);
        totalCorrection += ConstraintCorrection(position, upperNeighbor.positionAndInverseMass.xyz, verticalRestLength);
    }
    if ((particleRow + 1u) < constantsResolutionY)
    {
        uint lowerNeighborIndex = particleIndex + constantsResolutionX;
        ClothParticle lowerNeighbor;
        lowerNeighbor.positionAndInverseMass = outputParticles.Load<float4>(lowerNeighborIndex * 48 + 0);
        lowerNeighbor.previousPosition = outputParticles.Load<float4>(lowerNeighborIndex * 48 + 16);
        lowerNeighbor.normal = outputParticles.Load<float4>(lowerNeighborIndex * 48 + 32);
        totalCorrection += ConstraintCorrection(position, lowerNeighbor.positionAndInverseMass.xyz, verticalRestLength);
    }
    position += totalCorrection;
    outputParticles.Store<float>(particleIndex * 48 + 0, position.x);
    outputParticles.Store<float>(particleIndex * 48 + 4, position.y);
    outputParticles.Store<float>(particleIndex * 48 + 8, position.z);
}

void RecalculateNormal(uint particleIndex)
{
    ClothParticle particle;
    particle.positionAndInverseMass = outputParticles.Load<float4>(particleIndex * 48 + 0);
    particle.previousPosition = outputParticles.Load<float4>(particleIndex * 48 + 16);
    particle.normal = outputParticles.Load<float4>(particleIndex * 48 + 32);
    uint particleColumn = particleIndex % constantsResolutionX;
    uint particleRow = particleIndex / constantsResolutionX;
    float3 particlePosition = particle.positionAndInverseMass.xyz;
    float3 normalSum = 0.0f.xxx;
    bool hasLeft = particleColumn > 0u;
    bool hasRight = (particleColumn + 1u) < constantsResolutionX;
    bool hasUp = particleRow > 0u;
    bool hasDown = (particleRow + 1u) < constantsResolutionY;
    float3 left;
    if (hasLeft)
    {
        left = outputParticles.Load<float4>((particleIndex - 1u) * 48 + 0).xyz;
    }
    float3 right;
    if (hasRight)
    {
        right = outputParticles.Load<float4>((particleIndex + 1u) * 48 + 0).xyz;
    }
    float3 up;
    if (hasUp)
    {
        up = outputParticles.Load<float4>((particleIndex - constantsResolutionX) * 48 + 0).xyz;
    }
    float3 down;
    if (hasDown)
    {
        down = outputParticles.Load<float4>((particleIndex + constantsResolutionX) * 48 + 0).xyz;
    }
    if (hasUp && hasLeft)
    {
        normalSum += cross(up - particlePosition, left - particlePosition);
    }
    if (hasRight && hasUp)
    {
        normalSum += cross(right - particlePosition, up - particlePosition);
    }
    if (hasDown && hasRight)
    {
        normalSum += cross(down - particlePosition, right - particlePosition);
    }
    if (hasLeft && hasDown)
    {
        normalSum += cross(left - particlePosition, down - particlePosition);
    }
    float normalLengthSquared = dot(normalSum, normalSum);
    if (normalLengthSquared > 1e-12f)
    {
        particle.normal = float4(normalize(normalSum), 0.0f);
    }
    else
    {
        particle.normal = float4(0.0f, 0.0f, 1.0f, 0.0f);
    }
    outputParticles.Store<float4>(particleIndex * 48 + 0, particle.positionAndInverseMass);
    outputParticles.Store<float4>(particleIndex * 48 + 16, particle.previousPosition);
    outputParticles.Store<float4>(particleIndex * 48 + 32, particle.normal);
}

void RunCompute()
{
    uint particleIndex = dispatchThreadId.x;
    if (particleIndex >= constantsParticleCount)
    {
        return;
    }
    switch (constantsPhase)
    {
        case 0u:
        {
            Integrate(particleIndex);
            break;
        }
        case 1u:
        {
            SolveConstraints(particleIndex);
            break;
        }
        case 2u:
        {
            RecalculateNormal(particleIndex);
            break;
        }
    }
}

[numthreads(64, 1, 1)]
void main(StageInput stageInput)
{
    dispatchThreadId = stageInput.dispatchThreadId;
    RunCompute();
}

#elif defined(IRYVEN_STAGE_VERTEX)
// Explicit buffer offsets match the CPU resource layouts.
#include "shader_compat.hlsl"

struct ClothParticle
{
    float4 positionAndInverseMass;
    float4 previousPosition;
    float4 normal;
};

IRYVEN_BINDING(0, 2)
ByteAddressBuffer clothParticles : register(t0, space2);
#include "frame_uniform.hlsl"

#ifdef __spirv__
struct DrawConstantsData
{
    IRYVEN_OFFSET(0) row_major float4x4 drawModel;
    IRYVEN_OFFSET(64) uint drawMaterialIndex;
    IRYVEN_OFFSET(68) uint drawResolutionX;
    IRYVEN_OFFSET(72) uint drawResolutionY;
    IRYVEN_OFFSET(76) uint drawPadding;
};
IRYVEN_PUSH_CONSTANT ConstantBuffer<DrawConstantsData> iryvenPush;
#define drawModel iryvenPush.drawModel
#define drawMaterialIndex iryvenPush.drawMaterialIndex
#define drawResolutionX iryvenPush.drawResolutionX
#define drawResolutionY iryvenPush.drawResolutionY
#define drawPadding iryvenPush.drawPadding
#else
IRYVEN_PUSH_CONSTANT cbuffer DrawConstants : register(b0, space2147483647)
{
    row_major float4x4 drawModel : packoffset(c0);
    uint drawMaterialIndex : packoffset(c4);
    uint drawResolutionX : packoffset(c4.y);
    uint drawResolutionY : packoffset(c4.z);
    uint drawPadding : packoffset(c4.w);
};
#endif

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
    float4 transformedPosition = mul(float4(particle.positionAndInverseMass.xyz, 1.0f), drawModel);
    clipPosition = mul(transformedPosition, frameViewProjection);
    worldPosition = transformedPosition.xyz;
    worldNormal = TransformNormal(particle.normal.xyz, drawModel);
    uint width = max(drawResolutionX, 2u);
    uint height = max(drawResolutionY, 2u);
    texCoord = float2(float(particleIndex % width) / float(width - 1u), float(particleIndex / width) / float(height - 1u));
    float3 tangent = mul(float3(1.0f, 0.0f, 0.0f), float3x3(drawModel[0].xyz, drawModel[1].xyz, drawModel[2].xyz));
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

#else
#error Select a supported IRYVEN_STAGE_* macro for this shader
#endif
