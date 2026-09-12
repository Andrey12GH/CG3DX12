#pragma once

#include <string>

namespace Shaders
{
    inline const std::string VertexShader = R"(
    cbuffer ObjectCB : register(b0)
    {
        matrix world;
        matrix view;
        matrix projection;
    };

    struct VSInput
    {
        float3 position : POSITION;
        float4 color : COLOR;
        float3 normal : NORMAL;
        float2 texcoord : TEXCOORD;
    };

    struct PSInput
    {
        float4 position : SV_POSITION;
        float4 color : COLOR;
        float3 normal : NORMAL;
        float2 texcoord : TEXCOORD;
    };

    PSInput main(VSInput input)
    {
        PSInput output;
        const float4 worldPosition = mul(float4(input.position, 1.0), world);
        output.position = mul(mul(worldPosition, view), projection);
        output.color = input.color;
        output.normal = mul(input.normal, (float3x3)world);
        output.texcoord = input.texcoord;
        return output;
    }
    )";

    inline const std::string PixelShader = R"(
    cbuffer LightCB : register(b1)
    {
        float3 lightDir;
        float padding;
        float4 ambientColor;
        float4 diffuseColor;
    };

    cbuffer MaterialCB : register(b2)
    {
        float4 materialColor;
        float2 uvScale;
        float2 uvOffset;
    };

    Texture2D diffuseTexture : register(t0);
    SamplerState textureSampler : register(s0);

    struct PSInput
    {
        float4 position : SV_POSITION;
        float4 color : COLOR;
        float3 normal : NORMAL;
        float2 texcoord : TEXCOORD;
    };

    float4 main(PSInput input) : SV_TARGET
    {
        const float2 animatedUV = input.texcoord * uvScale + uvOffset;
        const float4 texel = diffuseTexture.Sample(textureSampler, animatedUV);

        const float3 normal = normalize(input.normal);
        const float diffuseAmount = max(dot(normal, -lightDir), 0.0);
        const float4 lighting = ambientColor + diffuseColor * diffuseAmount;
        const float4 surface = texel * materialColor * input.color;

        return float4(surface.rgb * lighting.rgb, surface.a);
    }
    )";
}
