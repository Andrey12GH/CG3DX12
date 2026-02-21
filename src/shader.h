#pragma once
#include <string>

namespace Shaders
{
    inline std::string VertexShader = R"(
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
    };

    struct PSInput
    {
        float4 position : SV_POSITION;
        float4 color : COLOR;
        float3 normal : NORMAL;
    };

    PSInput main(VSInput input)
    {
        PSInput output;
        float4 worldPos = mul(float4(input.position,1), world);
        output.position = mul(worldPos, view);
        output.position = mul(output.position, projection);
        output.color = input.color;
        output.normal = input.normal;
        return output;
    }
    )";

    inline std::string PixelShader = R"(
    cbuffer LightCB : register(b1)
    {
        float3 lightDir;
        float padding;
        float4 ambientColor;
        float4 diffuseColor;
    };

    struct PSInput
    {
        float4 position : SV_POSITION;
        float4 color : COLOR;
        float3 normal : NORMAL;
    };

    float4 main(PSInput input) : SV_TARGET
    {
        float3 n = normalize(input.normal);
        float NdotL = max(dot(n, -lightDir), 0.0);

        float4 baseColor = float4(0.8, 0.3, 0.3, 1.0);

        float4 finalColor = baseColor * (ambientColor + diffuseColor * NdotL);
        return finalColor;
    }
    )";
}