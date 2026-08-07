struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

struct VertexInput
{
    float3 position : POSITION;
    float3 color : COLOR0;
};

struct DrawConstants
{
    column_major float4x4 modelViewProjection;
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> Draw;

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(Draw.modelViewProjection, float4(input.position, 1.0));
    output.color = input.color;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    return float4(input.color, 1.0);
}
