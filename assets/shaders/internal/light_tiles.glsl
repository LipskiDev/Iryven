#ifndef IRYVEN_LIGHT_TILES_GLSL
#define IRYVEN_LIGHT_TILES_GLSL

// Must match LightCullingPass::kTileSize and kWordsPerTile.
const uint LIGHT_TILE_SIZE = 8u;
const uint LIGHT_TILE_WORDS = 16u;

layout(std430, set = 0, binding = 3) readonly buffer LightTiles {
    // x=tile columns, y=tile rows, z=tile size, w=words per tile.
    uvec4 lightTileHeader;
    uint lightTileMasks[];
};

bool LightOverlapsTile(uint localIndex, vec2 pixel) {
    uvec2 tile = uvec2(pixel) / lightTileHeader.z;
    if (any(greaterThanEqual(tile, lightTileHeader.xy))) return false;
    uint offset = (tile.y * lightTileHeader.x + tile.x) * lightTileHeader.w;
    return (lightTileMasks[offset + localIndex / 32u] & (1u << (localIndex % 32u))) != 0u;
}

#endif
