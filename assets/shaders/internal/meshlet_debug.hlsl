#ifndef IRYVEN_MESHLET_DEBUG_HLSL
#define IRYVEN_MESHLET_DEBUG_HLSL

float3 MeshletColor(uint meshletHash)
{
    meshletHash ^= (meshletHash >> uint(16));
    meshletHash *= 2146121005u;
    meshletHash ^= (meshletHash >> uint(15));
    meshletHash *= 2221713035u;
    meshletHash ^= (meshletHash >> uint(16));
    return 0.25f.xxx + ((float3(float(meshletHash & 255u), float((meshletHash >> uint(8)) & 255u), float((meshletHash >> uint(16)) & 255u)) * 0.75f) / 255.0f.xxx);
}

#endif
