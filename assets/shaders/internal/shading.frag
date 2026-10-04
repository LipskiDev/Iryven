#version 460
#extension GL_GOOGLE_include_directive : require

#include "frame_uniform.glsl"
#include "light_buffer.glsl"

layout(set = 1, binding = 0) uniform sampler2D baseMetallic;
layout(set = 1, binding = 1) uniform sampler2D normalRoughness;
layout(set = 1, binding = 2) uniform sampler2D emissiveOcclusion;
layout(set = 1, binding = 3) uniform sampler2D specular;
layout(set = 1, binding = 4) uniform sampler2D sceneDepth;
layout(set = 1, binding = 5) uniform samplerCubeArray pointShadowMap;
layout(push_constant) uniform ShadingConstants {
    mat4 inverseViewProjection;
    uint lightCountView;
	uint shadowTierView;
} shading;

float ProjectShadowDepth(float distanceToFace, float nearPlane, float farPlane) {
    return farPlane / (farPlane - nearPlane)
         - (farPlane * nearPlane) /
           ((farPlane - nearPlane) * distanceToFace);
}


float EvaluatePointShadow(GpuLight light, vec3 worldPosition, vec3 worldNormal) {
    if (light.shadowInfo.x == 0u) {
        return 1.0;
    }

    vec3 lightToFragment = worldPosition - light.positionAndType.xyz;
    vec3 fragmentToLight = -lightToFragment;

    float distanceToLight = length(lightToFragment);
    if(distanceToLight <= 0.0001) {
        return 1.0;
    }

    vec3 surfaceToLight = fragmentToLight / distanceToLight;
    float normalDotLight = max(dot(worldNormal, surfaceToLight), 0.0);

    if(normalDotLight <= 0.0) {
        return 1.0;
    }

    float distanceToFace = max(
        abs(lightToFragment.x),
        max(abs(lightToFragment.y), abs(lightToFragment.z)));

    const float nearPlane = 0.1;
    float farPlane = max(light.directionAndRange.w, nearPlane + 0.0001);

    float currentDepth = ProjectShadowDepth(distanceToFace, nearPlane, farPlane);

    vec4 shadowCoordinate = vec4(
        lightToFragment, float(light.shadowInfo.z));
    float storedDepth = textureLod(
        pointShadowMap, shadowCoordinate, float(light.shadowInfo.y)).r;

    float bias = max(
    0.005 * (1.0 - normalDotLight), 0.0005);

    return currentDepth - bias < storedDepth ? 1.0 : 0.0;
}

#define IRYVEN_POINT_SHADOW(light, worldPosition, normal) \
    EvaluatePointShadow(light, worldPosition, normal)
#include "lighting.glsl"

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
        emissive.a, f0.rgb, f0.a > 0.5, 0.0,
		shading.lightCountView != 0u, shading.shadowTierView != 0u);

    outputColor = vec4(lighting +
		((shading.lightCountView != 0u || shading.shadowTierView != 0u)
			? vec3(0.0) : emissive.rgb), 1.0);
}
