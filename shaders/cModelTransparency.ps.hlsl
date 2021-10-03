
#include "common.inc.hlsl"
#include "globalData.inc.hlsl"
#include "cModel.inc.hlsl"
#include "visibilityBuffer.inc.hlsl"

[[vk::binding(10, CMODEL)]] SamplerState _sampler;

struct PSInput
{
    float4 position : SV_Position;
    uint drawID : TEXCOORD0;
    float4 uv01 : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float depth : TEXCOORD3;
};

struct PSOutput
{
    float4 transparency : SV_Target0;
    float4 transparencyWeight : SV_Target1;
};

PSOutput main(PSInput input)
{
    CModelDrawCallData drawCallData = LoadCModelDrawCallData(input.drawID);

    float4 color = float4(0, 0, 0, 0);
    float3 specular = float3(0, 0, 0);

    for (uint textureUnitIndex = drawCallData.textureUnitOffset; textureUnitIndex < drawCallData.textureUnitOffset + drawCallData.numTextureUnits; textureUnitIndex++)
    {
        CModelTextureUnit textureUnit = _cModelTextureUnits[textureUnitIndex];

        uint isProjectedTexture = textureUnit.data1 & 0x1;
        uint materialFlags = (textureUnit.data1 >> 1) & 0x3FF;
        uint blendingMode = (textureUnit.data1 >> 11) & 0x7;
        
        uint materialType = (textureUnit.data1 >> 16) & 0xFFFF;
        uint vertexShaderId = materialType & 0xFF;
        uint pixelShaderId = materialType >> 8;
        
        if (materialType == 0x8000)
            continue;

        float4 texture1 = _cModelTextures[NonUniformResourceIndex(textureUnit.textureIDs[0])].Sample(_sampler, input.uv01.xy);
        float4 texture2 = float4(1, 1, 1, 1);

        if (vertexShaderId > 2)
        {
            // ENV uses generated UVCoords based on camera pos + geometry normal in frame space
            texture2 = _cModelTextures[NonUniformResourceIndex(textureUnit.textureIDs[1])].Sample(_sampler, input.uv01.zw);
        }

        float4 shadedColor = ShadeCModel(pixelShaderId, texture1, texture2, specular);
        color = BlendCModel(blendingMode, color, shadedColor);
    }

    bool isUnlit = drawCallData.numUnlitTextureUnits;

    color.rgb = Lighting(color.rgb, float3(0.0f, 0.0f, 0.0f), input.normal, 1.0f, !isUnlit) + specular;
    color = saturate(color);

    color.rgb *= color.a; // Premultiply it

    // Insert your favorite weighting function here. The color-based factor
    // avoids color pollution from the edges of wispy clouds. The z-based
    // factor gives precedence to nearer surfaces.
    //float z = -input.position.z;

    // This distance wants to be 0.1 < z < 500 according to the whitepaper, our nearclip is 1.0 and farclip is 100000.0f so we need to remap it
    float z = Map(input.depth, 1.0f, 100000.0f, 0.1f, 500.0f);
    float distWeight = clamp(0.03f / (1e-5f + pow(z / 200.0f, 4.0f)), 1e-2f, 3e3f);

    float alphaWeight = max(min(1.0f, max(max(color.r, color.g), color.b) * color.a), color.a);
    //alphaWeight *= alphaWeight;
    
    float weight = alphaWeight * distWeight;

    PSOutput output;
    output.transparency = color * weight;
    output.transparencyWeight.a = weight;

    return output;
}