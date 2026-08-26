#version 460

layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 outputColor;

layout(set = 0, binding = 0) uniform sampler2D fontAtlas;

const float pixelRange = 4.0;

float ScreenPixelRange(vec2 coordinates) {
    vec2 textureSizePixels = vec2(textureSize(fontAtlas, 0));
    vec2 unitRange = vec2(pixelRange) / textureSizePixels;
    vec2 screenTextureSize = 1.0 /
        sqrt(dFdx(coordinates) * dFdx(coordinates) +
            dFdy(coordinates) * dFdy(coordinates));
    return max(0.5 * dot(unitRange, screenTextureSize), 1.0);
}

float Median(float red, float green, float blue) {
    return max(min(red, green), min(max(red, green), blue));
}

void main() {
    vec3 multiChannelDistance = texture(fontAtlas, uv).rgb;
    float signedDistance = Median(
        multiChannelDistance.r,
        multiChannelDistance.g,
        multiChannelDistance.b);
    float screenDistance = ScreenPixelRange(uv) *
        (signedDistance - 0.5);
    float opacity = clamp(screenDistance + 0.5, 0.0, 1.0);
    outputColor = vec4(color.rgb, color.a * opacity);
}
