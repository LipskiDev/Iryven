#ifndef IRYVEN_SHADER_COMPAT_HLSL
#define IRYVEN_SHADER_COMPAT_HLSL

// projectionY is the Y scale of the active projection matrix. Its sign
// carries the viewport convention already selected by the renderer.
float2 IryvenUvToNdc(float2 uv, float projectionY)
{
  return (uv * 2.0f - 1.0f) * float2(1.0f, -sign(projectionY));
}

float2 IryvenNdcToUv(float2 ndc, float projectionY)
{
  return ndc * float2(0.5f, -0.5f * sign(projectionY)) + 0.5f;
}
#endif
