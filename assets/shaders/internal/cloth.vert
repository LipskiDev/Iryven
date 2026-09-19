#version 460

struct ClothParticle {
    vec4 positionAndInverseMass;
    vec4 previousPosition;
    vec4 normal;
};

layout(std140, set = 0, binding = 1) uniform FrameBuffer {
    mat4 frameView;
    mat4 frameProjection;
    mat4 frameViewProjection;
    vec4 cameraPosition;
    mat4 cullingView;
    mat4 cullingProjection;
    mat4 cullingViewProjection;
    vec4 cullingCameraPosition;
};

layout(std430, set = 2, binding = 0) readonly buffer ClothParticles {
    ClothParticle particles[];
};

layout(push_constant) uniform DrawConstants {
    mat4 model;
    uint materialIndex;
    uint resolutionX;
    uint resolutionY;
    uint padding;
} draw;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 texCoord;
layout(location = 3) out vec4 worldTangent;
layout(location = 4) out vec4 vertexColor;

vec3 TransformNormal(vec3 normal, mat4 model) {
    vec3 column0 = model[0].xyz;
    vec3 column1 = model[1].xyz;
    vec3 column2 = model[2].xyz;
    vec3 cofactor0 = cross(column1, column2);
    vec3 cofactor1 = cross(column2, column0);
    vec3 cofactor2 = cross(column0, column1);
    float determinantSign = dot(column0, cofactor0) < 0.0 ? -1.0 : 1.0;
    return normalize(determinantSign *
        (normal.x * cofactor0 + normal.y * cofactor1 + normal.z * cofactor2));
}

void main() {
    uint particleIndex = uint(gl_VertexIndex);
    ClothParticle particle = particles[particleIndex];
    vec4 transformedPosition = draw.model *
        vec4(particle.positionAndInverseMass.xyz, 1.0);

    gl_Position = frameViewProjection * transformedPosition;
    worldPosition = transformedPosition.xyz;
    worldNormal = TransformNormal(particle.normal.xyz, draw.model);

    uint width = max(draw.resolutionX, 2u);
    uint height = max(draw.resolutionY, 2u);
    texCoord = vec2(
        float(particleIndex % width) / float(width - 1u),
        float(particleIndex / width) / float(height - 1u));

    vec3 tangent = mat3(draw.model) * vec3(1.0, 0.0, 0.0);
    worldTangent = vec4(normalize(tangent), 1.0);
    vertexColor = vec4(1.0);
}
