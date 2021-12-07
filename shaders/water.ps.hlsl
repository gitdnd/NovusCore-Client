#include "common.inc.hlsl"
#include "globalData.inc.hlsl"
#include "orderIndependentTransparencyUtils.inc.hlsl"

struct Constants
{
    float4 shallowOceanColor;
    float4 deepOceanColor;
    float4 shallowRiverColor;
    float4 deepRiverColor;
    float waterVisibilityRange;
    float currentTime;
};

[[vk::push_constant]] Constants _constants;

struct PackedDrawCallData
{
    uint packed0; // u16 chunkID, u16 cellID
    uint packed1; // u16 textureStartIndex, u8 textureCount, u8 hasDepth
};

struct DrawCallData
{
    uint chunkID;
    uint cellID;
    uint textureStartIndex;
    uint textureCount;
    uint hasDepth;
};

[[vk::binding(0, PER_PASS)]] StructuredBuffer<PackedDrawCallData> _drawCallDatas;

DrawCallData LoadDrawCallData(uint drawCallID)
{
    PackedDrawCallData packedDrawCallData = _drawCallDatas[drawCallID];

    DrawCallData drawCallData;
    drawCallData.chunkID = ((packedDrawCallData.packed0 >> 0) & 0xFFFF);
    drawCallData.cellID = ((packedDrawCallData.packed0 >> 16) & 0xFFFF);
    drawCallData.textureStartIndex = ((packedDrawCallData.packed1 >> 0) & 0xFFFF);
    drawCallData.textureCount = ((packedDrawCallData.packed1 >> 16) & 0xFF);
    drawCallData.hasDepth = ((packedDrawCallData.packed1 >> 24) & 0xFF);

    return drawCallData;
}

[[vk::binding(2, PER_PASS)]] SamplerState _sampler;
[[vk::binding(3, PER_PASS)]] Texture2D<float> _depthRT;
[[vk::binding(4, PER_PASS)]] Texture2D<float4> _textures[1024];

struct PSInput
{
    float4 pixelPos : SV_Position;
    float2 textureUV : TEXCOORD0;
    uint drawCallID : TEXCOORD1;
};

struct PSOutput
{
    float4 transparency : SV_Target0;
    float4 transparencyWeight : SV_Target1;
};

PSOutput main(PSInput input)
{
    DrawCallData drawCallData = LoadDrawCallData(input.drawCallID);

    // We need to get the depth of the opaque pixel "under" this water pixel
    float2 dimensions;
    _depthRT.GetDimensions(dimensions.x, dimensions.y);

    float2 pixelUV = input.pixelPos.xy / dimensions;
    float opaqueDepth = _depthRT.Sample(_sampler, pixelUV); // 0.0 .. 1.0

    float waterDepth = input.pixelPos.z / input.pixelPos.w;

    float linearDepthDifference = LinearizeDepth((1.0f - opaqueDepth), 0.1f, 100000.0f) - LinearizeDepth((1.0f - (waterDepth)), 0.1f, 100000.0f);
    float blendFactor = clamp(linearDepthDifference, 0.0f, _constants.waterVisibilityRange) / _constants.waterVisibilityRange;

    // Blend color
    float4 color = lerp(_constants.shallowRiverColor, _constants.deepRiverColor, blendFactor);

    uint textureAnimationOffset = _constants.currentTime % drawCallData.textureCount;
    float4 texture0 = _textures[drawCallData.textureStartIndex + textureAnimationOffset].Sample(_sampler, input.textureUV);

    color.rgb = saturate(color.rgb + texture0.rgb);
    //color.a *= texture0.a;

    // Calculate OIT weight and output
    float oitWeight = CalculateOITWeight(color, waterDepth);

    PSOutput output;
    output.transparency = float4(color.rgb /** color.a*/, color.a) * oitWeight;
    output.transparencyWeight = color.aaaa;
    return output;
}