#pragma once
#include <NovusTypes.h>

#include <array>
#include <Math/Geometry.h>
#include <Utils/StringUtils.h>
#include <Utils/SafeVector.h>
#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/Descriptors/DepthImageDesc.h>
#include <Renderer/Descriptors/TextureDesc.h>
#include <Renderer/Descriptors/TextureArrayDesc.h>
#include <Renderer/Descriptors/ModelDesc.h>
#include <Renderer/Descriptors/SamplerDesc.h>
#include <Renderer/Descriptors/BufferDesc.h>
#include <Renderer/Buffer.h>
#include <Renderer/DescriptorSet.h>
#include <Renderer/GPUVector.h>

#include "../Gameplay/Map/Chunk.h"
#include "ViewConstantBuffer.h"

namespace Renderer
{
    class RenderGraph;
    class Renderer;
    class DescriptorSet;
}

namespace NDBC
{
    struct LiquidType;
}

class CameraFreeLook;
class DebugRenderer;
struct RenderResources;

class WaterRenderer
{
public:
    WaterRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer);
    ~WaterRenderer();

    void Update(f32 deltaTime);
    void LoadWater(SafeVector<u16>& chunkIDs);
    void Clear();

    void AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddWaterPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    // Drawcall stats
    u32 GetNumDrawCalls() { return static_cast<u32>(_drawCalls.Size()); }
    u32 GetNumSurvivingDrawCalls() { return _numSurvivingDrawCalls; }

    // Triangle stats
    u32 GetNumTriangles() { return _numTriangles; }
    u32 GetNumSurvivingTriangles() { return _numSurvivingTriangles; }
   
private:
    void CreatePermanentResources();

    bool RegisterChunksToBeLoaded(SafeVector<u16>& chunkIDs);
    void ExecuteLoad();

    bool TryLoadTexture(const std::string& textureName, u32 textureHash, u32 numTextures, u32& textureIndex);

    struct DrawCall
    {
        u32 indexCount;
        u32 instanceCount;
        u32 firstIndex;
        u32 vertexOffset;
        u32 firstInstance;
    };

#pragma pack(push, 1)
    struct WaterVertex
    {
        u8 xCellOffset = 0;
        u8 yCellOffset = 0;
        f16 height = f16(0);
        hvec2 uv = hvec2(f16(0), f16(0));
    };

    struct DrawCallData
    {
        u16 chunkID;
        u16 cellID;
        u16 textureStartIndex;
        u8 textureCount;
        u8 hasDepth;
    };
#pragma pack(pop)

    struct CullConstants
    {
        vec4 frustumPlanes[6];
        vec3 cameraPos;
        u32 maxDrawCount;
        u32 occlusionCull = false;
    };

    struct DrawConstants
    {
        vec4 shallowOceanColor = vec4(1, 1, 1, 1);
        vec4 deepOceanColor = vec4(1, 1, 1, 1);
        vec4 shallowRiverColor = vec4(1, 1, 1, 1);
        vec4 deepRiverColor = vec4(1, 1, 1, 1);
        f32 waterVisibilityRange = 10.0f;
        f32 currentTime = 0.0f;
    };

    struct AABB
    {
        vec4 min;
        vec4 max;
    };

    u32 _numSurvivingDrawCalls = 0;
    u32 _numTriangles = 0;
    u32 _numSurvivingTriangles = 0;

    struct WaterTextureInfo
    {
        u32 textureArrayIndex = 0;
        u32 numTextures = 0;
    };

    robin_hood::unordered_map<u32, WaterTextureInfo> _waterTextureInfos;

    CullConstants _cullConstants;
    DrawConstants _drawConstants;

    Renderer::Renderer* _renderer;

    Renderer::SamplerID _sampler;
    Renderer::SamplerID _occlusionSampler;

    Renderer::DescriptorSet _cullingDescriptorSet;
    Renderer::DescriptorSet _passDescriptorSet;

    Renderer::TextureArrayID _waterTextures;

    Renderer::GPUVector<DrawCall> _drawCalls;
    Renderer::GPUVector<DrawCallData> _drawCallDatas;

    Renderer::GPUVector<WaterVertex> _vertices;
    Renderer::GPUVector<u16> _indices;

    Renderer::GPUVector<AABB> _boundingBoxes;

    // GPU-only workbuffers
    Renderer::BufferID _culledDrawCallsBuffer;

    Renderer::BufferID _culledDrawCountBuffer;
    Renderer::BufferID _culledDrawCountReadBackBuffer;
    Renderer::BufferID _culledTriangleCountBuffer;
    Renderer::BufferID _culledTriangleCountReadBackBuffer;

    DebugRenderer* _debugRenderer;
};