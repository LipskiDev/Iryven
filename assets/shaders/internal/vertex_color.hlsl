struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
};

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct DrawConstants
{
    column_major float4x4 model;
    float4 baseColor;
};

struct GpuLight
{
    float4 positionAndType; // xyz = position, w = type (0 = directional, 1 = point, 2 = spot)5
    float4 directionAndRange; // xyz = direction, w = range
    float4 colorAndIntensity; // xyz = color, w = intensity
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
    return normalize(determinantSign * (
        normal.x * cofactor0 + normal.y * cofactor1 + normal.z * cofactor2));
}

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    const float4 worldPosition = mul(Draw.model, float4(input.position, 1.0));
    output.position = mul(FrameViewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = TransformNormal(input.normal, Draw.model);
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    const float3 normal = normalize(input.worldNormal);
    float3 lighting = float3(0.05, 0.05, 0.05);

    for (uint i = 0; i < LightCount; ++i)
    {
        GpuLight light = Lights[i];
        const uint type = (uint)light.positionAndType.w;
        float3 toLight;
        float attenuation = 1.0;

        if (type == 0)
        {
            toLight = normalize(-light.directionAndRange.xyz);
        }
        else
        {
            const float3 offset = light.positionAndType.xyz - input.worldPosition;
            const float distanceToLight = length(offset);
            toLight = distanceToLight > 0.0 ? offset / distanceToLight : float3(0.0, 1.0, 0.0);
            const float range = max(light.directionAndRange.w, 0.0001);
            const float rangeFactor = saturate(1.0 - distanceToLight / range);
            attenuation = rangeFactor * rangeFactor / max(distanceToLight * distanceToLight, 1.0);

            if (type == 2)
            {
                const float coneCosine = dot(normalize(light.directionAndRange.xyz), -toLight);
                attenuation *= smoothstep(light.spotAngles.y, light.spotAngles.x, coneCosine);
            }
        }

        const float diffuse = saturate(dot(normal, toLight));
        lighting += light.colorAndIntensity.rgb * light.colorAndIntensity.w * diffuse * attenuation;
    }

    return float4(Draw.baseColor.rgb * lighting, Draw.baseColor.a);
}
