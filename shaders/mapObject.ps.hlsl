#define GEOMETRY_PASS 1

#include "common.inc.hlsl"
#include "globalData.inc.hlsl"
#include "mapObject.inc.hlsl"
#include "visibilityBuffer.inc.hlsl"

[[vk::binding(7, MAPOBJECT)]] SamplerState _sampler;

struct PSInput
{
    uint triangleID : SV_PrimitiveID;
    uint drawID : TEXCOORD0;
    float3 modelPosition : TEXCOORD1;
    uint materialParamID : TEXCOORD2;
    float2 uv : TEXCOORD3;
};

struct PSOutput
{
    uint4 visibilityBuffer : SV_Target0;
};

PSOutput main(PSInput input)
{
    MapObjectMaterialParam materialParam = LoadMapObjectMaterialParam(input.materialParamID);
    MapObjectMaterial material = LoadMapObjectMaterial(materialParam.materialID);
    
    // If I do this instead, it works
    uint textureID0 = material.textureIDs[0]; // Prevents invalid patching of shader when running GPU validation layers, maybe remove in future
    float4 tex0 = _mapObjectTextures[NonUniformResourceIndex(textureID0)].Sample(_sampler, input.uv);

    float alphaTestVal = f16tof32(material.alphaTestVal);
    if (tex0.a < alphaTestVal)
    {
        discard;
    }

    InstanceLookupData lookupData = LoadInstanceLookupData(input.drawID);

    // Find the vertex ids
    Draw draw = _mapObjectDraws[input.drawID];
    uint3 vertexIDs = GetVertexIDs(input.triangleID, draw, _mapObjectIndices);

    // Load the vertices
    MapObjectVertex vertices[3];

    [unroll]
    for (uint i = 0; i < 3; i++)
    {
        vertices[i] = LoadMapObjectVertex(vertexIDs[i], lookupData);
    }

    // Calculate Barycentrics
    float2 barycentrics = NBLCalculateBarycentrics(input.modelPosition, float3x3(vertices[0].position.xyz, vertices[1].position.xyz, vertices[2].position.xyz));

    float2 ddxBarycentrics = ddx(barycentrics);
    float2 ddyBarycentrics = ddy(barycentrics);

    PSOutput output;
    output.visibilityBuffer = PackVisibilityBuffer(ObjectType::MapObject, input.drawID, input.triangleID, barycentrics, ddxBarycentrics, ddyBarycentrics);
    return output;
}