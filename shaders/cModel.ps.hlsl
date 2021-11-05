#define GEOMETRY_PASS 1

#include "common.inc.hlsl"
#include "globalData.inc.hlsl"
#include "cModel.inc.hlsl"
#include "visibilityBuffer.inc.hlsl"

[[vk::binding(10, CMODEL)]] SamplerState _sampler;

struct PSInput
{
    uint triangleID : SV_PrimitiveID;
    uint drawID : TEXCOORD0;
    float3 modelPosition : TEXCOORD1;
    float4 uv01 : TEXCOORD2;
};

struct PSOutput
{
    uint4 visibilityBuffer : SV_Target0;
};

PSOutput main(PSInput input)
{
    CModelDrawCallData drawCallData = LoadCModelDrawCallData(input.drawID);

    for (uint textureUnitIndex = drawCallData.textureUnitOffset; textureUnitIndex < drawCallData.textureUnitOffset + drawCallData.numTextureUnits; textureUnitIndex++)
    {
        CModelTextureUnit textureUnit = _cModelTextureUnits[textureUnitIndex];

        uint blendingMode = (textureUnit.data1 >> 11) & 0x7;

        if (blendingMode != 1) // ALPHA KEY
            continue;
        
        uint materialType = (textureUnit.data1 >> 16) & 0xFFFF;
        
        if (materialType == 0x8000)
            continue;

        float4 texture1 = _cModelTextures[NonUniformResourceIndex(textureUnit.textureIDs[0])].Sample(_sampler, input.uv01.xy);
        float4 texture2 = float4(1, 1, 1, 1);

        uint vertexShaderId = materialType & 0xFF;
        if (vertexShaderId > 2)
        {
            // ENV uses generated UVCoords based on camera pos + geometry normal in frame space
            texture2 = _cModelTextures[NonUniformResourceIndex(textureUnit.textureIDs[1])].Sample(_sampler, input.uv01.zw);
        }

        // Experimental alphakey discard without shading, if this has issues check github history for cModel.ps.hlsl
        float4 diffuseColor = float4(1, 1, 1, 1);
        // TODO: per-instance diffuseColor

        float minAlpha = min(texture1.a, min(texture2.a, diffuseColor.a));
        if (minAlpha < 224.0f / 255.0f)
        {
            discard;
        }
    }

    CModelInstanceData instanceData = _cModelInstanceDatas[drawCallData.instanceID];
    float4x4 instanceMatrix = _cModelInstanceMatrices[drawCallData.instanceID];

    // Get the VertexIDs of the triangle we're in
    Draw draw = _cModelDraws[input.drawID];
    uint3 vertexIDs = GetVertexIDs(input.triangleID, draw, _cModelIndices);

    // Load the vertices
    CModelVertex vertices[3];

    [unroll]
    for (uint i = 0; i < 3; i++)
    {
        vertices[i] = LoadCModelVertex(vertexIDs[i]);

        // Load the skinned vertex position (in model-space) if this vertex was animated
        if (instanceData.boneDeformOffset != 4294967295)
        {
            uint localVertexID = vertexIDs[i] - instanceData.modelVertexOffset; // This gets the local vertex ID relative to the model
            uint animatedVertexID = localVertexID + instanceData.animatedVertexOffset; // This makes it relative to the animated instance

            vertices[i].position = LoadAnimatedVertexPosition(animatedVertexID);
        }

        // TODO: Calculating this bone transform matrix is rather expensive, we should do this once in the vertex shader and save the result
        //float4x4 boneTransformMatrix = CalcBoneTransformMatrix(instanceData, vertices[i]);

        //vertices[i].position = mul(float4(vertices[i].position, 1.0f), boneTransformMatrix).xyz;
        vertices[i].position = mul(float4(vertices[i].position, 1.0f), instanceMatrix).xyz;
    }

    // Calculate Barycentrics
    float2 barycentrics = NBLCalculateBarycentrics(input.modelPosition, float3x3(vertices[0].position.xyz, vertices[1].position.xyz, vertices[2].position.xyz));

    float2 ddxBarycentrics = ddx(barycentrics);
    float2 ddyBarycentrics = ddy(barycentrics);

    PSOutput output;
    output.visibilityBuffer = PackVisibilityBuffer(ObjectType::CModelOpaque, input.drawID, input.triangleID, barycentrics, ddxBarycentrics, ddyBarycentrics);

    return output;
}