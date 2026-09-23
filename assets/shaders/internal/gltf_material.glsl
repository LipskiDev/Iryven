struct GpuMaterial {
    vec4 baseColorFactor;
    vec4 emissiveFactor;
    vec4 metallicRoughnessNormal;
    uvec4 textureIndices0;
    uvec4 textureIndices1;
    vec4 specularGlossiness;
    vec4 transmission;
    uvec4 extensionTextures;
};

#include "lighting.glsl"
#include "frame_uniform.glsl"
layout(std430, set = 0, binding = 2) readonly buffer MaterialBuffer {
    GpuMaterial materials[];
};

layout(set = 1, binding = 0) uniform sampler2D bindlessTextures[];




vec4 SampleBindlessTexture(uint textureIndex) {
    return texture(
        bindlessTextures[nonuniformEXT(textureIndex)], texCoord);
}

float CleanRoughness(vec3 worldNormal, float perceptalRoughness) {
    vec3 ddxNormal = dFdx(worldNormal);
    vec3 ddyNormal = dFdy(worldNormal);

    float pixelVariance = dot(ddxNormal, ddxNormal) + dot(ddyNormal, ddyNormal);

    const float varianceThreshold = 0.25;

    float alpha = perceptalRoughness * perceptalRoughness;

    float kernelAlpha = min(varianceThreshold, pixelVariance);
    float cleanedAlpha = sqrt(alpha + kernelAlpha);

    return cleanedAlpha;
}

vec3 EvaluateMaterialNormal(GpuMaterial material) {
    vec3 geometricNormal = normalize(worldNormal);
    if ((material.textureIndices1.y & 4u) == 0u)
        return geometricNormal;

    vec3 tangent;
    vec3 bitangent;
    if (dot(worldTangent.xyz, worldTangent.xyz) > 0.000001) {
        tangent = normalize(worldTangent.xyz - geometricNormal *
            dot(geometricNormal, worldTangent.xyz));
        float handedness = worldTangent.w < 0.0 ? -1.0 : 1.0;
        bitangent = normalize(cross(geometricNormal, tangent)) * handedness;
    } else {
        vec3 positionDx = dFdx(worldPosition);
        vec3 positionDy = dFdy(worldPosition);
        vec2 uvDx = dFdx(texCoord);
        vec2 uvDy = dFdy(texCoord);
        float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
        if (abs(determinant) < 0.000001) return geometricNormal;
        tangent = normalize(
            (positionDx * uvDy.y - positionDy * uvDx.y) / determinant);
        tangent = normalize(tangent - geometricNormal *
            dot(geometricNormal, tangent));
        bitangent = normalize(cross(geometricNormal, tangent)) *
            (determinant < 0.0 ? -1.0 : 1.0);
    }

    vec3 tangentNormal = SampleBindlessTexture(
        material.textureIndices0.z).xyz * 2.0 - 1.0;
    tangentNormal.xy *= material.metallicRoughnessNormal.z;
    tangentNormal = normalize(tangentNormal);
    return normalize(tangent * tangentNormal.x +
        bitangent * tangentNormal.y + geometricNormal * tangentNormal.z);
}

void ShadeMaterial(uint materialIndex, uint phase) {
    GpuMaterial material = materials[materialIndex];
    uint flags = material.textureIndices1.y;
    vec4 sampledBaseColor = (flags & 1u) != 0u
        ? SampleBindlessTexture(material.textureIndices0.x)
        : vec4(1.0);
    vec4 baseColor = material.baseColorFactor *
        vertexColor * sampledBaseColor;
    vec4 sampledMetallicRoughness = (flags & 2u) != 0u
        ? SampleBindlessTexture(material.textureIndices0.y)
        : vec4(1.0);
    float metallic = clamp(material.metallicRoughnessNormal.x *
        sampledMetallicRoughness.b, 0.0, 1.0);
    float roughness = clamp(material.metallicRoughnessNormal.y *
        sampledMetallicRoughness.g, 0.045, 1.0);

    bool specularGlossiness = material.extensionTextures.z == 1u;
    vec3 f0 = mix(vec3(0.04), baseColor.rgb, metallic);
    if (specularGlossiness) {
        vec4 sampleSG = (flags & 32u) != 0u
            ? SampleBindlessTexture(material.extensionTextures.x) : vec4(1.0);
        f0 = clamp(material.specularGlossiness.rgb * sampleSG.rgb, 0.0, 1.0);
        roughness = clamp(1.0 - material.specularGlossiness.a * sampleSG.a, 0.045, 1.0);
        metallic = 0.0;
    }
    float transmission = clamp(material.transmission.x *
        ((flags & 64u) != 0u ? SampleBindlessTexture(material.extensionTextures.y).r : 1.0), 0.0, 1.0);
    if (specularGlossiness) transmission = 0.0;

    vec3 normal = EvaluateMaterialNormal(material);
    float filteredRoughness = CleanRoughness(normal, roughness);

    float ambientOcclusion = (flags & 8u) != 0u
        ? mix(1.0, SampleBindlessTexture(material.textureIndices0.w).r,
            clamp(material.metallicRoughnessNormal.w, 0.0, 1.0))
        : 1.0;
    vec3 sampledEmissive = (flags & 16u) != 0u
        ? SampleBindlessTexture(material.textureIndices1.x).rgb : vec3(1.0);
#ifdef GBUFFER_PASS
    gBaseMetallic = vec4(baseColor.rgb, metallic);
    gNormalRoughness = vec4(normal, clamp(filteredRoughness, 0.045, 1.0));
    gEmissiveOcclusion = vec4(material.emissiveFactor.rgb * sampledEmissive, ambientOcclusion);
    gSpecular = vec4(f0, specularGlossiness ? 1.0 : 0.0);
#else
    vec3 viewDirection = normalize(cameraPosition.xyz - worldPosition);
    vec3 lighting = EvaluateDirectLighting(
        worldPosition, normal, viewDirection, baseColor.rgb,
        metallic, clamp(filteredRoughness, 0.045, 1.0), ambientOcclusion,
        f0, specularGlossiness, transmission);

    // Thin-surface transmission uses two sorted draws: destination attenuation,
    // then additive surface lighting. Fresnel reflection is never faded by transmission.
    // Background filtering for rough glass is not implemented.
    if (phase == 1u) {
        vec3 fresnel = FresnelSchlick(max(dot(normal, viewDirection), 0.0), f0);
        outputColor = vec4(clamp(baseColor.rgb * (1.0 - metallic) *
            transmission * (1.0 - fresnel), 0.0, 1.0), 1.0);
    } else {
        outputColor = vec4(lighting + material.emissiveFactor.rgb * sampledEmissive,
            baseColor.a);
    }
#endif
}
