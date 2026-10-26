// IRYVEN_SHADER_STAGES: VERTEX
// Explicit buffer offsets match the CPU resource layouts.

static float4 clipPosition;
static int vertexIndex;

struct StageInput
{
  uint vertexIndex : SV_VertexID;
};

struct StageOutput
{
  float4 clipPosition : SV_Position;
};

void RunVertex()
{
  float2 position = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2));
  clipPosition = float4((position * 2.0f) - 1.0f.xx, 0.0f, 1.0f);
}

StageOutput main(StageInput stageInput)
{
  vertexIndex = int(stageInput.vertexIndex);
  RunVertex();
  StageOutput stageOutput;
  stageOutput.clipPosition = clipPosition;
  return stageOutput;
}
