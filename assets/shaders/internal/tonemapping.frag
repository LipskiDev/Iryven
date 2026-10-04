#version 460

layout(set = 0, binding = 0) uniform sampler2D hdrSceneColor;

layout(location = 0) in vec2 textureCoordinates;
layout(location = 0) out vec4 outputColor;

void main() {
    vec3 hdrColor = texture(hdrSceneColor, textureCoordinates).rgb;
    vec3 mappedColor = hdrColor / (vec3(1.0) + hdrColor);
    outputColor = vec4(mappedColor, 1.0);
}
