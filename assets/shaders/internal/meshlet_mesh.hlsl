// IRYVEN_SHADER_STAGES: MESH
// Explicit buffer offsets match the CPU resource layouts.

struct TaskPayload
{
  uint meshletIndex;
};

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

ByteAddressBuffer meshletBuffer : register(t1, space2);
ByteAddressBuffer meshletVertexIndices : register(t2, space2);
ByteAddressBuffer vertexBuffer : register(t0, space2);
#include "frame_uniform.hlsl"

ByteAddressBuffer meshletTriangleIndices : register(t3, space2);
struct DrawConstants
{
  row_major float4x4 drawModel;
  uint4 drawMeshletInfo;
};

[[vk::push_constant]]
ConstantBuffer<DrawConstants> drawConstants : register(b0, space2147483647);

static uint groupThreadIndex;
struct StageInput
{
  uint groupThreadIndex : SV_GroupIndex;
};

struct MeshVertex
{
  float2 outTexCoord : TEXCOORD0;
  float4 outColor : TEXCOORD1;
  float3 outNormal : TEXCOORD2;
  float4 outTangent : TEXCOORD3;

  nointerpolation uint outMeshletId : TEXCOORD4;

  float3 outWorldPos : TEXCOORD5;

  nointerpolation float3 outMeshletCenter : TEXCOORD6;

  float4 clipPosition : SV_Position;
};

// Returns the determinant of a 2x2 matrix.
float Determinant2x2(float topLeft, float topRight, float bottomLeft, float bottomRight)
{
  return topLeft * bottomRight - bottomLeft * topRight;
}

// Invert the matrix using its adjugate and determinant.
float3x3 InverseMatrix3x3(float3x3 matrix)
{
  float3x3 adjugateMatrix;  // The adjoint matrix (inverse after dividing by determinant)

  // Create the transpose of the cofactors, as the classical adjoint of the matrix.
  adjugateMatrix[0][0] = Determinant2x2(matrix[1][1], matrix[1][2], matrix[2][1], matrix[2][2]);
  adjugateMatrix[0][1] = -Determinant2x2(matrix[0][1], matrix[0][2], matrix[2][1], matrix[2][2]);
  adjugateMatrix[0][2] = Determinant2x2(matrix[0][1], matrix[0][2], matrix[1][1], matrix[1][2]);

  adjugateMatrix[1][0] = -Determinant2x2(matrix[1][0], matrix[1][2], matrix[2][0], matrix[2][2]);
  adjugateMatrix[1][1] = Determinant2x2(matrix[0][0], matrix[0][2], matrix[2][0], matrix[2][2]);
  adjugateMatrix[1][2] = -Determinant2x2(matrix[0][0], matrix[0][2], matrix[1][0], matrix[1][2]);

  adjugateMatrix[2][0] = Determinant2x2(matrix[1][0], matrix[1][1], matrix[2][0], matrix[2][1]);
  adjugateMatrix[2][1] = -Determinant2x2(matrix[0][0], matrix[0][1], matrix[2][0], matrix[2][1]);
  adjugateMatrix[2][2] = Determinant2x2(matrix[0][0], matrix[0][1], matrix[1][0], matrix[1][1]);

  // Calculate the determinant from the cofactors of the first row.
  float matrixDeterminant = (adjugateMatrix[0][0] * matrix[0][0]) + (adjugateMatrix[0][1] * matrix[1][0]) +
                            (adjugateMatrix[0][2] * matrix[2][0]);

  // Divide the classical adjoint matrix by the determinant.
  // If determinant is zero, matrix is not invertable, so leave it unchanged.
  return (matrixDeterminant != 0.0f) ? (adjugateMatrix * (1.0f / matrixDeterminant)) : matrix;
}

