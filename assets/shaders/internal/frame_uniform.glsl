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
