#ifndef TERRAIN_INC_INCLUDED
#define TERRAIN_INC_INCLUDED

#include "common.inc.hlsl"
#include "cullingUtils.inc.hlsl"

#define NUM_CHUNKS_PER_MAP_SIDE (64)
#define NUM_CELLS_PER_CHUNK_SIDE (16)
#define NUM_CELLS_PER_CHUNK (NUM_CELLS_PER_CHUNK_SIDE * NUM_CELLS_PER_CHUNK_SIDE)

#define NUM_INDICES_PER_CELL (768)
#define NUM_TRIANGLES_PER_CELL (NUM_INDICES_PER_CELL/3)
#define NUM_VERTICES_PER_CELL (145)

#define NUM_VERTICES_PER_OUTER_PATCH_ROW (9)
#define NUM_VERTICES_PER_INNER_PATCH_ROW (8)
#define NUM_VERTICES_PER_PATCH_ROW (NUM_VERTICES_PER_OUTER_PATCH_ROW + NUM_VERTICES_PER_INNER_PATCH_ROW)

#define CHUNK_SIDE_SIZE (533.33333f)
#define CELL_SIDE_SIZE (33.33333f)
#define PATCH_SIDE_SIZE (CELL_SIDE_SIZE / 8.0f)

#define MAP_SIZE (CHUNK_SIDE_SIZE * NUM_CHUNKS_PER_MAP_SIDE)
#define MAP_HALF_SIZE (MAP_SIZE / 2.0f)

struct PackedCellData
{
    uint packedDiffuseIDs1;
    uint packedDiffuseIDs2;
    uint packedHoles;
}; // 12 bytes

struct CellData
{
    uint4 diffuseIDs;
    uint holes;
};

struct ChunkData
{
    uint alphaID;
};

struct CellInstance
{
    uint packedChunkCellID;
    uint globalCellID;
};

uint GetGlobalCellID(uint chunkID, uint cellID)
{
    return (chunkID * NUM_CELLS_PER_CHUNK) + cellID;
}

float2 GetChunkPosition(uint chunkID)
{
    const uint chunkX = chunkID % NUM_CHUNKS_PER_MAP_SIDE;
    const uint chunkY = chunkID / NUM_CHUNKS_PER_MAP_SIDE;

    const float2 chunkPos = MAP_HALF_SIZE - (float2(chunkX, chunkY) * CHUNK_SIDE_SIZE);
    return -chunkPos;
}

float2 GetCellPosition(uint chunkID, uint cellID)
{
    const uint cellX = cellID % NUM_CELLS_PER_CHUNK_SIDE;
    const uint cellY = cellID / NUM_CELLS_PER_CHUNK_SIDE;

    const float2 chunkPos = GetChunkPosition(chunkID);
    const float2 cellPos = float2(cellX, cellY) * CELL_SIDE_SIZE;

    float2 pos = chunkPos + cellPos;
    return float2(-pos.y, -pos.x);
}

float2 GetGlobalVertexPosition(uint chunkID, uint cellID, uint vertexID)
{
    const int chunkX = chunkID % NUM_CHUNKS_PER_MAP_SIDE * NUM_CELLS_PER_CHUNK_SIDE;
    const int chunkY = chunkID / NUM_CHUNKS_PER_MAP_SIDE * NUM_CELLS_PER_CHUNK_SIDE;

    const int cellX = ((cellID % NUM_CELLS_PER_CHUNK_SIDE) + chunkX);
    const int cellY = ((cellID / NUM_CELLS_PER_CHUNK_SIDE) + chunkY);

    const int vX = vertexID % 17;
    const int vY = vertexID / 17;

    bool isOddRow = vX > 8.01;

    float2 vertexOffset;
    vertexOffset.x = -(8.5 * isOddRow);
    vertexOffset.y = (0.5 * isOddRow);

    int2 globalVertex = int2(vX + cellX * 8, vY + cellY * 8);

    float2 finalPos = -MAP_HALF_SIZE + (float2(globalVertex)+vertexOffset) * PATCH_SIDE_SIZE;

    return float2(-finalPos.y, -finalPos.x);
}

