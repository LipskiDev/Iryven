#ifndef IRYVEN_LIGHTING_HLSL
#define IRYVEN_LIGHTING_HLSL
#include "light_tiles.hlsl"
#include "light_depth_bins.hlsl"
#include "shadow_sampling.hlsl"
float GeometrySchlickGGX(float normalDotView, float geometryFactor)
{
    return normalDotView / max((normalDotView * (1.0f - geometryFactor)) + geometryFactor, 1e-06f);
}

float DistributionGGX(float normalDotHalf, float alphaSquared)
{
    float denominator = ((normalDotHalf * normalDotHalf) * (alphaSquared - 1.0f)) + 1.0f;
    return alphaSquared / max((3.1415927f * denominator) * denominator, 1e-06f);
}

float3 FresnelSchlick(float viewDotHalf, float3 reflectanceAtNormal)
{
    return reflectanceAtNormal + ((1.0f.xxx - reflectanceAtNormal) * pow(1.0f - clamp(viewDotHalf, 0.0f, 1.0f), 5.0f));
}

#endif
