#ifndef IRYVEN_SHADOW_SAMPLING_GLSL
#define IRYVEN_SHADOW_SAMPLING_GLSL

#include "shadow_common.glsl"

float SamplePointShadow(samplerCubeArrayShadow shadowMaps, vec3 worldPosition,
    vec3 lightPosition, float lightRange, uint shadowLightIndex, float bias) {
    vec3 lightToPosition = worldPosition - lightPosition;
    float comparisonDepth = ShadowVectorDepth(lightToPosition, lightRange) - bias;
    return texture(shadowMaps,
        vec4(lightToPosition, float(shadowLightIndex)), comparisonDepth);
}

#endif
