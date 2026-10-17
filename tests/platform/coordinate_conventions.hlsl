#include "../../assets/shaders/internal/shader_compat.hlsl"

cbuffer CoordinateConstants : register(b0, space2147483647) {
  row_major float4x4 inverseViewProjection;
  float4 screenUvAndDepth;
  float4 projectedNdc;
};
RWStructuredBuffer<float4> results : register(u0, space0);
[numthreads(1, 1, 1)]
void main() {
  float2 ndc = IryvenUvToNdc(screenUvAndDepth.xy);
  float2 uv = IryvenNdcToUv(projectedNdc.xy);
  results[0] = float4(ndc, uv);
  float4 world = mul(float4(ndc, screenUvAndDepth.z, 1), inverseViewProjection);
  results[1] = world / world.w;
}
