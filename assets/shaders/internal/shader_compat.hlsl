#ifndef IRYVEN_SHADER_COMPAT_HLSL
#define IRYVEN_SHADER_COMPAT_HLSL
#ifdef __spirv__
#define IRYVEN_BINDING(slot, space) [[vk::binding(slot, space)]]
#define IRYVEN_LOCATION(index) [[vk::location(index)]]
#define IRYVEN_OFFSET(bytes) [[vk::offset(bytes)]]
#define IRYVEN_COMBINED [[vk::combinedImageSampler]]
#define IRYVEN_PUSH_CONSTANT [[vk::push_constant]]
#define IRYVEN_PER_PRIMITIVE [[vk::ext_decorate(5271)]]
#else
#define IRYVEN_BINDING(binding, set)
#define IRYVEN_LOCATION(location)
#define IRYVEN_OFFSET(bytes)
#define IRYVEN_COMBINED
#define IRYVEN_PUSH_CONSTANT
#define IRYVEN_PER_PRIMITIVE
#endif

// Texture coordinates start at the top left on both backends. Vulkan's
// positive-height viewport maps NDC -Y to the top; D3D12 maps NDC +Y there.
float2 IryvenUvToNdc(float2 uv)
{
    float2 ndc = uv * 2.0f - 1.0f;
#ifndef __spirv__
    ndc.y = -ndc.y;
#endif
    return ndc;
}

float2 IryvenNdcToUv(float2 ndc)
{
#ifndef __spirv__
    ndc.y = -ndc.y;
#endif
    return ndc * 0.5f + 0.5f;
}
#endif
