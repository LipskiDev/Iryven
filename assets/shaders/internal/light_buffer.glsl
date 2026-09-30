#ifndef IRYVEN_LIGHT_BUFFER_GLSL
#define IRYVEN_LIGHT_BUFFER_GLSL

const uint MAX_LIGHTS = 512u;

struct GpuLight {
    vec4 positionAndType;
    vec4 directionAndRange;
    vec4 colorAndIntensity;
    vec4 spotAngles;
};

layout(std430, set = 0, binding = 0) readonly buffer LightBuffer {
    // x=total count, y=directional count, z=local offset, w=local count.
    uvec4 lightHeader;
    GpuLight lights[512];
};

#endif
