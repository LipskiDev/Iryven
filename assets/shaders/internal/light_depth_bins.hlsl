#ifndef IRYVEN_LIGHT_DEPTH_BINS_HLSL
#define IRYVEN_LIGHT_DEPTH_BINS_HLSL
uint2 IryvenLocalLightDepthRange(ByteAddressBuffer depthBinBuffer, float3 worldPosition, uint localLightCount)
{
    float depth = dot(depthBinBuffer.Load<float4>(0), float4(worldPosition, 1.0));
    // x: minimum depth; y: maximum depth; z: inverse bin width.
    float4 depthMapping = depthBinBuffer.Load<float4>(16);
    if (isnan(depth) || isinf(depth) || depth < depthMapping.x || depth > depthMapping.y)
    {
        return uint2(0, localLightCount);
    }
    uint depthBinIndex = uint(clamp((depth - depthMapping.x) * depthMapping.z, 0.0, 255.0));
    uint packedLightRange = depthBinBuffer.Load<uint>(32 + depthBinIndex * 4);
    uint firstLightIndex = packedLightRange & 0xffffu;
    uint lastLightIndex = packedLightRange >> 16;
    if (firstLightIndex > lastLightIndex)
    {
        return uint2(0, 0);
    }
    return min(uint2(firstLightIndex, lastLightIndex + 1), uint2(localLightCount, localLightCount));
}
#endif
