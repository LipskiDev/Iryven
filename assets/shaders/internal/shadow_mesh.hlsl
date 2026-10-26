// IRYVEN_SHADER_STAGES: MESH
// Explicit buffer offsets match the CPU resource layouts.

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

ByteAddressBuffer meshletBuffer : register(t1, space1);
cbuffer ShadowMatrices : register(b0, space0)
{
  row_major float4x4 shadowViewProjections[768] : packoffset(c0);
};

ByteAddressBuffer meshletVertexIndices : register(t2, space1);
ByteAddressBuffer vertexBuffer : register(t0, space1);
ByteAddressBuffer meshletTriangleIndices : register(t3, space1);
struct ShadowDrawConstants
{
  row_major float4x4 drawModel;
  uint4 drawMeshletInfo;
};

[[vk::push_constant]]
ConstantBuffer<ShadowDrawConstants> shadowDrawConstants : register(b0, space2147483647);

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

[outputtopology("triangle")][numthreads(32, 1, 1)] void main(StageInput stageInput,
                                                             out vertices MeshVertex outputVertices[64],
                                                             out indices uint3 outputTriangles[128],
                                                             out primitives MeshPrimitive outputPrimitives[128])
{
  groupId = stageInput.groupId;
  groupThreadIndex = stageInput.groupThreadIndex;

  uint meshletIndex = shadowDrawConstants.drawMeshletInfo.x + groupId.x;
  uint cubeFaceIndex = groupId.y;
  uint layer = shadowDrawConstants.drawMeshletInfo.y + cubeFaceIndex;
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
  float4x4 modelViewProjection = mul(shadowDrawConstants.drawModel, shadowViewProjections[layer]);
  float3 worldCenter = mul(float4(meshlet.center, 1.0f), shadowDrawConstants.drawModel).xyz;
  float worldRadius = meshlet.radius * max(max(length(shadowDrawConstants.drawModel[0].xyz),
                                               length(shadowDrawConstants.drawModel[1].xyz)),
                                           length(shadowDrawConstants.drawModel[2].xyz));
  float4x4 planeRows = transpose(shadowViewProjections[layer]);
  // Left, right, bottom, top, near, far clip planes.
  float4 planes[6] = {planeRows[3] + planeRows[0],
                      planeRows[3] - planeRows[0],
                      planeRows[3] + planeRows[1],
                      planeRows[3] - planeRows[1],
                      planeRows[2],
                      planeRows[3] - planeRows[2]};
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
  if (!visible)
    return;
  for (uint vertexIndexInMeshlet = groupThreadIndex; vertexIndexInMeshlet < meshlet.vertexCount;
       vertexIndexInMeshlet += 32u)
  {
    uint vertexIndex = meshletVertexIndices.Load<uint>((meshlet.vertexOffset + vertexIndexInMeshlet) * 4 + 0);
    uint vertexElementOffset = vertexIndex * 16u;
    outputVertices[vertexIndexInMeshlet].clipPosition =
        mul(float4(vertexBuffer.Load<float>(vertexElementOffset * 4 + 0),
                   vertexBuffer.Load<float>((vertexElementOffset + 1u) * 4 + 0),
                   vertexBuffer.Load<float>((vertexElementOffset + 2u) * 4 + 0), 1.0f),
            modelViewProjection);
  }
  for (uint triangleIndex = groupThreadIndex; triangleIndex < meshlet.triangleCount; triangleIndex += 32u)
  {
    uint triangleIndexOffset = meshlet.triangleOffset + (triangleIndex * 3u);
    outputTriangles[triangleIndex] = uint3(meshletTriangleIndices.Load<uint>(triangleIndexOffset * 4 + 0),
                                           meshletTriangleIndices.Load<uint>((triangleIndexOffset + 1u) * 4 + 0),
                                           meshletTriangleIndices.Load<uint>((triangleIndexOffset + 2u) * 4 + 0));
    outputPrimitives[triangleIndex].renderTargetLayer = int(cubeFaceIndex);
  }
}
