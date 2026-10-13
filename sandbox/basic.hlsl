struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

static const float3 positions[3] =
{
    float3(-0.5f, -0.5f, 0.0f),
    float3(0.5f, -0.5f, 0.0f),
    float3(0.0f, 0.5f, 0.0f)
};

static const float3 colors[3] =
{
    float3(1.0f, 0.0f, 0.0f),
    float3(0.0f, 1.0f, 0.0f),
    float3(0.0f, 0.0f, 1.0f)
};

VertexOutput VSmain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.position = float4(positions[vertexId], 1.0f);
    output.color = colors[vertexId];
    return output;
}

float4 PSmain(VertexOutput input) : SV_Target0
{
    return float4(input.color, 1.0f);
}
