#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_KHR_shader_subgroup_ballot : require

#include "light_buffer.glsl"
#include "light_tiles.glsl"
#include "light_depth_bins.glsl"

#ifndef IRYVEN_POINT_SHADOW
#define IRYVEN_POINT_SHADOW(light, worldPosition, normal) 1.0
#endif

const float PI = 3.14159265359;

float DistributionGGX(float normalDotHalf, float alphaSquared) {
    float denominator = normalDotHalf * normalDotHalf *
        (alphaSquared - 1.0) + 1.0;
    return alphaSquared /
        max(PI * denominator * denominator, 0.000001);
}

float GeometrySchlickGGX(float normalDotView, float factor) {
    return normalDotView /
        max(normalDotView * (1.0 - factor) + factor, 0.000001);
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
    float transmission,
	bool countLights,
	bool showShadowTiers) {
    float diffuseWeight = specularGlossiness
        ? 1.0 - max(max(reflectanceAtNormal.r, reflectanceAtNormal.g), reflectanceAtNormal.b)
        : 1.0 - metallic;
    vec3 lighting = showShadowTiers ? vec3(0.0) :
		baseColor * diffuseWeight * (1.0 - transmission) *
		0.05 * ambientOcclusion;

    // Material and view terms are constant across all lights for this fragment.
    float normalDotView = max(dot(normal, viewDirection), 0.0001);
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float radius = roughness + 1.0;
    float geometryFactor = (radius * radius) / 8.0;
    float viewGeometry = GeometrySchlickGGX(normalDotView, geometryFactor);
    vec3 diffuseBase = diffuseWeight * (1.0 - transmission) * baseColor / PI;

    uvec2 localRange = lightHeader.w == 0u ? uvec2(0u)
        : LocalLightDepthRange(worldPosition, lightHeader.w);
    uvec2 tile = uvec2(gl_FragCoord.xy) / LIGHT_TILE_SIZE;
    uint tileOffset = (tile.y * lightTileHeader.x + tile.x) * LIGHT_TILE_WORDS;
    uint word = localRange.x / 32u;
    uint endWord = localRange.x < localRange.y ? (localRange.y + 31u) / 32u : word;
    uint affectingLights = 0u;
	float dominantLightImpact = -1.0;
	uint dominantShadowTier = 4u;
    uint bits = 0u, laneMask = 0u, wordBase = 0u, directional = 0u;
    for (;;) {
        uint index;
        bool selected = true;
        if (directional < lightHeader.y) {
            index = directional++;
        } else {
            while (bits == 0u && word < endWord) {
                wordBase = word * 32u;
                uint first = max(localRange.x, wordBase);
                uint end = min(localRange.y, wordBase + 32u);
                laneMask = 0u;
                if (first < end && !gl_HelperInvocation) {
                    laneMask = lightTileMasks[tileOffset + word] &
                        (0xffffffffu << (first - wordBase)) & (0xffffffffu >> (32u - (end - wordBase)));
                }

                bits = laneMask;
                ++word;
            }
            if (bits == 0u) break;
            index = lightHeader.z + wordBase + uint(findLSB(bits));
            selected = (laneMask & (bits & (0u - bits))) != 0u;
            bits &= bits - 1u;
        }
        GpuLight light = lights[index];
        if (!selected) continue;
        if (countLights) {
            if (any(greaterThan(light.colorAndIntensity.rgb * light.colorAndIntensity.w, vec3(0.0))))
                ++affectingLights;
            continue;
        }
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
                max(distanceToLight * distanceToLight, 1e-4);

            if (type == 2u) {
                float coneCosine = dot(
                    normalize(light.directionAndRange.xyz), -toLight);
                attenuation *= smoothstep(
                    light.spotAngles.y, light.spotAngles.x, coneCosine);
            }
        }

        if (attenuation <= 0.0) continue;

        float normalDotLight = max(dot(normal, toLight), 0.0);
        if (normalDotLight <= 0.0) continue;

        

        vec3 halfVector = normalize(viewDirection + toLight);
        float normalDotHalf = max(dot(normal, halfVector), 0.0);
        float viewDotHalf = max(dot(viewDirection, halfVector), 0.0);
        float distribution = DistributionGGX(normalDotHalf, alphaSquared);
        float geometry = viewGeometry * GeometrySchlickGGX(normalDotLight, geometryFactor);
        vec3 fresnel = FresnelSchlick(
            viewDotHalf, reflectanceAtNormal);
        vec3 specular = distribution * geometry * fresnel /
            max(4.0 * normalDotView * normalDotLight, 0.0001);
        vec3 diffuse = (specularGlossiness ? vec3(1.0) : (1.0 - fresnel)) *
            diffuseBase;
        vec3 radiance = light.colorAndIntensity.rgb *
            light.colorAndIntensity.w * attenuation;
        float visibility = type == 1u
            ? IRYVEN_POINT_SHADOW(light, worldPosition, normal)
            : 1.0;
		vec3 contribution = (diffuse + specular) * radiance *
			normalDotLight * visibility;
		if (showShadowTiers) {
			float impact = dot(contribution, vec3(0.2126, 0.7152, 0.0722));
			if (impact > dominantLightImpact) {
				dominantLightImpact = impact;
				dominantShadowTier = type == 1u && light.shadowInfo.x != 0u
					? light.shadowInfo.y : 3u;
			}
		}
		lighting += contribution;
    }
    // Debug view: color code the number of lights affecting this fragment.
    if (countLights) {
        // Fixed bands: zero, 1-4, 5-8, 9-16, 17-32, 33-64, 65+.
        if (affectingLights == 0u) return vec3(0.0);
        if (affectingLights <= 4u) return vec3(0.0, 0.0, 1.0);
        if (affectingLights <= 8u) return vec3(0.0, 1.0, 1.0);
        if (affectingLights <= 16u) return vec3(0.0, 1.0, 0.0);
        if (affectingLights <= 32u) return vec3(1.0, 1.0, 0.0);
        if (affectingLights <= 64u) return vec3(1.0, 0.0, 0.0);
        return vec3(1.0, 0.0, 1.0);
    }
	if (showShadowTiers) {
		if (dominantShadowTier == 0u) return vec3(1.0, 0.2, 0.15);
		if (dominantShadowTier == 1u) return vec3(1.0, 0.8, 0.1);
		if (dominantShadowTier == 2u) return vec3(0.15, 0.45, 1.0);
		if (dominantShadowTier == 3u) return vec3(0.65, 0.25, 1.0);
		return vec3(0.0);
	}
    return lighting;
}
