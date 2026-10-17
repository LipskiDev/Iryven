#include "../assets/shaders/internal/shader_compat.hlsl"

struct VertexInput
{
    IRYVEN_LOCATION(0) float3 position : SHADER_INPUT0;
    IRYVEN_LOCATION(1) float2 uv : SHADER_INPUT1;
};

struct VertexOutput
{
    float4 position : SV_Position;
    IRYVEN_LOCATION(0) float2 uv : TEXCOORD0;
};


#ifdef __spirv__
struct DrawData {
    float4x4 viewProjection;
    float4 baseColorFactor;
};
IRYVEN_PUSH_CONSTANT ConstantBuffer<DrawData> drawData;
#define viewProjection drawData.viewProjection
#define baseColorFactor drawData.baseColorFactor
#else
cbuffer DrawData : register(b0, space2147483647)
{
    float4x4 viewProjection;
    float4 baseColorFactor;
};
#endif
IRYVEN_COMBINED IRYVEN_BINDING(0, 0)
Texture2D<float4> baseColorTexture : register(t0, space0);
IRYVEN_COMBINED IRYVEN_BINDING(0, 0)
SamplerState baseColorSampler : register(s0, space0);

VertexOutput VSmain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(viewProjection, float4(input.position, 1.0f));
    output.uv = input.uv;
    return output;
}

float4 PSmain(VertexOutput input) : SV_Target0
{
    float4 color = baseColorTexture.Sample(baseColorSampler, input.uv) * baseColorFactor;
    // Encode linear material color for the UNORM swapchain.
    float3 srgb = lerp(1.055f * pow(max(color.rgb, 0.0f), 1.0f / 2.4f) - 0.055f,
                      color.rgb * 12.92f, color.rgb <= 0.0031308f);
    return float4(srgb, color.a);
}
