#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec4 worldTangent;
layout(location = 4) in vec4 vertexColor;
layout(location = 0) out vec4 outputColor;

struct GpuMaterial {
    vec4 baseColorFactor;
    vec4 emissiveFactor;
    vec4 metallicRoughnessNormal;
    uvec4 textureIndices0;
    uvec4 textureIndices1;
};

struct GpuLight {
    vec4 positionAndType;
    vec4 directionAndRange;
    vec4 colorAndIntensity;
    vec4 spotAngles;
};

layout(std140, set = 0, binding = 0) uniform LightBuffer {
    uvec4 lightHeader;
    GpuLight lights[128];
};

layout(std140, set = 0, binding = 1) uniform FrameBuffer {
    mat4 frameView;
    mat4 frameProjection;
    mat4 frameViewProjection;
    vec4 cameraPosition;
};

layout(std430, set = 0, binding = 2) readonly buffer MaterialBuffer {
    GpuMaterial materials[];
};

layout(set = 1, binding = 0) uniform sampler2D bindlessTextures[];

layout(push_constant) uniform DrawConstants {
    mat4 model;
    uint materialIndex;
    uvec3 padding;
} draw;

const float PI = 3.14159265359;

vec4 SampleBindlessTexture(uint textureIndex) {
    return texture(
        bindlessTextures[nonuniformEXT(textureIndex)], texCoord);
}

float DistributionGGX(float normalDotHalf, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float denominator = normalDotHalf * normalDotHalf *
        (alphaSquared - 1.0) + 1.0;
    return alphaSquared /
        max(PI * denominator * denominator, 0.000001);
}

float GeometrySchlickGGX(float normalDotView, float roughness) {
    float radius = roughness + 1.0;
    float factor = (radius * radius) / 8.0;
    return normalDotView /
        max(normalDotView * (1.0 - factor) + factor, 0.000001);
}

float GeometrySmith(
    float normalDotView,
    float normalDotLight,
    float roughness) {
    return GeometrySchlickGGX(normalDotView, roughness) *
        GeometrySchlickGGX(normalDotLight, roughness);
}

vec3 FresnelSchlick(float viewDotHalf, vec3 reflectanceAtNormal) {
    return reflectanceAtNormal + (1.0 - reflectanceAtNormal) *
        pow(1.0 - clamp(viewDotHalf, 0.0, 1.0), 5.0);
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

vec3 EvaluateDirectLighting(
    vec3 normal,
    vec3 viewDirection,
    vec3 baseColor,
    float metallic,
    float roughness,
    float ambientOcclusion) {
    vec3 reflectanceAtNormal = mix(vec3(0.04), baseColor, metallic);
    vec3 lighting = baseColor * (1.0 - metallic) *
        0.05 * ambientOcclusion;

    for (uint index = 0; index < lightHeader.x; ++index) {
        GpuLight light = lights[index];
        uint type = uint(light.positionAndType.w);
        vec3 toLight;
        float attenuation = 1.0;

        if (type == 0u) {
            toLight = normalize(-light.directionAndRange.xyz);
        } else {
            vec3 offset = light.positionAndType.xyz - worldPosition;
            float distanceToLight = length(offset);
            toLight = distanceToLight > 0.0
                ? offset / distanceToLight
                : vec3(0.0, 1.0, 0.0);
            float range = max(light.directionAndRange.w, 0.0001);
            float rangeFactor = clamp(
                1.0 - distanceToLight / range, 0.0, 1.0);
            attenuation = rangeFactor * rangeFactor /
                max(distanceToLight * distanceToLight, 1.0);

            if (type == 2u) {
                float coneCosine = dot(
                    normalize(light.directionAndRange.xyz), -toLight);
                attenuation *= smoothstep(
                    light.spotAngles.y, light.spotAngles.x, coneCosine);
            }
        }

        float normalDotLight = max(dot(normal, toLight), 0.0);
        if (normalDotLight <= 0.0) continue;

        vec3 halfVector = normalize(viewDirection + toLight);
        float normalDotView = max(dot(normal, viewDirection), 0.0001);
        float normalDotHalf = max(dot(normal, halfVector), 0.0);
        float viewDotHalf = max(dot(viewDirection, halfVector), 0.0);
        float distribution = DistributionGGX(normalDotHalf, roughness);
        float geometry = GeometrySmith(
            normalDotView, normalDotLight, roughness);
        vec3 fresnel = FresnelSchlick(
            viewDotHalf, reflectanceAtNormal);
        vec3 specular = distribution * geometry * fresnel /
            max(4.0 * normalDotView * normalDotLight, 0.0001);
        vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) *
            baseColor / PI;
        vec3 radiance = light.colorAndIntensity.rgb *
            light.colorAndIntensity.w * attenuation;
        lighting += (diffuse + specular) * radiance * normalDotLight;
    }
    return lighting;
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

void main() {
    GpuMaterial material = materials[draw.materialIndex];
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

    vec3 normal = EvaluateMaterialNormal(material);
    float filteredRoughness = CleanRoughness(normal, roughness);

    float ambientOcclusion = (flags & 8u) != 0u
        ? mix(1.0, SampleBindlessTexture(material.textureIndices0.w).r,
            clamp(material.metallicRoughnessNormal.w, 0.0, 1.0))
        : 1.0;
    // normal = normalize(worldNormal);
    vec3 viewDirection = normalize(cameraPosition.xyz - worldPosition);
    vec3 lighting = EvaluateDirectLighting(
        normal, viewDirection, baseColor.rgb,
        metallic, filteredRoughness, ambientOcclusion);
    vec3 sampledEmissive = (flags & 16u) != 0u
        ? SampleBindlessTexture(material.textureIndices1.x).rgb
        : vec3(1.0);
    outputColor = vec4(
        lighting + material.emissiveFactor.rgb * sampledEmissive,
        baseColor.a);
}
