#ifndef IRYVEN_LIGHT_DEPTH_BINS_GLSL
#define IRYVEN_LIGHT_DEPTH_BINS_GLSL

layout(std430, set = 0, binding = 4) readonly buffer LightDepthBins {
    vec4 lightDepthPlane;
    vec4 lightDepthMapping; // near, far, bins per depth unit, reserved
    uint lightDepthRanges[256];
};

uvec2 LocalLightDepthRange(vec3 worldPosition, uint localCount) {
    float depth = dot(lightDepthPlane, vec4(worldPosition, 1.0));

    if (isnan(depth) || isinf(depth) || depth < lightDepthMapping.x || depth > lightDepthMapping.y)
        return uvec2(0u, localCount);
    uint bin = uint(clamp((depth - lightDepthMapping.x) * lightDepthMapping.z, 0.0, 255.0));
    uint packed = lightDepthRanges[bin];
    uint first = packed & 0xffffu;
    uint last = packed >> 16u;
    if (first > last) return uvec2(0u);
    return min(uvec2(first, last + 1u), uvec2(localCount));
}

#endif
