// Baseline glTF material shader. Keep the vertex interface stable while the
// material inputs below grow into metallic-roughness PBR and texture sampling.

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 texCoord : TEXCOORD0;
    [[vk::location(3)]] float4 tangent : TANGENT;
    [[vk::location(4)]] float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 worldPosition : TEXCOORD0;
    [[vk::location(1)]] float3 worldNormal : TEXCOORD1;
    [[vk::location(2)]] float2 texCoord : TEXCOORD2;
    [[vk::location(3)]] float4 worldTangent : TEXCOORD3;
    [[vk::location(4)]] float4 color : COLOR0;
};

struct DrawConstants
{
    column_major float4x4 model;
    uint materialIndex;
    uint3 padding;
};

struct GpuMaterial
{
    float4 baseColorFactor;
    float4 emissiveFactor;
    float4 metallicRoughnessNormal;
    uint4 flagsAndTextures;
};

struct GpuLight
{
    float4 positionAndType;
    float4 directionAndRange;
    float4 colorAndIntensity;
    float4 spotAngles;
};

cbuffer LightBuffer : register(b0, space0)
{
    uint LightCount;
    uint3 LightPadding;
    GpuLight Lights[128];
};

cbuffer FrameBuffer : register(b1, space0)
{
    column_major float4x4 FrameView;
    column_major float4x4 FrameProjection;
    column_major float4x4 FrameViewProjection;
    float4 CameraPosition;
};

StructuredBuffer<GpuMaterial> Materials : register(t2, space0);
Texture2D BaseColorTexture : register(t0, space1);
SamplerState BaseColorSampler : register(s0, space1);
Texture2D MetallicRoughnessTexture : register(t1, space1);
SamplerState MetallicRoughnessSampler : register(s1, space1);
Texture2D NormalTexture : register(t2, space1);
SamplerState NormalSampler : register(s2, space1);
Texture2D OcclusionTexture : register(t3, space1);
SamplerState OcclusionSampler : register(s3, space1);
Texture2D EmissiveTexture : register(t4, space1);
SamplerState EmissiveSampler : register(s4, space1);

[[vk::push_constant]] ConstantBuffer<DrawConstants> Draw;

float3 TransformNormal(float3 normal, float4x4 model)
{
    const float3 column0 = float3(model[0][0], model[1][0], model[2][0]);
    const float3 column1 = float3(model[0][1], model[1][1], model[2][1]);
    const float3 column2 = float3(model[0][2], model[1][2], model[2][2]);
    const float3 cofactor0 = cross(column1, column2);
    const float3 cofactor1 = cross(column2, column0);
    const float3 cofactor2 = cross(column0, column1);
    const float determinant = dot(column0, cofactor0);
    const float determinantSign = determinant < 0.0 ? -1.0 : 1.0;
    return normalize(determinantSign *
        (normal.x * cofactor0 + normal.y * cofactor1 + normal.z * cofactor2));
}

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    const float4 worldPosition = mul(Draw.model, float4(input.position, 1.0));
    output.position = mul(FrameViewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = TransformNormal(input.normal, Draw.model);
    output.texCoord = input.texCoord;
    const float3 transformedTangent = mul((float3x3)Draw.model, input.tangent.xyz);
    output.worldTangent = float4(
        dot(transformedTangent, transformedTangent) > 0.0
            ? normalize(transformedTangent)
            : float3(1.0, 0.0, 0.0),
        input.tangent.w);
    output.color = input.color;
    return output;
}

static const float PI = 3.14159265359;

float DistributionGGX(float NdotH, float roughness)
{
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float denominator =
        NdotH * NdotH * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(PI * denominator * denominator, 0.000001);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    const float r = roughness + 1.0;
    const float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 0.000001);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    return GeometrySchlickGGX(NdotV, roughness) *
        GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float VdotH, float3 reflectanceAtNormal)
{
    return reflectanceAtNormal + (1.0 - reflectanceAtNormal) *
        pow(1.0 - saturate(VdotH), 5.0);
}