AABB GetCellAABB(uint chunkID, uint cellID, float2 heightRange)
{
    float2 pos = GetCellPosition(chunkID, cellID);
    float3 aabb_min = float3(pos.x, pos.y, heightRange.x);
    float3 aabb_max = float3(pos.x - CELL_SIDE_SIZE, pos.y - CELL_SIDE_SIZE, heightRange.y);

    AABB boundingBox;
    boundingBox.min = max(aabb_min, aabb_max);
    boundingBox.max = min(aabb_min, aabb_max);

    return boundingBox;
}

float2 GetCellSpaceVertexPosition(uint vertexID)
{
    float vertexX = vertexID % 17.0f;
    float vertexY = floor(vertexID / 17.0f);

    bool isOddRow = vertexX > 8.01f;
    vertexX = vertexX - (8.5f * isOddRow);
    vertexY = vertexY + (0.5f * isOddRow);

    // We go from a 2D coordinate system where x is Positive East & y is Positive South
    // we translate this into 3D where x is Positive North & y is Positive West
    return float2(-vertexY, -vertexX);
}

bool IsHoleVertex(uint vertexId, uint hole)
{
    if (hole == 0)
    {
        return false;
    }

    const uint blockRow = vertexId / 34;
    const uint blockVertexId = vertexId % 34;

    const uint shiftedHole = hole >> (blockRow * 4);

    if (shiftedHole & 0b0001)
    {
        if (blockVertexId == 9 || blockVertexId == 10 || blockVertexId == 26 || blockVertexId == 27)
        {
            return true;
        }
    }

    if (shiftedHole & 0b0010)
    {
        if (blockVertexId == 11 || blockVertexId == 12 || blockVertexId == 28 || blockVertexId == 29)
        {
            return true;
        }
    }

    if (shiftedHole & 0b0100)
    {
        if (blockVertexId == 13 || blockVertexId == 14 || blockVertexId == 30 || blockVertexId == 31)
        {
            return true;
        }
    }

    if (shiftedHole & 0b1000)
    {
        if (blockVertexId == 15 || blockVertexId == 16 || blockVertexId == 32 || blockVertexId == 33)
        {
            return true;
        }
    }

    return false;
}

[[vk::binding(0, TERRAIN)]] StructuredBuffer<PackedCellData> _packedCellData;
CellData LoadCellData(uint globalCellID)
{
    const PackedCellData rawCellData = _packedCellData[globalCellID];

    CellData cellData;

    // Unpack diffuse IDs
    cellData.diffuseIDs.x = (rawCellData.packedDiffuseIDs1 >> 0)  & 0xffff;
    cellData.diffuseIDs.y = (rawCellData.packedDiffuseIDs1 >> 16)  & 0xffff;
    cellData.diffuseIDs.z = (rawCellData.packedDiffuseIDs2 >> 0) & 0xffff;
    cellData.diffuseIDs.w = (rawCellData.packedDiffuseIDs2 >> 16) & 0xffff;

    // Unpack holes
    cellData.holes = rawCellData.packedHoles & 0xffff;

    return cellData;
}

uint3 GetLocalTerrainVertexIDs(uint triangleID)
{
    uint patchID = triangleID / 4;
    uint patchRow = patchID / 8;
    uint patchColumn = patchID % 8;

    uint patchVertexIDs[5];

    // Top Left is calculated like this
    patchVertexIDs[0] = patchColumn + (patchRow * (NUM_VERTICES_PER_PATCH_ROW));

    // Top Right is always +1 from Top Left
    patchVertexIDs[1] = patchVertexIDs[0] + 1;

    // Bottom Left is always NUM_VERTICES_PER_PATCH_ROW from the Top Left vertex
    patchVertexIDs[2] = patchVertexIDs[0] + NUM_VERTICES_PER_PATCH_ROW;

    // Bottom Right is always +1 from Bottom Left
    patchVertexIDs[3] = patchVertexIDs[2] + 1;

    // Center is always NUM_VERTICES_PER_OUTER_PATCH_ROW from Top Left
    patchVertexIDs[4] = patchVertexIDs[0] + NUM_VERTICES_PER_OUTER_PATCH_ROW;

    // Branchless fuckery, Pursche is confused so talk to Nix.
    // https://www.meme-arsenal.com/memes/81e0feae96e7eb1e08704c6d1be70ba8.jpg - Nix 2021
    uint triangleWithinPatch = triangleID % 4; // 0 - top, 1 - left, 2 - bottom, 3 - right
    uint2 triangleComponentOffsets = uint2(triangleWithinPatch > 1, // Identify if we are within bottom or right triangle
        triangleWithinPatch == 0 || triangleWithinPatch == 3); // Identify if we are within the top or right triangle
    uint3 vertexIDs;
    vertexIDs.x = patchVertexIDs[4];
    vertexIDs.y = patchVertexIDs[triangleComponentOffsets.x * 2 + triangleComponentOffsets.y];
    vertexIDs.z = patchVertexIDs[(!triangleComponentOffsets.y) * 2 + triangleComponentOffsets.x];

    return vertexIDs;
}

