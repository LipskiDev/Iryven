#ifndef IRYVEN_LIGHT_TILES_HLSL
#define IRYVEN_LIGHT_TILES_HLSL
static const uint LIGHT_TILE_SIZE = 8u;
static const uint LIGHT_TILE_WORDS = 16u;

bool IryvenLightOverlapsTile(ByteAddressBuffer lightTileBuffer, uint localLightIndex, float2 pixelPosition)
{
    // xy: tile grid extent; z: tile size in pixels; w: mask words per tile.
    uint4 tileGridHeader = lightTileBuffer.Load<uint4>(0);
    uint2 tileCoordinate = uint2(pixelPosition) / tileGridHeader.z;
    if (any(tileCoordinate >= tileGridHeader.xy))
    {
        return false;
    }
    uint tileMaskOffset = (tileCoordinate.y * tileGridHeader.x + tileCoordinate.x) * tileGridHeader.w;
    uint lightMask = lightTileBuffer.Load<uint>(16 + 4 * (tileMaskOffset + localLightIndex / 32));
    return (lightMask & (1u << (localLightIndex % 32))) != 0;
}
#endif
