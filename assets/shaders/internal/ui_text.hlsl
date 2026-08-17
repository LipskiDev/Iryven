struct VertexInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 0.0f, 1.0f);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

Texture2D fontAtlas : register(t0, space0);
SamplerState fontSampler : register(s0, space0);

static const float pxRange = 4.0f;

float2 Sqr(float2 x)
{
    return x * x;
}

float ScreenPxRange(float2 texCoord)
{
    uint width;
    uint height;
    fontAtlas.GetDimensions(width, height);

    float2 unitRange =
        float2(pxRange, pxRange) / float2(width, height);

    float2 screenTexSize =
        1.0f / sqrt(Sqr(ddx(texCoord)) + Sqr(ddy(texCoord)));

    return max(
        0.5f * dot(unitRange, screenTexSize),
        1.0f
    );
}

float Median(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    float3 msd =
        fontAtlas.Sample(fontSampler, input.uv).rgb;

    float signedDistance =
        Median(msd.r, msd.g, msd.b);

    float screenDistance =
        ScreenPxRange(input.uv) *
        (signedDistance - 0.5f);

    float opacity =
        saturate(screenDistance + 0.5f);

    return float4(
        input.color.rgb,
        input.color.a * opacity
    );
}