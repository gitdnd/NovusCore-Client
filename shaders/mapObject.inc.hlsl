#ifndef MAPOBJECT_INC_INCLUDED
#define MAPOBJECT_INC_INCLUDED
#include "common.inc.hlsl"

struct PackedInstanceLookupData
{
    uint packed0; // uint16_t instanceID, uint16_t materialParamID
    uint packed1; // uint16_t cullingDataID, uint16_t vertexColorTextureID0
    uint packed2; // uint16_t vertexColorTextureID1, uint16_t padding
    uint vertexOffset;
    uint vertexColor0Offset;
    uint vertexColor1Offset;
    uint loadedObjectID;
}; // 28 bytes

struct InstanceLookupData
{
    uint instanceID;
    uint materialParamID;
    uint cullingDataID;
    uint vertexColorTextureID0;
    uint vertexColorTextureID1;
    uint vertexOffset;
    uint vertexColor0Offset;
    uint vertexColor1Offset;
    uint loadedObjectID;
};

[[vk::binding(0, MAPOBJECT)]] StructuredBuffer<PackedInstanceLookupData> _packedInstanceLookup;

InstanceLookupData LoadInstanceLookupData(uint instanceLookupDataID)
{
    PackedInstanceLookupData packedInstanceLookupData = _packedInstanceLookup[instanceLookupDataID];
    
    InstanceLookupData instanceLookupData;
    
    instanceLookupData.instanceID = packedInstanceLookupData.packed0 & 0xFFFF;
    instanceLookupData.materialParamID = (packedInstanceLookupData.packed0 >> 16) & 0xFFFF;
    instanceLookupData.cullingDataID = packedInstanceLookupData.packed1 & 0xFFFF;
    instanceLookupData.vertexColorTextureID0 = (packedInstanceLookupData.packed1 >> 16) & 0xFFFF;
    instanceLookupData.vertexColorTextureID1 = packedInstanceLookupData.packed2 & 0xFFFF;
    instanceLookupData.vertexOffset = packedInstanceLookupData.vertexOffset;
    instanceLookupData.vertexColor0Offset = packedInstanceLookupData.vertexColor0Offset;
    instanceLookupData.vertexColor1Offset = packedInstanceLookupData.vertexColor1Offset;
    instanceLookupData.loadedObjectID = packedInstanceLookupData.loadedObjectID;
    
    return instanceLookupData;
}

struct PackedMapObjectVertex
{
    uint data0; // half positionX, half positionY
    uint data1; // half positionZ, uint8_t octNormalX, uint8_t octNormalY
    uint data2; // half uvX, half uvY
    uint data3; // half uvZ, half uvW
}; // 16 bytes

struct MapObjectVertex
{
    float3 position;
    float4 uv;
#if !GEOMETRY_PASS
    float3 normal;
    float4 color0;
    float4 color1;
#endif
};

[[vk::binding(1, MAPOBJECT)]] StructuredBuffer<PackedMapObjectVertex> _packedMapObjectVertices;

float3 UnpackMapObjectPosition(PackedMapObjectVertex packedVertex)
{
    float3 position;

    position.x = f16tof32(packedVertex.data0);
    position.y = f16tof32(packedVertex.data0 >> 16);
    position.z = f16tof32(packedVertex.data1);

    return position;
}

float3 UnpackMapObjectNormal(PackedMapObjectVertex packedVertex)
{
    uint x = (packedVertex.data1 >> 16) & 0xFF;
    uint y = packedVertex.data1 >> 24;

    float2 octNormal = float2(x, y) / 255.0f;
    return OctNormalDecode(octNormal);
}

float4 UnpackMapObjectUVs(PackedMapObjectVertex packedVertex)
{
    float4 uvs;

    uvs.x = f16tof32(packedVertex.data2);
    uvs.y = f16tof32(packedVertex.data2 >> 16);
    uvs.z = f16tof32(packedVertex.data3);
    uvs.w = f16tof32(packedVertex.data3 >> 16);

    return uvs;
}

MapObjectVertex UnpackMapObjectVertex(PackedMapObjectVertex packedVertex)
{
    MapObjectVertex vertex;
    vertex.position = UnpackMapObjectPosition(packedVertex);
    vertex.uv = UnpackMapObjectUVs(packedVertex);

#if !GEOMETRY_PASS
    vertex.normal = UnpackMapObjectNormal(packedVertex);
#endif

    return vertex;
}

[[vk::binding(10, MAPOBJECT)]] Texture2D<float4> _mapObjectTextures[4096]; // Give this index 10 because this needs to be defined last

