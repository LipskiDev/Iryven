#ifndef IRYVEN_FRAME_UNIFORM_HLSL
#define IRYVEN_FRAME_UNIFORM_HLSL
#include "shader_compat.hlsl"
IRYVEN_BINDING(1, 0)
cbuffer FrameBuffer : register(b1, space0)
{
    row_major float4x4 frameView : packoffset(c0);
    row_major float4x4 frameProjection : packoffset(c4);
    row_major float4x4 frameViewProjection : packoffset(c8);
    float4 cameraPosition : packoffset(c12);
    row_major float4x4 cullingView : packoffset(c13);
    row_major float4x4 cullingProjection : packoffset(c17);
    row_major float4x4 cullingViewProjection : packoffset(c21);
    float4 cullingCameraPosition : packoffset(c25);
};
#endif