struct PackedTerrainVertex
{
    uint packed0;
    uint packed1;
}; // 8 bytes

struct TerrainVertex
{
    float3 position;
#if !GEOMETRY_PASS
    float3 normal;
    float3 color;
    float2 uv;
#endif
};

[[vk::binding(1, TERRAIN)]] StructuredBuffer<PackedTerrainVertex> _packedTerrainVertices;

float3 UnpackTerrainNormal(uint encoded)
{
    uint x = encoded & 0x000000FFu;
    uint y = (encoded >> 8u) & 0x000000FFu;
    uint z = (encoded >> 16u) & 0x000000FFu;

    float3 normal = (float3(x, y, z) / 127.0f) - 1.0f;
    return normalize(normal);
}

float3 UnpackColor(uint encoded)
{
    float r = (float)((encoded) & 0xFF);
    float g = (float)((encoded >> 8) & 0xFF);
    float b = (float)((encoded >> 16) & 0xFF);

    return float3(r, g, b) / 127.0f;
}

float UnpackHalf(uint encoded)
{
    return f16tof32(encoded);
}

TerrainVertex UnpackTerrainVertex(const PackedTerrainVertex packedVertex)
{
    // The vertex consists of 8 bytes of data, we split this into two uints called data0 and data1
    // data0 contains, in order:
    // u8 normal.x
    // u8 normal.y
    // u8 normal.z
    // u8 color.r
    //
    // data1 contains, in order:
    // u8 color.g
    // u8 color.b
    // half height, 2 bytes

    TerrainVertex vertex;

#if !GEOMETRY_PASS
    // Unpack normal and color
    uint normal = packedVertex.packed0;// & 0x00FFFFFFu;
    uint color = ((packedVertex.packed1 & 0x0000FFFFu) << 8u) | (packedVertex.packed0 >> 24u);

    vertex.normal = UnpackTerrainNormal(normal);
    vertex.color = UnpackColor(color);
#endif

    // Unpack height
    uint height = packedVertex.packed1 >> 16u;
    vertex.position.z = UnpackHalf(height);

    return vertex;
}

TerrainVertex LoadTerrainVertex(uint chunkID, uint cellID, uint vertexBaseOffset, uint vertexID)
{
    // Load height
    const uint vertexIndex = vertexBaseOffset + vertexID;
    const PackedTerrainVertex packedVertex = _packedTerrainVertices[vertexIndex];
    float2 vertexPos = GetCellSpaceVertexPosition(vertexID);

    TerrainVertex vertex = UnpackTerrainVertex(packedVertex);

    vertex.position.xy = GetGlobalVertexPosition(chunkID, cellID, vertexID);
#if !GEOMETRY_PASS
    vertex.uv = float2(-vertexPos.y, -vertexPos.x); // Negated to go from 3D coordinates to 2D
#endif

    return vertex;
}

[[vk::binding(2, TERRAIN)]] StructuredBuffer<CellInstance> _cellInstances;
[[vk::binding(3, TERRAIN)]] StructuredBuffer<ChunkData> _chunkData;

[[vk::binding(4, TERRAIN)]] SamplerState _alphaSampler;

[[vk::binding(5, TERRAIN)]] Texture2D<float4> _ambientOcclusion;

[[vk::binding(6, TERRAIN)]] RWTexture2D<float4> _resolvedColor;

[[vk::binding(7, TERRAIN)]] Texture2D<float4> _terrainColorTextures[4096];
[[vk::binding(8, TERRAIN)]] Texture2DArray<float4> _terrainAlphaTextures[NUM_CHUNKS_PER_MAP_SIDE * NUM_CHUNKS_PER_MAP_SIDE];

#endif // TERRAIN_INC_INCLUDED