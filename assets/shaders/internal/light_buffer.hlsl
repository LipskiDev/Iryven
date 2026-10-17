#ifndef IRYVEN_LIGHT_BUFFER_HLSL
#define IRYVEN_LIGHT_BUFFER_HLSL
static const uint MAX_LIGHTS = 512u;
// Five 16-byte fields: 80 bytes per CPU light.
struct GpuLight
{
    float4 positionAndType;   // xyz: world position; w: directional / point / spot type.
    float4 directionAndRange; // xyz: light direction; w: attenuation range.
    float4 colorAndIntensity; // rgb: linear color; w: intensity.
    float4 spotAngles;        // x: inner cone cosine; y: outer cone cosine.
    uint4 shadowInfo;         // x: enabled; y: resolution tier; z: cube-array slot.
};

// The buffer starts with a 16-byte header, followed by 80-byte lights.
GpuLight LoadLight(ByteAddressBuffer lightBuffer, uint lightIndex)
{
    uint lightByteOffset = 16u + lightIndex * 80u;
    GpuLight light;
    light.positionAndType = lightBuffer.Load<float4>(lightByteOffset);
    light.directionAndRange = lightBuffer.Load<float4>(lightByteOffset + 16u);
    light.colorAndIntensity = lightBuffer.Load<float4>(lightByteOffset + 32u);
    light.spotAngles = lightBuffer.Load<float4>(lightByteOffset + 48u);
    light.shadowInfo = lightBuffer.Load<uint4>(lightByteOffset + 64u);
    return light;
}
#endif
