#version 460
#extension GL_EXT_mesh_shader : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec4 vertexColor;
layout(location = 2) in vec3 worldNormal;
layout(location = 3) in vec4 worldTangent;
layout(location = 4) perprimitiveEXT in flat uint meshletId;
layout(location = 5) in vec3 worldPosition;
layout(location = 6) perprimitiveEXT in flat vec3 meshletCenter;
layout(location = 0) out vec4 outputColor;

layout(push_constant) uniform DrawConstants {
    mat4 model;
    uvec4 meshletInfo; // x=first meshlet, y=material, w=transmission phase.
} draw;

#include "gltf_material.glsl"

// 0: material, 1: meshlet ID, 2: world-space bounding sphere center.
const uint debugMode = 0u;

vec3 MeshletColor(uint id) {
    id ^= id >> 16;
    id *= 0x7feb352du;
    id ^= id >> 15;
    id *= 0x846ca68bu;
    id ^= id >> 16;
    return 0.25 + 0.75 * vec3(id & 255u, (id >> 8) & 255u, (id >> 16) & 255u) / 255.0;
}

void main() {
    ShadeMaterial(draw.meshletInfo.y, draw.meshletInfo.w);
    if (debugMode != 0u) {
        // Leave the attenuation pass neutral when displaying debug colors.
        vec3 color = debugMode == 1u ? MeshletColor(meshletId)
            : clamp((meshletCenter + vec3(100.0)) / 200.0, 0.0, 1.0);
        outputColor = vec4(draw.meshletInfo.w == 1u ? vec3(0.0) : color, 1.0);
    }
}
