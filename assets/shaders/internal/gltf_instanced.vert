#version 460

layout(location = 0) in vec3 inputPosition;
layout(location = 1) in vec3 inputNormal;
layout(location = 2) in vec2 inputTexCoord;
layout(location = 3) in vec4 inputTangent;
layout(location = 4) in vec4 inputColor;
layout(location = 5) in vec4 instanceModel0;
layout(location = 6) in vec4 instanceModel1;
layout(location = 7) in vec4 instanceModel2;
layout(location = 8) in vec4 instanceModel3;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 texCoord;
layout(location = 3) out vec4 worldTangent;
layout(location = 4) out vec4 vertexColor;

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

layout(push_constant) uniform DrawConstants {
    mat4 reservedModel;
    uint materialIndex;
    uint reserved0;
    uint reserved1;
    uint phase;
} draw;

vec3 TransformNormal(vec3 normal, mat4 model) {
    vec3 column0 = model[0].xyz;
    vec3 column1 = model[1].xyz;
    vec3 column2 = model[2].xyz;
    vec3 cofactor0 = cross(column1, column2);
    vec3 cofactor1 = cross(column2, column0);
    vec3 cofactor2 = cross(column0, column1);
    float determinant = dot(column0, cofactor0);
    float determinantSign = determinant < 0.0 ? -1.0 : 1.0;
    return normalize(determinantSign *
        (normal.x * cofactor0 + normal.y * cofactor1 + normal.z * cofactor2));
}

void main() {
    mat4 model = mat4(instanceModel0, instanceModel1, instanceModel2, instanceModel3);
    vec4 transformedPosition = model * vec4(inputPosition, 1.0);
    gl_Position = frameViewProjection * transformedPosition;
    worldPosition = transformedPosition.xyz;
    worldNormal = TransformNormal(inputNormal, model);
    texCoord = inputTexCoord;
    vec3 transformedTangent = mat3(model) * inputTangent.xyz;
    worldTangent = vec4(
        dot(transformedTangent, transformedTangent) > 0.0
            ? normalize(transformedTangent)
            : vec3(1.0, 0.0, 0.0),
        inputTangent.w);
    vertexColor = inputColor;
}
