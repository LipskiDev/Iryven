#ifndef IRYVEN_SHADOW_COMMON_GLSL
#define IRYVEN_SHADOW_COMMON_GLSL

const uint SHADOW_CUBE_FACE_COUNT = 6u;
const uint SHADOW_TASK_GROUP_SIZE = 32u;

struct ShadowMeshletInstance {
    uint objectIndex;
    uint meshletIndex;
    uint padding0;
    uint padding1;
};

struct ShadowDrawCommand {
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
    uint lightAndFace;
};

uint PackShadowLightFace(uint lightIndex, uint faceIndex) {
    return (lightIndex << 16u) | (faceIndex & 0xffffu);
}

uint ShadowLightIndex(uint packed) { return packed >> 16u; }
uint ShadowFaceIndex(uint packed) { return packed & 0xffffu; }
uint ShadowLayer(uint lightIndex, uint faceIndex) {
    return lightIndex * SHADOW_CUBE_FACE_COUNT + faceIndex;
}

// Conservative AABB-to-cubemap-face classification. A set bit means that the
// bounds may be visible in that face and should survive task-shader culling.
uint ShadowCubeFaceMask(vec3 lightPosition, vec3 boundsMin, vec3 boundsMax) {
    vec3 center = (boundsMin + boundsMax) * 0.5 - lightPosition;
    vec3 extent = (boundsMax - boundsMin) * 0.5;
    vec3 a = abs(center);
    uint mask = 0u;
    if (center.x + extent.x >= max(a.y - extent.y, a.z - extent.z)) mask |= 1u;
    if (-center.x + extent.x >= max(a.y - extent.y, a.z - extent.z)) mask |= 2u;
    if (center.y + extent.y >= max(a.x - extent.x, a.z - extent.z)) mask |= 4u;
    if (-center.y + extent.y >= max(a.x - extent.x, a.z - extent.z)) mask |= 8u;
    if (center.z + extent.z >= max(a.x - extent.x, a.y - extent.y)) mask |= 16u;
    if (-center.z + extent.z >= max(a.x - extent.x, a.y - extent.y)) mask |= 32u;
    return mask;
}

float ShadowVectorDepth(vec3 lightToPosition, float lightRange) {
    float majorAxis = max(max(abs(lightToPosition.x), abs(lightToPosition.y)),
        abs(lightToPosition.z));
    const float nearPlane = 0.01;
    float farPlane = max(lightRange, nearPlane + 0.001);
    return farPlane / (farPlane - nearPlane) -
        (nearPlane * farPlane) / ((farPlane - nearPlane) * max(majorAxis, nearPlane));
}

#endif
