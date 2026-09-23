#version 460
#extension GL_GOOGLE_include_directive : require

#include "lighting.glsl"
#include "frame_uniform.glsl"

layout(set = 1, binding = 0) uniform sampler2D baseMetallic;
layout(set = 1, binding = 1) uniform sampler2D normalRoughness;
layout(set = 1, binding = 2) uniform sampler2D emissiveOcclusion;
layout(set = 1, binding = 3) uniform sampler2D specular;
layout(set = 1, binding = 4) uniform sampler2D sceneDepth;
layout(push_constant) uniform ShadingConstants { mat4 inverseViewProjection; } shading;
layout(location = 0) out vec4 outputColor;

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 nr = texelFetch(normalRoughness, pixel, 0);
    // Cleared normals mark background, including when there is no camera.
    if (dot(nr.xyz, nr.xyz) < 0.25) discard;
    float depth = texelFetch(sceneDepth, pixel, 0).r;
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(sceneDepth, 0));
    vec4 position = shading.inverseViewProjection * vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec3 worldPosition = position.xyz / position.w;
    vec4 base = texelFetch(baseMetallic, pixel, 0);
    vec4 emissive = texelFetch(emissiveOcclusion, pixel, 0);
    vec4 f0 = texelFetch(specular, pixel, 0);
    vec3 lighting = EvaluateDirectLighting(worldPosition, normalize(nr.xyz),
        normalize(cameraPosition.xyz - worldPosition), base.rgb, base.a, nr.a,
        emissive.a, f0.rgb, f0.a > 0.5, 0.0);
    outputColor = vec4(lighting + emissive.rgb, 1.0);
}
