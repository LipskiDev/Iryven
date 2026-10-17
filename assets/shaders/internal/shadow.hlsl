// IRYVEN_SHADER_STAGES: FRAGMENT MESH TASK
// Stage selection is supplied by ShaderCompiler or the build rules.

#if defined(IRYVEN_STAGE_FRAGMENT)
// Explicit buffer offsets match the CPU resource layouts.
#include "shader_compat.hlsl"

void main()
{
    // Depth-only pass; no color output is needed.
}

#elif defined(IRYVEN_STAGE_MESH)
// Explicit buffer offsets match the CPU resource layouts.
#include "shader_compat.hlsl"

struct Meshlet
{
    uint vertexOffset;
    uint triangleOffset;
    uint vertexCount;
    uint triangleCount;
    float3 center;
    float radius;
    float3 coneApex;
    float3 coneAxis;
    float coneCutoff;
};

IRYVEN_BINDING(1, 1)
ByteAddressBuffer meshletBuffer : register(t1, space1);
IRYVEN_BINDING(0, 0)
cbuffer ShadowMatrices : register(b0, space0)
{
    row_major float4x4 shadowViewProjections[768] : packoffset(c0);
};

IRYVEN_BINDING(2, 1)
ByteAddressBuffer meshletVertexIndices : register(t2, space1);
IRYVEN_BINDING(0, 1)
ByteAddressBuffer vertexBuffer : register(t0, space1);
IRYVEN_BINDING(3, 1)
ByteAddressBuffer meshletTriangleIndices : register(t3, space1);
#ifdef __spirv__
struct ShadowDrawConstantsData
{
    IRYVEN_OFFSET(0) row_major float4x4 drawModel;
    IRYVEN_OFFSET(64) uint4 drawMeshletInfo;
};
IRYVEN_PUSH_CONSTANT ConstantBuffer<ShadowDrawConstantsData> iryvenPush;
#define drawModel iryvenPush.drawModel
#define drawMeshletInfo iryvenPush.drawMeshletInfo
#else
IRYVEN_PUSH_CONSTANT cbuffer ShadowDrawConstants : register(b0, space2147483647)
{
    row_major float4x4 drawModel : packoffset(c0);
    uint4 drawMeshletInfo : packoffset(c4);
};
#endif

static uint3 groupId;
static uint groupThreadIndex;
struct StageInput
{
    uint3 groupId : SV_GroupID;
    uint groupThreadIndex : SV_GroupIndex;
};

struct MeshVertex
{
    float4 clipPosition : SV_Position;
};

struct MeshPrimitive
{
    uint renderTargetLayer : SV_RenderTargetArrayIndex;
};