float3 EvaluateDirectLighting(
    float3 worldPosition,
    float3 normal,
    float3 viewDirection,
    float3 baseColor,
    float metallic,
    float roughness,
    float ambientOcclusion)
{
    const float3 dielectricReflectance = float3(0.04, 0.04, 0.04);
    const float3 reflectanceAtNormal = lerp(
        dielectricReflectance, baseColor, metallic);
    float3 lighting = baseColor * (1.0 - metallic) * 0.05 * ambientOcclusion;
    for (uint i = 0; i < LightCount; ++i)
    {
        const GpuLight light = Lights[i];
        const uint type = (uint)light.positionAndType.w;
        float3 toLight;
        float attenuation = 1.0;

        if (type == 0)
        {
            toLight = normalize(-light.directionAndRange.xyz);
        }
        else
        {
            const float3 offset = light.positionAndType.xyz - worldPosition;
            const float distanceToLight = length(offset);
            toLight = distanceToLight > 0.0
                ? offset / distanceToLight
                : float3(0.0, 1.0, 0.0);
            const float range = max(light.directionAndRange.w, 0.0001);
            const float rangeFactor = saturate(1.0 - distanceToLight / range);
            attenuation = rangeFactor * rangeFactor /
                max(distanceToLight * distanceToLight, 1.0);

            if (type == 2)
            {
                const float coneCosine =
                    dot(normalize(light.directionAndRange.xyz), -toLight);
                attenuation *= smoothstep(
                    light.spotAngles.y, light.spotAngles.x, coneCosine);
            }
        }

        const float NdotL = saturate(dot(normal, toLight));
        if (NdotL <= 0.0)
            continue;

        const float3 halfVector = normalize(viewDirection + toLight);
        const float NdotV = max(saturate(dot(normal, viewDirection)), 0.0001);
        const float NdotH = saturate(dot(normal, halfVector));
        const float VdotH = saturate(dot(viewDirection, halfVector));

        const float distribution = DistributionGGX(NdotH, roughness);
        const float geometry = GeometrySmith(NdotV, NdotL, roughness);
        const float3 fresnel = FresnelSchlick(VdotH, reflectanceAtNormal);
        const float3 specular = distribution * geometry * fresnel /
            max(4.0 * NdotV * NdotL, 0.0001);

        const float3 diffuseWeight = (1.0 - fresnel) * (1.0 - metallic);
        const float3 diffuse = diffuseWeight * baseColor / PI;
        const float3 radiance = light.colorAndIntensity.rgb *
            light.colorAndIntensity.w * attenuation;
        lighting += (diffuse + specular) * radiance * NdotL;
    }
    return lighting;
}

float3 EvaluateMaterialNormal(VertexOutput input, GpuMaterial material)
{
    const float3 geometricNormal = normalize(input.worldNormal);
    if ((material.flagsAndTextures.z & 1u) == 0u)
        return geometricNormal;

    float3 tangent;
    float3 bitangent;
    if (dot(input.worldTangent.xyz, input.worldTangent.xyz) > 0.000001)
    {
        tangent = normalize(input.worldTangent.xyz - geometricNormal *
            dot(geometricNormal, input.worldTangent.xyz));
        const float handedness = input.worldTangent.w < 0.0 ? -1.0 : 1.0;
        bitangent = normalize(cross(geometricNormal, tangent)) * handedness;
    }
    else
    {
        const float3 positionDx = ddx(input.worldPosition);
        const float3 positionDy = ddy(input.worldPosition);
        const float2 uvDx = ddx(input.texCoord);
        const float2 uvDy = ddy(input.texCoord);
        const float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
        if (abs(determinant) < 0.000001)
            return geometricNormal;
        tangent = normalize((positionDx * uvDy.y - positionDy * uvDx.y) /
            determinant);
        tangent = normalize(tangent - geometricNormal * dot(geometricNormal, tangent));
        bitangent = normalize(cross(geometricNormal, tangent)) *
            (determinant < 0.0 ? -1.0 : 1.0);
    }

    float3 tangentNormal = NormalTexture.Sample(NormalSampler, input.texCoord).xyz *
        2.0 - 1.0;
    tangentNormal.xy *= material.metallicRoughnessNormal.z;
    tangentNormal = normalize(tangentNormal);
    return normalize(
        tangent * tangentNormal.x +
        bitangent * tangentNormal.y +
        geometricNormal * tangentNormal.z);
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    const GpuMaterial material = Materials[Draw.materialIndex];
    const float4 sampledBaseColor = material.flagsAndTextures.x != 0
        ? BaseColorTexture.Sample(BaseColorSampler, input.texCoord)
        : float4(1.0, 1.0, 1.0, 1.0);
    const float4 baseColor = material.baseColorFactor * input.color * sampledBaseColor;
    const float4 sampledMetallicRoughness = material.flagsAndTextures.y != 0
        ? MetallicRoughnessTexture.Sample(MetallicRoughnessSampler, input.texCoord)
        : float4(1.0, 1.0, 1.0, 1.0);
    const float metallic = saturate(
        material.metallicRoughnessNormal.x * sampledMetallicRoughness.b);
    // glTF permits zero roughness, but GGX needs a small numerical floor.
    const float roughness = clamp(
        material.metallicRoughnessNormal.y * sampledMetallicRoughness.g,
        0.045,
        1.0);
    const float ambientOcclusion = (material.flagsAndTextures.z & 2u) != 0u
        ? lerp(
            1.0,
            OcclusionTexture.Sample(OcclusionSampler, input.texCoord).r,
            saturate(material.metallicRoughnessNormal.w))
        : 1.0;
    const float3 normal = EvaluateMaterialNormal(input, material);
    const float3 viewDirection = normalize(
        CameraPosition.xyz - input.worldPosition);
    const float3 lighting = EvaluateDirectLighting(
        input.worldPosition,
        normal,
        viewDirection,
        baseColor.rgb,
        metallic,
        roughness,
        ambientOcclusion);
    const float3 sampledEmissive = (material.flagsAndTextures.z & 4u) != 0u
        ? EmissiveTexture.Sample(EmissiveSampler, input.texCoord).rgb
        : float3(1.0, 1.0, 1.0);
    const float3 emissive = material.emissiveFactor.rgb * sampledEmissive;
    return float4(lighting + emissive, baseColor.a);
}
