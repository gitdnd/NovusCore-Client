permutation EDITOR_PASS = [0, 1];
#define GEOMETRY_PASS 1

#include "globalData.inc.hlsl"
#include "terrain.inc.hlsl"

struct VSInput
{
    uint vertexID : SV_VertexID;
    uint instanceID : SV_InstanceID;
};

struct VSOutput
{
    float4 position : SV_Position;
#if !EDITOR_PASS
    uint instanceID : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
#endif
};

VSOutput main(VSInput input)
{
    CellInstance cellInstance = _cellInstances[input.instanceID];

    VSOutput output;

    CellData cellData = LoadCellData(cellInstance.globalCellID);
    if (IsHoleVertex(input.vertexID, cellData.holes))
    {
        const float NaN = asfloat(0b01111111100000000000000000000000);
        output.position = float4(NaN, NaN, NaN, NaN);
        return output;
    }

    const uint cellID = cellInstance.packedChunkCellID & 0xFFFF;
    const uint chunkID = cellInstance.packedChunkCellID >> 16;

    uint vertexBaseOffset = cellInstance.globalCellID * NUM_VERTICES_PER_CELL;
    TerrainVertex vertex = LoadTerrainVertex(chunkID, cellID, vertexBaseOffset, input.vertexID);

    output.position = mul(float4(vertex.position, 1.0f), _viewData.viewProjectionMatrix);

#if !EDITOR_PASS
    output.instanceID = input.instanceID;
    output.worldPosition = vertex.position;
#endif

    return output;
}