[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void main(
    StageInput stageInput,
    out vertices MeshVertex outputVertices[64],
    out indices uint3 outputTriangles[128],
    out primitives MeshPrimitive outputPrimitives[128])
{
    groupId = stageInput.groupId;
    groupThreadIndex = stageInput.groupThreadIndex;

    uint meshletIndex = drawMeshletInfo.x + groupId.x;
    uint cubeFaceIndex = groupId.y;
    uint layer = drawMeshletInfo.y + cubeFaceIndex;
    Meshlet meshlet;
    meshlet.vertexOffset = meshletBuffer.Load<uint>(meshletIndex * 64 + 0);
    meshlet.triangleOffset = meshletBuffer.Load<uint>(meshletIndex * 64 + 4);
    meshlet.vertexCount = meshletBuffer.Load<uint>(meshletIndex * 64 + 8);
    meshlet.triangleCount = meshletBuffer.Load<uint>(meshletIndex * 64 + 12);
    meshlet.center = meshletBuffer.Load<float3>(meshletIndex * 64 + 16);
    meshlet.radius = meshletBuffer.Load<float>(meshletIndex * 64 + 28);
    meshlet.coneApex = meshletBuffer.Load<float3>(meshletIndex * 64 + 32);
    meshlet.coneAxis = meshletBuffer.Load<float3>(meshletIndex * 64 + 48);
    meshlet.coneCutoff = meshletBuffer.Load<float>(meshletIndex * 64 + 60);
    float4x4 modelViewProjection = mul(drawModel, shadowViewProjections[layer]);
    float3 worldCenter = mul(float4(meshlet.center, 1.0f), drawModel).xyz;
    float worldRadius = meshlet.radius * max(max(length(drawModel[0].xyz), length(drawModel[1].xyz)), length(drawModel[2].xyz));
    float4x4 planeRows = transpose(shadowViewProjections[layer]);
    // Left, right, bottom, top, near, far clip planes.
    float4 planes[6] = {
        planeRows[3] + planeRows[0],
        planeRows[3] - planeRows[0],
        planeRows[3] + planeRows[1],
        planeRows[3] - planeRows[1],
        planeRows[2],
        planeRows[3] - planeRows[2]
    };
    bool visible = true;
    for (uint planeIndex = 0u; planeIndex < 6u; planeIndex++)
    {
        float4 plane = planes[planeIndex];
        if (dot(plane, float4(worldCenter, 1.0f)) < ((-worldRadius) * length(plane.xyz)))
        {
            visible = false;
        }
    }
    // DXIL requires one group-uniform output-count declaration per mesh group.
    SetMeshOutputCounts(visible ? meshlet.vertexCount : 0u, visible ? meshlet.triangleCount : 0u);
    if (!visible) return;
    for (uint vertexIndexInMeshlet = groupThreadIndex; vertexIndexInMeshlet < meshlet.vertexCount; vertexIndexInMeshlet += 32u)
    {
        uint vertexIndex = meshletVertexIndices.Load<uint>((meshlet.vertexOffset + vertexIndexInMeshlet) * 4 + 0);
        uint vertexElementOffset = vertexIndex * 16u;
        outputVertices[vertexIndexInMeshlet].clipPosition = mul(float4(vertexBuffer.Load<float>(vertexElementOffset * 4 + 0), vertexBuffer.Load<float>((vertexElementOffset + 1u) * 4 + 0), vertexBuffer.Load<float>((vertexElementOffset + 2u) * 4 + 0), 1.0f), modelViewProjection);
    }
    for (uint triangleIndex = groupThreadIndex; triangleIndex < meshlet.triangleCount; triangleIndex += 32u)
    {
        uint triangleIndexOffset = meshlet.triangleOffset + (triangleIndex * 3u);
        outputTriangles[triangleIndex] = uint3(meshletTriangleIndices.Load<uint>(triangleIndexOffset * 4 + 0), meshletTriangleIndices.Load<uint>((triangleIndexOffset + 1u) * 4 + 0), meshletTriangleIndices.Load<uint>((triangleIndexOffset + 2u) * 4 + 0));
        outputPrimitives[triangleIndex].renderTargetLayer = int(cubeFaceIndex);
    }

}

#elif defined(IRYVEN_STAGE_TASK)
#include "shadow_common.hlsl"
// Explicit buffer offsets match the CPU resource layouts.
#include "shader_compat.hlsl"

struct MeshletBounds
{
    float4 sphere;
};

struct ShadowTaskPayload
{
    uint meshletIndices[32];
    uint objectIndices[32];
    uint lightAndFace;
};

IRYVEN_BINDING(0, 0)
ByteAddressBuffer shadowDrawCommands : register(t0, space0);
IRYVEN_BINDING(1, 0)
ByteAddressBuffer shadowMeshletInstances : register(t1, space0);
IRYVEN_BINDING(2, 0)
ByteAddressBuffer meshletBoundingSpheres : register(t2, space0);
IRYVEN_BINDING(3, 0)
ByteAddressBuffer objectTransforms : register(t3, space0);
IRYVEN_BINDING(4, 0)
ByteAddressBuffer shadowLightPositions : register(t4, space0);
#ifdef __spirv__
struct ShadowTaskConstantsData
{
    IRYVEN_OFFSET(0) uint drawDataCommandIndex;
    IRYVEN_OFFSET(4) uint drawDataMaxCandidatesPerLight;
};
IRYVEN_PUSH_CONSTANT ConstantBuffer<ShadowTaskConstantsData> iryvenPush;
#define drawDataCommandIndex iryvenPush.drawDataCommandIndex
#define drawDataMaxCandidatesPerLight iryvenPush.drawDataMaxCandidatesPerLight
#else
IRYVEN_PUSH_CONSTANT cbuffer ShadowDrawData : register(b0, space2147483647)
{
    uint drawDataCommandIndex : packoffset(c0);
    uint drawDataMaxCandidatesPerLight : packoffset(c0.y);
};
#endif

static uint3 groupId;
static uint groupThreadIndex;
static uint4 precedingLaneMask;
struct StageInput
{
    uint3 groupId : SV_GroupID;
    uint groupThreadIndex : SV_GroupIndex;
};

groupshared ShadowTaskPayload meshletPayload;

void RunTask()
{
    ShadowDrawCommand command;
    command.groupCountX = shadowDrawCommands.Load<uint>(drawDataCommandIndex * 16 + 0);
    command.groupCountY = shadowDrawCommands.Load<uint>(drawDataCommandIndex * 16 + 4);
    command.groupCountZ = shadowDrawCommands.Load<uint>(drawDataCommandIndex * 16 + 8);
    command.lightAndFace = shadowDrawCommands.Load<uint>(drawDataCommandIndex * 16 + 12);
    uint lightIndex = ShadowLightIndex(command.lightAndFace);
    uint faceIndex = ShadowFaceIndex(command.lightAndFace);
    uint candidateIndex = (groupId.x * 32u) + groupThreadIndex;
    uint readOffset = (lightIndex * drawDataMaxCandidatesPerLight) + candidateIndex;
    ShadowMeshletInstance candidate;
    candidate.objectIndex = shadowMeshletInstances.Load<uint>(readOffset * 16 + 0);
    candidate.meshletIndex = shadowMeshletInstances.Load<uint>(readOffset * 16 + 4);
    candidate.padding0 = shadowMeshletInstances.Load<uint>(readOffset * 16 + 8);
    candidate.padding1 = shadowMeshletInstances.Load<uint>(readOffset * 16 + 12);
    float4 localSphere = meshletBoundingSpheres.Load<float4>(candidate.meshletIndex * 16 + 0);
    float4x4 model = float4x4(objectTransforms.Load<float4>(candidate.objectIndex * 64 + 0), objectTransforms.Load<float4>(candidate.objectIndex * 64 + 16), objectTransforms.Load<float4>(candidate.objectIndex * 64 + 32), objectTransforms.Load<float4>(candidate.objectIndex * 64 + 48));
    float3 center = mul(float4(localSphere.xyz, 1.0f), model).xyz;
    float scale = max(max(length(model[0].xyz), length(model[1].xyz)), length(model[2].xyz));
    float radius = localSphere.w * scale;
    uint faceMask = ShadowCubeFaceMask((shadowLightPositions.Load<float4>(lightIndex * 16 + 0).xyz), (center - radius.xxx), (center + radius.xxx));
    bool visible = (faceMask & (1u << faceIndex)) != 0u;
    uint4 ballot = WaveActiveBallot(visible);
    uint outputIndex = countbits(ballot.x & precedingLaneMask.x) + countbits(ballot.y & precedingLaneMask.y) + countbits(ballot.z & precedingLaneMask.z) + countbits(ballot.w & precedingLaneMask.w);
    if (visible)
    {
        meshletPayload.meshletIndices[outputIndex] = candidate.meshletIndex;
        meshletPayload.objectIndices[outputIndex] = candidate.objectIndex;
    }
    if (groupThreadIndex == 0u)
    {
        meshletPayload.lightAndFace = command.lightAndFace;
    }
    DispatchMesh(countbits(ballot.x) + countbits(ballot.y) + countbits(ballot.z) + countbits(ballot.w), 1u, 1u, meshletPayload);
}

[numthreads(32, 1, 1)]
void main(StageInput stageInput)
{
    groupId = stageInput.groupId;
    groupThreadIndex = stageInput.groupThreadIndex;
    precedingLaneMask = (1u << (WaveGetLaneIndex() - uint4(0, 32, 64, 96))) - 1u;
    if (WaveGetLaneIndex() >= 32) precedingLaneMask.x = ~0u;
    if (WaveGetLaneIndex() >= 64) precedingLaneMask.y = ~0u;
    if (WaveGetLaneIndex() >= 96) precedingLaneMask.z = ~0u;
    if (WaveGetLaneIndex() < 32) precedingLaneMask.y = 0u;
    if (WaveGetLaneIndex() < 64) precedingLaneMask.z = 0u;
    if (WaveGetLaneIndex() < 96) precedingLaneMask.w = 0u;
    RunTask();
}

#else
#error Select a supported IRYVEN_STAGE_* macro for this shader
#endif
