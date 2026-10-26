
struct VertexInput
{
  [[vk::location(0)]] float3 position : SHADER_INPUT0;
  [[vk::location(1)]] float2 uv : SHADER_INPUT1;
};

struct VertexOutput
{
  float4 position : SV_Position;
  [[vk::location(0)]] float2 uv : TEXCOORD0;
  [[vk::location(1)]] float3 worldPosition : TEXCOORD1;
};

struct DrawData
{
  float4x4 viewProjection;
  float4 baseColorFactor;
};

[[vk::push_constant]]
ConstantBuffer<DrawData> drawData : register(b0, space2147483647);

[[vk::combinedImageSampler]] Texture2D<float4> baseColorTexture : register(t0, space0);
[[vk::combinedImageSampler]] SamplerState baseColorSampler : register(s0, space0);

VertexOutput VSmain(VertexInput input)
{
  VertexOutput output;
  output.position = mul(drawData.viewProjection, float4(input.position, 1.0f));
  output.uv = input.uv;
  output.worldPosition = input.position;
  return output;
}

float4 PSmain(VertexOutput input) : SV_Target0
{
  float4 color = baseColorTexture.Sample(baseColorSampler, input.uv) * drawData.baseColorFactor;
  float3 normal = normalize(cross(ddx(input.worldPosition), ddy(input.worldPosition)));
  float diffuse = abs(dot(normal, normalize(float3(-0.4f, 0.7f, 1.0f))));
  color.rgb *= 0.25f + 0.75f * diffuse;
  // Encode linear material color for the UNORM swapchain.
  float3 srgb =
      lerp(1.055f * pow(max(color.rgb, 0.0f), 1.0f / 2.4f) - 0.055f, color.rgb * 12.92f, color.rgb <= 0.0031308f);
  return float4(srgb, color.a);
}
