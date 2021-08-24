permutation EDITOR_PASS = [0, 1];
#define GEOMETRY_PASS 1

#include "globalData.inc.hlsl"
#include "mapObject.inc.hlsl"

struct VSInput
{
    uint vertexID : SV_VertexID;
    uint drawID : SV_InstanceID;
};

struct VSOutput
{
    float4 position : SV_Position;
#if !EDITOR_PASS
    nointerpolation uint drawID : TEXCOORD0;
    float3 modelPosition : TEXCOORD1;
    uint materialParamID : TEXCOORD2;
    float2 uv : TEXCOORD3;
#endif
};

VSOutput main(VSInput input)
{
    InstanceLookupData lookupData = LoadInstanceLookupData(input.drawID);
    
    InstanceData instanceData = _mapObjectInstanceData[lookupData.instanceID];
    MapObjectVertex vertex = LoadMapObjectVertex(input.vertexID, lookupData);

    float4 position = float4(vertex.position, 1.0f);
    position = mul(position, instanceData.instanceMatrix);

    VSOutput output;
    output.position = mul(position, _viewData.viewProjectionMatrix);

#if !EDITOR_PASS
    output.materialParamID = lookupData.materialParamID;
    output.uv = vertex.uv.xy;
    output.drawID = input.drawID;
    output.modelPosition = vertex.position.xyz;
#endif

    return output;
}