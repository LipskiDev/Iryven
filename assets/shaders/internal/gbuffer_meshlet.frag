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
#define GBUFFER_PASS
layout(location = 0) out vec4 gBaseMetallic;
layout(location = 1) out vec4 gNormalRoughness;
layout(location = 2) out vec4 gEmissiveOcclusion;
layout(location = 3) out vec4 gSpecular;

layout(push_constant) uniform DrawConstants {
    mat4 model;
    uvec4 meshletInfo; // x=first meshlet, y=material, w=transmission phase.
} draw;

#include "gltf_material.glsl"

void main() { ShadeMaterial(draw.meshletInfo.y, 0u); }