MapObjectVertex LoadMapObjectVertex(uint vertexID, InstanceLookupData lookupData)
{
    PackedMapObjectVertex packedVertex = _packedMapObjectVertices[vertexID];

    MapObjectVertex vertex = UnpackMapObjectVertex(packedVertex);

#if !GEOMETRY_PASS
    // vertexMeshOffset refers to the offset into the global vertices list where the mesh that this vertex is part of starts
    // localVertexOffset refers to the local vertex id, if the mesh starts at 300 and vertexID is 303, the localVertexOffset is 3
    uint localVertexOffset = vertexID - lookupData.vertexOffset;

    bool hasVertexColor0 = lookupData.vertexColor0Offset != 0xffffffff;
    {
        uint offsetVertexID0 = (localVertexOffset + lookupData.vertexColor0Offset) * hasVertexColor0;
        uint3 vertexColorUV0 = uint3((float)offsetVertexID0 % 1024.0f, (float)offsetVertexID0 / 1024.0f, 0);

        vertex.color0 = _mapObjectTextures[NonUniformResourceIndex(lookupData.vertexColorTextureID0)].Load(vertexColorUV0) * float4(hasVertexColor0, hasVertexColor0, hasVertexColor0, 1.0f);
    }

    bool hasVertexColor1 = lookupData.vertexColor1Offset != 0xffffffff;
    {
        uint offsetVertexID1 = (localVertexOffset + lookupData.vertexColor1Offset) * hasVertexColor1;
        uint3 vertexColorUV1 = uint3((float)offsetVertexID1 % 1024.0f, (float)offsetVertexID1 / 1024.0f, 0);

        vertex.color1 = _mapObjectTextures[NonUniformResourceIndex(lookupData.vertexColorTextureID1)].Load(vertexColorUV1) * float4(hasVertexColor1, hasVertexColor1, hasVertexColor1, 1.0f);
    }
#endif

    return vertex;
}

struct InstanceData
{
    float4x4 instanceMatrix;
};

[[vk::binding(2, MAPOBJECT)]] StructuredBuffer<InstanceData> _mapObjectInstanceData;

struct PackedMapObjectMaterialParam
{
    uint packed; // uint16_t materialID, uint16_t exteriorLit
}; // 4 bytes

struct MapObjectMaterialParam
{
    uint materialID;
    uint exteriorLit;
};

struct PackedMapObjectMaterial
{
    uint packed0; // uint16_t textureID0, uint16_t textureID1
    uint packed1; // uint16_t textureID2, uint16_t alphaTestVal
    uint packed2; // uint16_t materialType, uint16_t isUnlit
}; // 12 bytes

struct MapObjectMaterial
{
    uint textureIDs[3];
    uint alphaTestVal;
    uint materialType;
    uint isUnlit;
};
[[vk::binding(3, MAPOBJECT)]] StructuredBuffer<PackedMapObjectMaterialParam> _packedMapObjectMaterialParams;
[[vk::binding(4, MAPOBJECT)]] StructuredBuffer<PackedMapObjectMaterial> _packedMapObjectMaterialData;

MapObjectMaterialParam LoadMapObjectMaterialParam(uint materialParamID)
{
    PackedMapObjectMaterialParam packedMaterialParam = _packedMapObjectMaterialParams[materialParamID];

    MapObjectMaterialParam materialParam;

    materialParam.materialID = packedMaterialParam.packed & 0xFFFF;
    materialParam.exteriorLit = (packedMaterialParam.packed >> 16) & 0xFFFF;

    return materialParam;
}

MapObjectMaterial LoadMapObjectMaterial(uint materialID)
{
    PackedMapObjectMaterial packedMaterial = _packedMapObjectMaterialData[materialID];

    MapObjectMaterial material;

    material.textureIDs[0] = packedMaterial.packed0 & 0xFFFF;
    material.textureIDs[1] = (packedMaterial.packed0 >> 16) & 0xFFFF;
    material.textureIDs[2] = packedMaterial.packed1 & 0xFFFF;
    material.alphaTestVal = (packedMaterial.packed1 >> 16) & 0xFFFF;
    material.materialType = packedMaterial.packed2 & 0xFFFF;
    material.isUnlit = (packedMaterial.packed2 >> 16) & 0xFFFF;

    return material;
}

[[vk::binding(5, MAPOBJECT)]] StructuredBuffer<Draw> _mapObjectDraws;
[[vk::binding(6, MAPOBJECT)]] StructuredBuffer<uint> _mapObjectIndices;

#endif // MAPOBJECT_INC_INCLUDED