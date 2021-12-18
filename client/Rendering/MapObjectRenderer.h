#pragma once
#include <NovusTypes.h>
#include <robin_hood.h>
#include <filesystem>
#include <Utils/ByteBuffer.h>
#include <Utils/SafeVector.h>
#include <Utils/SafeUnorderedMap.h>
#include <Containers/StringTable.h>

#include <Renderer/Buffer.h>
#include <Renderer/GPUVector.h>
#include <Renderer/Descriptors/SamplerDesc.h>
#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/Descriptors/DepthImageDesc.h>
#include <Renderer/Descriptors/BufferDesc.h>

#include "ViewConstantBuffer.h"
#include "../Gameplay/Map/MapObject.h"

namespace Renderer
{
    class RenderGraph;
    class Renderer;
    class DescriptorSet;
}

namespace Terrain
{
    struct Chunk;
    struct MapObject;
    struct Placement;
    struct PlacementDetails;
}

class StringTable;
class DebugRenderer;
struct RenderResources;

class MapObjectRenderer
{
    struct MeshRoot
    {
        u32 numMaterials;
        u32 numMeshes;
        u32 numDecorations;
        u32 numDecorationSets;
    };

    struct RenderBatch
    {
        u32 firstIndex;
        u32 indexCount;
    };

    struct Mesh
    {
        Terrain::MapObjectFlags renderFlags;

        u32 baseIndexOffset;
        u32 baseVertexOffset;
        u32 baseVertexColor1Offset;
        u32 baseVertexColor2Offset;
        u32 baseMaterialOffset;
    };

    struct MapObjectToBeLoaded
    {
        const Terrain::Placement* placement = nullptr;
        const std::string* nmorName = nullptr;
        u32 nmorNameHash = 0;

        MeshRoot meshRoot;
        std::vector<Mesh> meshes;
    };

    struct DrawCall
    {
        u32 indexCount;
        u32 instanceCount;
        u32 firstIndex;
        u32 vertexOffset;
        u32 firstInstance;
    };

    struct MaterialParameters
    {
        u16 materialID;
        u16 exteriorLit;
    };

    struct RenderBatchOffsets
    {
        u32 baseVertexOffset;
        u32 baseIndexOffset;
        u32 baseVertexColor1Offset;
        u32 baseVertexColor2Offset;
    };

#pragma pack(push, 1)
    struct MapObjectDecoration
    {
        u32 nameID;
        vec3 position;
        quaternion rotation;
        f32 scale;
        u32 color;
    };
    
     struct MapObjectDecorationSet
     {
         char name[20] = { 0 };
         u16 index;
         u32 count;
     };
#pragma pack(pop)

public:
    struct LoadedMapObject
    {
        LoadedMapObject(){}

        // We have to manually implement a copy constructor because std::mutex is not copyable
        LoadedMapObject(const LoadedMapObject& other) 
        {
            objectID = other.objectID;
            debugName = other.debugName;
            drawCallIDs = other.drawCallIDs;
            materialParameterIDs = other.materialParameterIDs;
            instanceIDs = other.instanceIDs;
            instanceMaterialParameterIDs = other.instanceMaterialParameterIDs;
            vertexColors[0] = other.vertexColors[0];
            vertexColors[1] = other.vertexColors[1];
            vertexColorTextureIDs[0] = other.vertexColorTextureIDs[0];
            vertexColorTextureIDs[1] = other.vertexColorTextureIDs[1];
            instanceCount = other.instanceCount;
            baseMaterialOffset = other.baseMaterialOffset;
            baseCullingDataOffset = other.baseCullingDataOffset;
            renderBatches = other.renderBatches;
            renderBatchOffsets = other.renderBatchOffsets;
            cullingData = other.cullingData;
        };

        u32 objectID;
        std::string debugName = "";

        std::vector<u32> drawCallIDs;
        std::vector<u16> materialParameterIDs;

        std::vector<u16> instanceIDs;
        std::vector<u32> instanceMaterialParameterIDs;

        std::vector<u32> vertexColors[2];

        u32 vertexColorTextureIDs[2] = { 0, 0 };
        u32 instanceCount;

        u32 baseMaterialOffset = 0;
        u32 baseCullingDataOffset = 0;

        // Renderbatches
        std::vector<Terrain::RenderBatch> renderBatches;
        std::vector<RenderBatchOffsets> renderBatchOffsets;

        // Decorations
        std::vector<MapObjectDecoration> decorations;
        std::vector<MapObjectDecorationSet> decorationSets;
        StringTable decorationStringTable;

        // Culling data
        std::vector<Terrain::CullingData> cullingData;
        std::mutex mutex;
    };

    struct InstanceLookupData
    {
        u16 instanceID;
        u16 materialParamID;
        u16 cullingDataID;
        u16 vertexColorTextureID0 = 0;
        u16 vertexColorTextureID1 = 0;
        u16 padding1;
        u32 vertexOffset;
        u32 vertexColor1Offset;
        u32 vertexColor2Offset;
        u32 loadedObjectID;
    };

