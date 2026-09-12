#pragma once

#include <string>

namespace Shaders
{
    inline const std::string GeometryVertexShader = R"(
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
        float3 worldPosition : WORLDPOS;
        float4 color : COLOR;
        float3 worldNormal : NORMAL;
        float2 texcoord : TEXCOORD;
    };

    PSInput main(VSInput input)
    {
        PSInput output;
        const float4 worldPosition = mul(float4(input.position, 1.0), world);
        output.position = mul(mul(worldPosition, view), projection);
        output.worldPosition = worldPosition.xyz;
        output.color = input.color;
        output.worldNormal = mul(input.normal, (float3x3)world);
        output.texcoord = input.texcoord;
        return output;
    }
    )";

    inline const std::string GeometryPixelShader = R"(
    cbuffer MaterialCB : register(b1)
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
        float3 worldPosition : WORLDPOS;
        float4 color : COLOR;
        float3 worldNormal : NORMAL;
        float2 texcoord : TEXCOORD;
    };

    struct GBufferOutput
    {
        float4 albedoSpecular : SV_TARGET0;
        float4 normal : SV_TARGET1;
        float4 worldPosition : SV_TARGET2;
    };

    GBufferOutput main(PSInput input)
    {
        const float4 texel = diffuseTexture.Sample(
            textureSampler, input.texcoord * uvScale + uvOffset);
        const float4 surface = texel * materialColor * input.color;

        GBufferOutput output;
        output.albedoSpecular = float4(surface.rgb, 0.35);
        output.normal = float4(normalize(input.worldNormal), 1.0);
        output.worldPosition = float4(input.worldPosition, 1.0);
        return output;
    }
    )";

    inline const std::string LightingVertexShader = R"(
    struct PSInput
    {
        float4 position : SV_POSITION;
    };

    PSInput main(uint id : SV_VertexID)
    {
        const float2 uv = float2((id << 1) & 2, id & 2);
        PSInput output;
        output.position = float4(
            uv * float2(2.0, -2.0) + float2(-1.0, 1.0),
            0.0, 1.0);
        return output;
    }
    )";

    inline const std::string LightingPixelShader = R"(
    cbuffer LightingCB : register(b0)
    {
        float3 lightPosition;
        float lightRange;
        float3 lightDirection;
        float spotInnerCos;
        float3 lightColor;
        float intensity;
        float3 cameraPosition;
        float spotOuterCos;
        uint lightType;
        float ambientStrength;
        float2 padding;
    };

    Texture2D<float4> albedoSpecularTexture : register(t0);
    Texture2D<float4> normalTexture : register(t1);
    Texture2D<float4> worldPositionTexture : register(t2);

    struct PSInput
    {
        float4 position : SV_POSITION;
    };

    float4 main(PSInput input) : SV_TARGET
    {
        const int3 pixel = int3(int2(input.position.xy), 0);
        const float4 worldData = worldPositionTexture.Load(pixel);
        if (worldData.w < 0.5)
            return 0.0;

        const float4 albedoSpecular = albedoSpecularTexture.Load(pixel);
        const float3 normal = normalize(normalTexture.Load(pixel).xyz);
        const float3 worldPosition = worldData.xyz;

        float3 surfaceToLight;
        float attenuation = 1.0;

        if (lightType == 0)
        {
            surfaceToLight = normalize(-lightDirection);
        }
        else
        {
            const float3 delta = lightPosition - worldPosition;
            const float distanceToLight = length(delta);
            if (distanceToLight >= lightRange)
                return float4(albedoSpecular.rgb * ambientStrength, 0.0);

            surfaceToLight = delta / max(distanceToLight, 0.0001);
            attenuation = saturate(1.0 - distanceToLight / lightRange);
            attenuation *= attenuation;

            if (lightType == 2)
            {
                const float coneCosine = dot(-surfaceToLight, normalize(lightDirection));
                attenuation *= smoothstep(spotOuterCos, spotInnerCos, coneCosine);
            }
        }

        const float diffuseAmount = saturate(dot(normal, surfaceToLight));
        const float3 viewDirection = normalize(cameraPosition - worldPosition);
        const float3 halfVector = normalize(surfaceToLight + viewDirection);
        const float specularAmount = pow(saturate(dot(normal, halfVector)), 32.0)
            * albedoSpecular.a;

        const float3 direct =
            (albedoSpecular.rgb * diffuseAmount + specularAmount.xxx)
            * lightColor * intensity * attenuation;
        const float3 ambient = albedoSpecular.rgb * ambientStrength;
        return float4(direct + ambient, 0.0);
    }
    )";
}
