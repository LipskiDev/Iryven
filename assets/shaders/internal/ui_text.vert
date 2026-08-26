#version 460

layout(location = 0) in vec2 inputPosition;
layout(location = 1) in vec2 inputUv;
layout(location = 2) in vec4 inputColor;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 color;

void main() {
    gl_Position = vec4(inputPosition, 0.0, 1.0);
    uv = inputUv;
    color = inputColor;
}