    struct InstanceData
    {
        mat4x4 instanceMatrix;
    };

public:
    MapObjectRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer);

    void Update(f32 deltaTime);

    void AddOccluderPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddEditorPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    void RegisterMapObjectToBeLoaded(const std::string& mapObjectName, const Terrain::Placement& mapObjectPlacement);
    void RegisterMapObjectsToBeLoaded(u16 chunkID, const Terrain::Chunk& chunk, StringTable& stringTable);
    void ExecuteLoad();

    void Clear();

    SafeVector<LoadedMapObject>& GetLoadedMapObjects() { return _loadedMapObjects; }
    SafeVector<InstanceData>& GetInstances() { return _instances; }
    SafeVector<InstanceLookupData>& GetInstanceLookupData() { return _instanceLookupData; }

    u32 GetNumLoadedMapObjects() { return static_cast<u32>(_loadedMapObjects.Size()); }
    u32 GetNumMapObjectPlacements() { return static_cast<u32>(_instances.Size()); }

    // Drawcall stats
    u32 GetNumDrawCalls() { return static_cast<u32>(_drawCalls.Size()); }
    u32 GetNumSurvivingOccluderDrawCalls() { return _numSurvivingOccluderDrawCalls; }
    u32 GetNumSurvivingGeometryDrawCalls() { return _numSurvivingGeometryDrawCalls; }

    // Triangle stats
    u32 GetNumTriangles() { return _numTriangles; }
    u32 GetNumSurvivingOccluderTriangles() { return _numSurvivingOccluderTriangles; }
    u32 GetNumSurvivingGeometryTriangles() { return _numSurvivingGeometryTriangles; }

    Renderer::DescriptorSet& GetMaterialPassDescriptorSet() { return _materialPassDescriptorSet; };

private:
    void CreatePermanentResources();
    bool LoadMapObject(MapObjectToBeLoaded& mapObjectToBeLoaded, LoadedMapObject& mapObject);

    // Sub loaders
    bool LoadRoot(const std::filesystem::path nmorPath, MeshRoot& meshRoot, LoadedMapObject& mapObject);
    bool LoadMesh(const std::filesystem::path nmoPath, Mesh& mesh, LoadedMapObject& mapObject);

    bool LoadIndicesAndVertices(Bytebuffer& buffer, Mesh& mesh, LoadedMapObject& mapObject);

    bool LoadRenderBatches(Bytebuffer& buffer, Mesh& mesh, LoadedMapObject& mapObject);

    void AddInstance(LoadedMapObject& mapObject, const Terrain::Placement* placement, u32& instanceIndex);

    void CreateBuffers();

    struct Material
    {
        u16 textureIDs[3] = { 0,0,0 };
        f16 alphaTestVal = f16(-1.0f);//1.0f / 255.0f;//1.0f / 16.0f;//1.0f / 255.0f;
        u16 materialType = 0;
        u16 unlit = 0;
    };

    struct MeshData
    {
        u32 materialID;
        u32 renderFlags;
        u32 vertexColorTextureID;
        u32 vertexUVIndex;
    };

    struct CullingConstants
    {
        vec4 frustumPlanes[6];
        vec3 cameraPos;
        u32 maxDrawCount;
        u32 occlusionEnabled;
    };

private:
    Renderer::Renderer* _renderer;
    DebugRenderer* _debugRenderer;

    Renderer::SamplerID _sampler;
    Renderer::SamplerID _occlusionSampler;

    Renderer::DescriptorSet _occluderFillDescriptorSet;
    Renderer::DescriptorSet _cullingDescriptorSet;
    Renderer::DescriptorSet _geometryPassDescriptorSet;
    Renderer::DescriptorSet _materialPassDescriptorSet;
    Renderer::DescriptorSet _sortingDescriptorSet;

    SafeVector<LoadedMapObject> _loadedMapObjects;
    SafeUnorderedMap<u32, u32> _nameHashToIndexMap;

    Renderer::Buffer<CullingConstants>* _cullingConstantBuffer;

    Renderer::GPUVector<DrawCall> _drawCalls;

    Renderer::GPUVector<Terrain::MapObjectVertex> _vertices;
    Renderer::GPUVector<u16> _indices;
    Renderer::GPUVector<InstanceData> _instances;
    Renderer::GPUVector<InstanceLookupData> _instanceLookupData;
    Renderer::GPUVector<Material> _materials;
    Renderer::GPUVector<MaterialParameters> _materialParameters;
    Renderer::GPUVector<Terrain::CullingData> _cullingData;

    // GPU-only workbuffers
    FrameResource<Renderer::BufferID, 2> _culledDrawCallsBitMaskBuffer;
    Renderer::BufferID _culledDrawCallsBuffer;
    Renderer::BufferID _culledSortedDrawCallsBuffer;
    Renderer::BufferID _sortKeysBuffer;
    Renderer::BufferID _sortValuesBuffer;

    Renderer::BufferID _drawCountBuffer;
    Renderer::BufferID _occluderDrawCountReadBackBuffer;
    Renderer::BufferID _geometryDrawCountReadBackBuffer;

    Renderer::BufferID _triangleCountBuffer;
    Renderer::BufferID _occluderTriangleCountReadBackBuffer;
    Renderer::BufferID _geometryTriangleCountReadBackBuffer;

    Renderer::TextureArrayID _mapObjectTextures;

    SafeUnorderedMap<u32, u8> _uniqueIdCounter;

    u32 _numSurvivingOccluderDrawCalls;
    u32 _numSurvivingGeometryDrawCalls;

    u32 _numTriangles;
    u32 _numSurvivingOccluderTriangles;
    u32 _numSurvivingGeometryTriangles;

    SafeVector<MapObjectToBeLoaded> _mapObjectsToBeLoaded;
};