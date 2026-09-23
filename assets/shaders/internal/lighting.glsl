struct GpuLight {
    vec4 positionAndType;
    vec4 directionAndRange;
    vec4 colorAndIntensity;
    vec4 spotAngles;
};

layout(std140, set = 0, binding = 0) uniform LightBuffer {
    uvec4 lightHeader;
    GpuLight lights[512];
};

const float PI = 3.14159265359;

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

vec3 EvaluateDirectLighting(
    vec3 worldPosition,
    vec3 normal,
    vec3 viewDirection,
    vec3 baseColor,
    float metallic,
    float roughness,
    float ambientOcclusion,
    vec3 reflectanceAtNormal,
    bool specularGlossiness,
    float transmission) {
    float diffuseWeight = specularGlossiness
        ? 1.0 - max(max(reflectanceAtNormal.r, reflectanceAtNormal.g), reflectanceAtNormal.b)
        : 1.0 - metallic;
    vec3 lighting = baseColor * diffuseWeight * (1.0 - transmission) *
        0.05 * ambientOcclusion;

    for (uint index = 0; index < min(lightHeader.x, 512u); ++index) {
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
        vec3 diffuse = (specularGlossiness ? vec3(1.0) : (1.0 - fresnel)) *
            diffuseWeight * (1.0 - transmission) * baseColor / PI;
        vec3 radiance = light.colorAndIntensity.rgb *
            light.colorAndIntensity.w * attenuation;
        lighting += (diffuse + specular) * radiance * normalDotLight;
    }
    return lighting;
}
