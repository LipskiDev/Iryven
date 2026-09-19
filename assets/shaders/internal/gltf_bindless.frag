#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec4 worldTangent;
layout(location = 4) in vec4 vertexColor;
layout(location = 0) out vec4 outputColor;

layout(push_constant) uniform DrawConstants {
    mat4 model;
    uint materialIndex;
    uint reserved0;
    uint reserved1;
    uint phase;
} draw;

#include "gltf_material.glsl"

void main() {
    ShadeMaterial(draw.materialIndex, draw.phase);
}
