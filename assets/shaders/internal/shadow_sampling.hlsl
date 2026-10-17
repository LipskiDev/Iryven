#ifndef IRYVEN_SHADOW_SAMPLING_HLSL
#define IRYVEN_SHADOW_SAMPLING_HLSL
#include "shadow_common.hlsl"
float SamplePointShadow(TextureCubeArray<float> shadowMaps, SamplerComparisonState shadowSampler,
    float3 worldPosition, float3 lightPosition, float lightRange, uint shadowLightIndex, float bias) {
    float3 lightToPosition = worldPosition - lightPosition;
    float comparisonDepth = ShadowVectorDepth(lightToPosition, lightRange) - bias;
    return shadowMaps.SampleCmpLevelZero(shadowSampler,
        float4(lightToPosition, float(shadowLightIndex)), comparisonDepth);
}
#endif
