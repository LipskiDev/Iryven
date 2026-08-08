struct VertexOutput
{
    float4 position : SV_Position;
};

struct VertexInput
{
    float3 position : POSITION;
};

struct DrawConstants
{
    column_major float4x4 modelViewProjection;
    float4 baseColor;
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> Draw;

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(Draw.modelViewProjection, float4(input.position, 1.0));
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    return Draw.baseColor;
}
