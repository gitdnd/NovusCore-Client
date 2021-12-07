#pragma once
#include <NovusTypes.h>

#include <array>

#include <Utils/StringUtils.h>
#include <Utils/SafeVector.h>
#include <Math/Geometry.h>
#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/Descriptors/DepthImageDesc.h>
#include <Renderer/Descriptors/TextureDesc.h>
#include <Renderer/Descriptors/TextureArrayDesc.h>
#include <Renderer/Descriptors/SamplerDesc.h>
#include <Renderer/Descriptors/BufferDesc.h>
#include <Renderer/Buffer.h>
#include <Renderer/DescriptorSet.h>

#include "../Gameplay/Map/Chunk.h"

namespace Terrain
{
    struct Map;

    constexpr u32 NUM_VERTICES_PER_CHUNK = Terrain::MAP_CELL_TOTAL_GRID_SIZE * Terrain::MAP_CELLS_PER_CHUNK;
    constexpr u32 NUM_INDICES_PER_CELL = 768;
    constexpr u32 NUM_TRIANGLES_PER_CELL = NUM_INDICES_PER_CELL / 3;
}

namespace Renderer
{
    class RenderGraph;
    class Renderer;
}

namespace NDBC
{
    struct Map;
}

class Camera;
class DebugRenderer;
class MapObjectRenderer;
class CModelRenderer;
class WaterRenderer;
struct RenderResources;

class TerrainRenderer
{
    struct ChunkToBeLoaded
    {
        Terrain::Map* map = nullptr;
        Terrain::Chunk* chunk = nullptr;
        u16 chunkPosX;
        u16 chunkPosY;
        u16 chunkID;
    };

    struct CullingConstants
    {
        vec4 frustumPlanes[6];
        mat4x4 viewmat;
        u32 occlusionEnabled;
    };

    struct CellInstance
    {
        u32 packedChunkCellID;
        u32 instanceID;
    };

#pragma pack(push, 1)
    struct TerrainVertex
    {
        u8 normal[3];
        u8 color[3];
        f16 height;
    };
#pragma pack(pop)

public:
    TerrainRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer, MapObjectRenderer* mapObjectRenderer, CModelRenderer* cModelRenderer, WaterRenderer* waterRenderer);
    ~TerrainRenderer();

    void Update(f32 deltaTime);

    void AddOccluderPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddEditorPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    bool LoadMap(const NDBC::Map* map);

    const SafeVector<Geometry::AABoundingBox>& GetBoundingBoxes() { return _cellBoundingBoxes; }

    // Drawcall stats
    u32 GetNumDrawCalls() { return Terrain::MAP_CELLS_PER_CHUNK * static_cast<u32>(_loadedChunks.Size()); }
    u32 GetNumOccluderDrawCalls() { return _numOccluderDrawCalls; }
    u32 GetNumSurvivingDrawCalls() { return _numSurvivingDrawCalls; }

    // Triangle stats
    u32 GetNumTriangles() { return Terrain::MAP_CELLS_PER_CHUNK * static_cast<u32>(_loadedChunks.Size()) * Terrain::NUM_TRIANGLES_PER_CELL; }
    u32 GetNumOccluderTriangles() { return _numOccluderDrawCalls * Terrain::NUM_TRIANGLES_PER_CELL; }
    u32 GetNumSurvivingGeometryTriangles() { return _numSurvivingDrawCalls * Terrain::NUM_TRIANGLES_PER_CELL; }

    u32 GetInstanceIDFromChunkID(u32 chunkID);

    Renderer::DescriptorSet& GetMaterialPassDescriptorSet() { return _materialPassDescriptorSet; };
    
private:
    void CreatePermanentResources();

    void RegisterChunksToBeLoaded(Terrain::Map& map, ivec2 middleChunk, u16 drawDistance);
    void RegisterChunkToBeLoaded(Terrain::Map& map, u16 chunkPosX, u16 chunkPosY);
    void ExecuteLoad();

    void LoadChunk(const ChunkToBeLoaded& chunkToBeLoaded);
    //void LoadChunksAround(Terrain::Map& map, ivec2 middleChunk, u16 drawDistance);

    void DebugRenderCellTriangles(const Camera* camera);
private:
    Renderer::Renderer* _renderer; 
    
    CullingConstants _cullingConstants;

    Renderer::BufferID _instanceBuffer;

    FrameResource<Renderer::BufferID, 2> _culledInstanceBitMaskBuffer;
    Renderer::BufferID _culledInstanceBuffer;
    Renderer::BufferID _cellHeightRangeBuffer;
    Renderer::BufferID _occluderArgumentBuffer;
    Renderer::BufferID _argumentBuffer;

    Renderer::BufferID _occluderDrawCountReadBackBuffer;
    Renderer::BufferID _drawCountReadBackBuffer;

    Renderer::BufferID _chunkBuffer;
    Renderer::BufferID _cellBuffer;

    Renderer::BufferID _vertexBuffer;

    Renderer::BufferID _cellIndexBuffer;
    
    Renderer::TextureArrayID _terrainColorTextureArray;

    Renderer::TextureArrayID _terrainAlphaTextureArray;

    Renderer::SamplerID _alphaSampler;
    Renderer::SamplerID _colorSampler;
    Renderer::SamplerID _occlusionSampler;

    Renderer::DescriptorSet _geometryPassDescriptorSet;

    Renderer::DescriptorSet _occluderFillPassDescriptorSet;
    Renderer::DescriptorSet _occluderDrawPassDescriptorSet;
    Renderer::DescriptorSet _cullingPassDescriptorSet;
    Renderer::DescriptorSet _materialPassDescriptorSet;
    Renderer::DescriptorSet _editorPassDescriptorSet;

    SafeVector<u16> _loadedChunks;
    SafeVector<Geometry::AABoundingBox> _cellBoundingBoxes;

    std::vector<CellInstance> _culledInstances;
    std::vector<ChunkToBeLoaded> _chunksToBeLoaded;

    std::mutex _subLoadMutex;

    u32 _numOccluderDrawCalls;
    u32 _numSurvivingDrawCalls;
    
    robin_hood::unordered_map<u32, u32> _chunkIDToInstanceID;

    DebugRenderer* _debugRenderer = nullptr;
    MapObjectRenderer* _mapObjectRenderer = nullptr;
    CModelRenderer* _cModelRenderer = nullptr;
    WaterRenderer* _waterRenderer = nullptr;
};