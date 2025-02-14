#include "globalData.inc.hlsl"
#include "terrain.inc.hlsl"

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

struct PackedVertex
{
    uint packed0; // u8 xCellOffset, u8 yCellOffset, f16 height
    uint packed1; // f16 xUV, f16 yUV
}; // 8 bytes

struct Vertex
{
    float3 position;
    float2 uv;
};

[[vk::binding(1, PER_PASS)]] StructuredBuffer<PackedVertex> _vertices;

Vertex LoadVertex(DrawCallData drawCallData, uint vertexID)
{
    PackedVertex packedVertex = _vertices[vertexID];

    float2 cellOffsetPos = float2(((packedVertex.packed0 >> 0) & 0xFF) * PATCH_SIDE_SIZE, ((packedVertex.packed0 >> 8) & 0xFF) * PATCH_SIDE_SIZE);

    Vertex vertex;
    
    float2 cellBasePos = GetCellPosition(drawCallData.chunkID, drawCallData.cellID);
    vertex.position.xy = cellBasePos - cellOffsetPos;
    vertex.position.z = f16tof32(packedVertex.packed0 >> 16);

    vertex.uv.x = f16tof32(packedVertex.packed1 >> 0);
    vertex.uv.y = f16tof32(packedVertex.packed1 >> 16);

    return vertex;
}

struct VSInput
{
    uint vertexID : SV_VertexID;
    uint instanceID : SV_InstanceID;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 textureUV : TEXCOORD0;
    uint drawCallID : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    DrawCallData drawCallData = LoadDrawCallData(input.instanceID);
    Vertex vertex = LoadVertex(drawCallData, input.vertexID);
    float2 uv = vertex.uv.xy;

    output.position = mul(float4(vertex.position.xyz, 1.0f), _viewData.viewProjectionMatrix);
    output.textureUV = uv;
    output.drawCallID = input.instanceID;

    return output;
}