[outputtopology("triangle")][numthreads(32, 1, 1)] void main(StageInput stageInput,
                                                             in payload TaskPayload meshletPayload,
                                                             out vertices MeshVertex outputVertices[64],
                                                             out indices uint3 outputTriangles[128])
{
  groupThreadIndex = stageInput.groupThreadIndex;

  uint meshletId = drawConstants.drawMeshletInfo.x + meshletPayload.meshletIndex;
  Meshlet meshlet;
  meshlet.vertexOffset = meshletBuffer.Load<uint>(meshletId * 64 + 0);
  meshlet.triangleOffset = meshletBuffer.Load<uint>(meshletId * 64 + 4);
  meshlet.vertexCount = meshletBuffer.Load<uint>(meshletId * 64 + 8);
  meshlet.triangleCount = meshletBuffer.Load<uint>(meshletId * 64 + 12);
  meshlet.center = meshletBuffer.Load<float3>(meshletId * 64 + 16);
  meshlet.radius = meshletBuffer.Load<float>(meshletId * 64 + 28);
  meshlet.coneApex = meshletBuffer.Load<float3>(meshletId * 64 + 32);
  meshlet.coneAxis = meshletBuffer.Load<float3>(meshletId * 64 + 48);
  meshlet.coneCutoff = meshletBuffer.Load<float>(meshletId * 64 + 60);
  SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);
  float3x3 modelBasis =
      float3x3(drawConstants.drawModel[0].xyz, drawConstants.drawModel[1].xyz, drawConstants.drawModel[2].xyz);
  float3x3 normalMatrix = transpose(InverseMatrix3x3(modelBasis));
  float orientation = (determinant(modelBasis) < 0.0f) ? (-1.0f) : 1.0f;

  float3 worldMeshletCenter = mul(float4(meshlet.center, 1.0f), drawConstants.drawModel).xyz;

  for (uint vertex = groupThreadIndex; vertex < meshlet.vertexCount; vertex += 32u)
  {
    uint sourceVertex = meshletVertexIndices.Load<uint>((meshlet.vertexOffset + vertex) * 4 + 0);
    uint vertexElementOffset = sourceVertex * 16u;
    float3 position = float3(vertexBuffer.Load<float>((vertexElementOffset + 0u) * 4 + 0),
                             vertexBuffer.Load<float>((vertexElementOffset + 1u) * 4 + 0),
                             vertexBuffer.Load<float>((vertexElementOffset + 2u) * 4 + 0));
    float3 normal = float3(vertexBuffer.Load<float>((vertexElementOffset + 3u) * 4 + 0),
                           vertexBuffer.Load<float>((vertexElementOffset + 4u) * 4 + 0),
                           vertexBuffer.Load<float>((vertexElementOffset + 5u) * 4 + 0));
    float2 texCoord = float2(vertexBuffer.Load<float>((vertexElementOffset + 6u) * 4 + 0),
                             vertexBuffer.Load<float>((vertexElementOffset + 7u) * 4 + 0));
    float4 tangent = float4(vertexBuffer.Load<float>((vertexElementOffset + 8u) * 4 + 0),
                            vertexBuffer.Load<float>((vertexElementOffset + 9u) * 4 + 0),
                            vertexBuffer.Load<float>((vertexElementOffset + 10u) * 4 + 0),
                            vertexBuffer.Load<float>((vertexElementOffset + 11u) * 4 + 0));
    float4 color = float4(vertexBuffer.Load<float>((vertexElementOffset + 12u) * 4 + 0),
                          vertexBuffer.Load<float>((vertexElementOffset + 13u) * 4 + 0),
                          vertexBuffer.Load<float>((vertexElementOffset + 14u) * 4 + 0),
                          vertexBuffer.Load<float>((vertexElementOffset + 15u) * 4 + 0));
    float4 worldPosition = mul(float4(position, 1.0f), drawConstants.drawModel);
    outputVertices[vertex].clipPosition = mul(worldPosition, frameViewProjection);
    outputVertices[vertex].outTexCoord = texCoord;
    outputVertices[vertex].outColor = color;
    outputVertices[vertex].outNormal = mul(normal, normalMatrix);
    outputVertices[vertex].outTangent = float4(mul(tangent.xyz, modelBasis), tangent.w * orientation);
    outputVertices[vertex].outWorldPos = worldPosition.xyz;
    outputVertices[vertex].outMeshletId = meshletId;
    outputVertices[vertex].outMeshletCenter = worldMeshletCenter;
  }
  for (uint triangleIndex = groupThreadIndex; triangleIndex < meshlet.triangleCount; triangleIndex += 32u)
  {
    uint sourceOffset = meshlet.triangleOffset + (triangleIndex * 3u);
    outputTriangles[triangleIndex] = uint3(meshletTriangleIndices.Load<uint>(sourceOffset * 4 + 0),
                                           meshletTriangleIndices.Load<uint>((sourceOffset + 1u) * 4 + 0),
                                           meshletTriangleIndices.Load<uint>((sourceOffset + 2u) * 4 + 0));
  }
}
