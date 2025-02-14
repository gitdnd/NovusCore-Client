#pragma once
#include <NovusTypes.h>
#include <mutex>
#include <queue>

#include <Utils/StringUtils.h>
#include <Utils/ConcurrentQueue.h>
#include <Utils/SafeVector.h>
#include <Utils/SafeUnorderedMap.h>
#include <Math/Geometry.h>
#include <Memory/BufferRangeAllocator.h>
#include <entity/fwd.hpp>

#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/Descriptors/DepthImageDesc.h>
#include <Renderer/Descriptors/TextureDesc.h>
#include <Renderer/Descriptors/TextureArrayDesc.h>
#include <Renderer/Descriptors/SamplerDesc.h>
#include <Renderer/Descriptors/BufferDesc.h>
#include <Renderer/Buffer.h>
#include <Renderer/GPUVector.h>
#include <Renderer/DescriptorSet.h>

#include "../Gameplay/Map/Chunk.h"
#include "CModel/CModel.h"
#include "ViewConstantBuffer.h"

namespace Renderer
{
    class RenderGraph;
    class Renderer;
    class DescriptorSet;
}

namespace Terrain
{
    struct PlacementDetails;
}

namespace NDBC
{
    struct CreatureDisplayInfo;
    struct CreatureModelData;
}

class CameraFreeLook;
class DebugRenderer;
class MapObjectRenderer;
struct RenderResources;

constexpr u32 CMODEL_INVALID_TEXTURE_ID = std::numeric_limits<u32>().max();
constexpr u8 CMODEL_INVALID_TEXTURE_UNIT_INDEX = std::numeric_limits<u8>().max();
class CModelRenderer
{
public:
    struct DrawCall
    {
        u32 indexCount;
        u32 instanceCount;
        u32 firstIndex;
        u32 vertexOffset;
        u32 drawID;
    };

    struct DrawCallData
    {
        u32 instanceID;
        u32 textureUnitOffset;
        u16 numTextureUnits;
        u16 numUnlitTextureUnits;
    };

    struct ModelInstanceData
    {
        u32 modelID = 0;
        u32 boneDeformOffset;
        u32 boneInstanceDataOffset;
        u32 unused;
        u32 modelVertexOffset = 0;
        u32 animatedVertexOffset = 0;
    };

    struct InstanceDisplayInfo
    {
        u32 opaqueDrawCallOffset = 0;
        u32 opaqueDrawCallCount = 0;
        u32 transparentDrawCallOffset = 0;
        u32 transparentDrawCallCount = 0;
    };

    struct LoadedComplexModel
    {
        LoadedComplexModel() {}

        // We have to manually implement a copy constructor because std::mutex is not copyable
        LoadedComplexModel(const LoadedComplexModel& other)
        {
            modelID = other.modelID;
            debugName = other.debugName;
            numVertices = other.numVertices;
            vertexOffset = other.vertexOffset;
            numBones = other.numBones;
            isAnimated = other.isAnimated;
            numOpaqueDrawCalls = other.numOpaqueDrawCalls;
            opaqueDrawCallTemplates = other.opaqueDrawCallTemplates;
            opaqueDrawCallDataTemplates = other.opaqueDrawCallDataTemplates;
            numTransparentDrawCalls = other.numTransparentDrawCalls;
            transparentDrawCallTemplates = other.transparentDrawCallTemplates;
            transparentDrawCallDataTemplates = other.transparentDrawCallDataTemplates;
        };

        u32 modelID;
        std::string debugName = "";
        bool failedToLoad = false;
        bool isStaticModel = false;

        u32 numVertices = 0;
        u32 vertexOffset = 0;

        u32 numCollisionTriangles = 0;
        u32 collisionTriangleOffset = 0;
        Geometry::AABoundingBox collisionAABB;

        u32 numBones = 0;
        u32 numSequences = 0;
        u32 sequenceOffset = 0;
        bool isAnimated = false;
        std::vector<i32> boneKeyId;

        u32 numOpaqueDrawCalls = 0;
        std::vector<DrawCall> opaqueDrawCallTemplates;
        std::vector<DrawCallData> opaqueDrawCallDataTemplates;

        u32 numTransparentDrawCalls = 0;
        std::vector<DrawCall> transparentDrawCallTemplates;
        std::vector<DrawCallData> transparentDrawCallDataTemplates;

        std::mutex mutex;
    };

    struct AnimationBoneInstance
    {
        enum AnimateState
        {
            STOPPED,
            PLAY_ONCE,
            PLAY_LOOP
        };

        f32 animationProgress = 0.f;
        u32 sequenceIndex = 65535;
        u32 animationframeIndex = 0;
        u32 animateState = 0; // 0 == STOPPED, 1 == PLAY_ONCE, 2 == PLAY_LOOP
    };

    struct AnimationRequest
    {
        struct Flags
        {
            u8 isPlaying : 1;
            u8 isLooping : 1;
        };

        u32 instanceId = 0;
        u16 boneIndex = 0;
        u16 sequenceIndex = 0;

        Flags flags;
    }; 
    
    struct AnimationModelInfo
    {
        u32 isAnimated = 0;
        u32 numBones = 0;
        u32 boneInfoOffset = 0;
        u32 sequenceOffset = 0;
    };

    struct AnimationSequence
    {
        u16 animationId = 0;
        u16 animationSubId = 0;
        u16 nextSubAnimationId = 0;
        u16 nextAliasId = 0;

        struct AnimationSequenceFlag
        {
            u32 isAlwaysPlaying : 1;
            u32 isAlias : 1;
            u32 blendTransition : 1; // (This applies if set on either side of the transition) If set we lerp between the end -> start states, but only if end != start (Compare Bone Values)
        } flags;

        f32 duration = 0.f;

        u16 repeatMin = 0;
        u16 repeatMax = 0;

        u16 blendTimeStart = 0;
        u16 blendTimeEnd = 0;

        u64 padding = 0;
    };

    struct AnimationBoneInfo
    {
        u16 numTranslationSequences = 0;
        u16 numRotationSequences = 0;
        u16 numScaleSequences = 0;

        i16 parentBoneId = 0;

        u32 translationSequenceOffset = 0;
        u32 rotationSequenceOffset = 0;
        u32 scaleSequenceOffset = 0;

        struct Flags
        {
            u32 animate : 1;
            u32 isTranslationTrackGlobalSequence : 1;
            u32 isRotationTrackGlobalSequence : 1;
            u32 isScaleTrackGlobalSequence : 1;

            u32 ignoreParentTranslation : 1;
            u32 ignoreParentRotation : 1;
            u32 ignoreParentScale : 1;
        } flags;

        f32 pivotPointX = 0.f;
        f32 pivotPointY = 0.f;
        f32 pivotPointZ = 0.f;

        u32 padding0;
        u32 padding1;
        u32 padding2;
    };

    struct AnimationTrackInfo
    {
        u16 sequenceIndex = 0;
        u16 padding = 0;

        u16 numTimestamps = 0;
        u16 numValues = 0;

        u32 timestampOffset = 0;
        u32 valueOffset = 0;
    };

public:
    CModelRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer);
    ~CModelRenderer();

    void OnModelCreated(entt::registry& registry, entt::entity entity);
    void OnModelDestroyed(entt::registry& registry, entt::entity entity);
    void OnModelVisible(entt::registry& registry, entt::entity entity);
    void OnModelInvisible(entt::registry& registry, entt::entity entity);

    void Update(f32 deltaTime);

    void AddOccluderPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddAnimationPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddEditorPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddTransparencyPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    void RegisterLoadFromChunk(u16 chunkID, const Terrain::Chunk& chunk, StringTable& stringTable);
    void RegisterLoadFromDecoration(const std::string& modelPath, const u32& modelPathHash, vec3 position, quaternion rotation, f32 scale);
    void ExecuteLoad();

    void Clear();

    SafeVector<DrawCallData>& GetOpaqueDrawCallData() { return _opaqueDrawCallDatas; }
    SafeVector<DrawCallData>& GetTransparentDrawCallData() { return _transparentDrawCallDatas; }
    SafeVector<LoadedComplexModel>& GetLoadedComplexModels() { return _loadedComplexModels; }
    SafeVector<CModel::ComplexModel>& GetLoadedComplexModelsFileData() { return _loadedComplexModelFileData; }
    SafeVector<ModelInstanceData>& GetModelInstanceDatas() { return _modelInstanceDatas; }
    SafeVector<Geometry::Triangle>& GetCollisionTriangles() { return _collisionTriangles; }
    const ModelInstanceData GetModelInstanceData(size_t instanceID) { return _modelInstanceDatas.ReadGet(instanceID); }
    const AnimationModelInfo GetAnimationModelInfo(size_t modelID) { return _animationModelInfo.ReadGet(modelID); }
    const AnimationBoneInstance GetAnimationBoneInstance(size_t boneIndex) { return _animationBoneInstances.ReadGet(boneIndex); }
    const AnimationBoneInfo GetAnimationBoneInfo(size_t boneIndex) { return _animationBoneInfo.ReadGet(boneIndex); }
    const AnimationTrackInfo GetAnimationTrackInfo(size_t trackIndex) { return _animationTrackInfo.ReadGet(trackIndex); }

    Renderer::GPUVector<mat4x4>& GetModelInstanceMatrices() { return _modelInstanceMatrices; }
    Renderer::GPUVector<AnimationBoneInfo>& GetAnimationBoneInfos() { return _animationBoneInfo; }
    Renderer::GPUVector<AnimationBoneInstance>& GetAnimationBoneInstances() { return _animationBoneInstances; }
    Renderer::GPUVector<AnimationTrackInfo>& GetAnimationTrackInfos() { return _animationTrackInfo; }
    const mat4x4 GetModelInstanceMatrix(size_t index) { return _modelInstanceMatrices.ReadGet(index); }

    const SafeVector<CModel::CullingData>& GetCullingData() { return _cullingDatas; }

    void AddAnimationRequest(AnimationRequest request)
    {
        _animationRequests.enqueue(request);
    }
    u32 GetNumSequencesForModelId(u32 modelId)
    {
        return _loadedComplexModels.ReadGet(modelId).numSequences;
    }

    const Renderer::GPUVector<AnimationSequence>& GetAnimationSequences()
    {
        return _animationSequences;
    }

    u32 GetNumLoadedCModels() { return static_cast<u32>(_loadedComplexModels.Size()); }
    u32 GetNumLoadedCModelsFileData() { return static_cast<u32>(_loadedComplexModelFileData.Size()); }
    u32 GetNumCModelPlacements() { return static_cast<u32>(_modelInstanceDatas.Size()); }
    u32 GetModelIndexByDrawCallDataIndex(u32 index, bool isOpaque)
    {
        u32 modelIndex = std::numeric_limits<u32>().max();

        if (isOpaque)
        {
            _opaqueDrawCallDataIndexToLoadedModelIndex.WriteLock([&](robin_hood::unordered_map<u32, u32> opaqueDrawCallDataIndexToLoadedModelIndex)
            {
                modelIndex = opaqueDrawCallDataIndexToLoadedModelIndex[index];
            });
        }
        else
        {
            _transparentDrawCallDataIndexToLoadedModelIndex.WriteLock([&](robin_hood::unordered_map<u32, u32> transparentDrawCallDataIndexToLoadedModelIndex)
            {
                modelIndex = transparentDrawCallDataIndexToLoadedModelIndex[index];
            });
        }
            
        return modelIndex;
    }
    
    // Drawcall stats
    u32 GetNumOpaqueDrawCalls() { return static_cast<u32>(_opaqueDrawCalls.Size()); }
    u32 GetNumOccluderSurvivingDrawCalls() { return _numOccluderSurvivingDrawCalls; }
    u32 GetNumOpaqueSurvivingDrawCalls() { return _numOpaqueSurvivingDrawCalls; }
    u32 GetNumTransparentDrawCalls() { return static_cast<u32>(_transparentDrawCalls.Size()); }
    u32 GetNumTransparentSurvivingDrawCalls() { return _numTransparentSurvivingDrawCalls; }

    // Triangle stats
    u32 GetNumOpaqueTriangles() { return _numOpaqueTriangles; }
    u32 GetNumOccluderSurvivingTriangles() { return _numOccluderSurvivingTriangles; }
    u32 GetNumOpaqueSurvivingTriangles() { return _numOpaqueSurvivingTriangles; }
    u32 GetNumTransparentTriangles() { return _numTransparentTriangles; }
    u32 GetNumTransparentSurvivingTriangles() { return _numTransparentSurvivingTriangles; }

    Renderer::DescriptorSet& GetMaterialPassDescriptorSet() { return _materialPassDescriptorSet; }

private:
    struct ComplexModelToBeLoaded
    {
        const Terrain::Placement* placement = nullptr;
        const std::string* name = nullptr;
        u32 nameHash = 0;
        entt::entity entityID = entt::null;
    };

    struct TextureUnit
    {
        u16 data = 0; // Texture Flag + Material Flag + Material Blending Mode
        u16 materialType = 0; // Shader ID
        u32 textureIds[2] = { CMODEL_INVALID_TEXTURE_ID, CMODEL_INVALID_TEXTURE_ID };
        u32 pad;
    };
    
    struct PackedAnimatedVertexPositions
    {
        u32 packed0;
        u32 packed1;
    };

    struct RenderBatch
    {
        u16 indexStart = 0;
        u16 indexCount = 0;
        bool isBackfaceCulled = true;

        u8 textureUnitIndices[8] =
        {
            CMODEL_INVALID_TEXTURE_UNIT_INDEX, CMODEL_INVALID_TEXTURE_UNIT_INDEX,
            CMODEL_INVALID_TEXTURE_UNIT_INDEX, CMODEL_INVALID_TEXTURE_UNIT_INDEX,
            CMODEL_INVALID_TEXTURE_UNIT_INDEX, CMODEL_INVALID_TEXTURE_UNIT_INDEX,
            CMODEL_INVALID_TEXTURE_UNIT_INDEX, CMODEL_INVALID_TEXTURE_UNIT_INDEX
        };

        Renderer::BufferID indexBuffer;
        Renderer::BufferID textureUnitIndicesBuffer;
    };

    struct Mesh
    {
        std::vector<RenderBatch> renderBatches;
        std::vector<TextureUnit> textureUnits;

        Renderer::BufferID vertexBuffer;
        Renderer::BufferID textureUnitsBuffer;
    };

    struct CullConstants
    {
        vec4 frustumPlanes[6];
        vec3 cameraPos;
        u32 maxDrawCount;
        u32 occlusionCull = false;
    };

private:
    void CreatePermanentResources();

    bool LoadComplexModel(ComplexModelToBeLoaded& complexModelToBeLoaded, LoadedComplexModel& complexModel);
    bool LoadFile(const std::string& cModelPathString, CModel::ComplexModel& cModel);

    bool IsRenderBatchTransparent(const CModel::ComplexRenderBatch& renderBatch, const CModel::ComplexModel& cModel);

    void AddInstance(LoadedComplexModel& complexModel, const Terrain::Placement& placement, entt::entity entityID, u32& instanceId);

    void CreateBuffers();
    void SyncBuffers();
private:
    Renderer::Renderer* _renderer; 
    bool _loadingIsDirty = false;
    bool _clearedThisFrame = true;

    Renderer::SamplerID _sampler;
    Renderer::SamplerID _occlusionSampler;

    Renderer::DescriptorSet _animationPrepassDescriptorSet;
    Renderer::DescriptorSet _compactDescriptorSet;
    Renderer::DescriptorSet _visibleInstanceArgumentDescriptorSet;
    Renderer::DescriptorSet _occluderFillDescriptorSet;
    Renderer::DescriptorSet _opaqueCullingDescriptorSet;
    Renderer::DescriptorSet _transparentCullingDescriptorSet;
    Renderer::DescriptorSet _sortingDescriptorSet;
    Renderer::DescriptorSet _geometryPassDescriptorSet;
    Renderer::DescriptorSet _materialPassDescriptorSet;
    Renderer::DescriptorSet _transparencyPassDescriptorSet;

    robin_hood::unordered_map<u32, u8> _uniqueIdCounter;
    std::shared_mutex _uniqueIdCounterMutex;

    SafeVector<ComplexModelToBeLoaded> _complexModelsToBeLoaded;
    SafeVector<LoadedComplexModel> _loadedComplexModels;
    SafeVector<CModel::ComplexModel> _loadedComplexModelFileData;

    SafeUnorderedMap<u32, u32> _nameHashToIndexMap;
    SafeUnorderedMap<u32, NDBC::CreatureDisplayInfo*> _nameHashToCreatureDisplayInfo;
    SafeUnorderedMap<u32, NDBC::CreatureModelData*> _nameHashToCreatureModelData;

    SafeUnorderedMap<u32, u32> _opaqueDrawCallDataIndexToLoadedModelIndex;
    SafeUnorderedMap<u32, u32> _transparentDrawCallDataIndexToLoadedModelIndex;

    SafeUnorderedMap<u32, std::queue<u32>> _freedModelIDInstances; // Key is ModelID, value is queue of InstanceID

    moodycamel::ConcurrentQueue<AnimationRequest> _animationRequests;

    Renderer::GPUVector<CModel::ComplexVertex> _vertices;
    Renderer::GPUVector<u16> _indices;
    Renderer::GPUVector<TextureUnit> _textureUnits;
    Renderer::GPUVector<ModelInstanceData> _modelInstanceDatas;
    SafeVector<InstanceDisplayInfo> _instanceDisplayInfos;
    SafeVector<Geometry::Triangle> _collisionTriangles;
    SafeVector<entt::entity> _instanceIDToEntityID;

    Renderer::GPUVector<mat4x4> _modelInstanceMatrices;
    Renderer::GPUVector<CModel::CullingData> _cullingDatas;

    Renderer::GPUVector<mat4x4> _animationBoneDeformMatrices;
    Renderer::GPUVector<AnimationBoneInstance> _animationBoneInstances;
    Renderer::GPUVector<AnimationSequence> _animationSequences;
    Renderer::GPUVector<AnimationModelInfo> _animationModelInfo;
    Renderer::GPUVector<AnimationBoneInfo> _animationBoneInfo;
    Renderer::GPUVector<AnimationTrackInfo> _animationTrackInfo;
    Renderer::GPUVector<u32> _animationTrackTimestamps;
    Renderer::GPUVector<vec4> _animationTrackValues;

    Renderer::GPUVector<DrawCall> _opaqueDrawCalls;
    Renderer::GPUVector<DrawCallData> _opaqueDrawCallDatas;

    Renderer::GPUVector<DrawCall> _transparentDrawCalls;
    Renderer::GPUVector<DrawCallData> _transparentDrawCallDatas;

    // GPU-only workbuffers
    Renderer::BufferID _visibleInstanceMaskBuffer;
    Renderer::BufferID _visibleInstanceCountBuffer;
    Renderer::BufferID _visibleInstanceIndexBuffer;
    Renderer::BufferID _visibleInstanceCountArgumentBuffer32;

    Renderer::BufferID _animatedVertexPositions;

    Renderer::BufferID _occluderDrawCountReadBackBuffer;
    Renderer::BufferID _occluderTriangleCountReadBackBuffer;

    FrameResource<Renderer::BufferID, 2> _opaqueCulledDrawCallBitMaskBuffer;
    Renderer::BufferID _opaqueCulledDrawCallBuffer;
    Renderer::BufferID _opaqueDrawCountBuffer;
    Renderer::BufferID _opaqueDrawCountReadBackBuffer;
    Renderer::BufferID _opaqueTriangleCountBuffer;
    Renderer::BufferID _opaqueTriangleCountReadBackBuffer;

    Renderer::BufferID _transparentCulledDrawCallBuffer;
    Renderer::BufferID _transparentDrawCountBuffer;
    Renderer::BufferID _transparentDrawCountReadBackBuffer;
    Renderer::BufferID _transparentTriangleCountBuffer;
    Renderer::BufferID _transparentTriangleCountReadBackBuffer;

    CullConstants _cullConstants;

    Renderer::TextureArrayID _cModelTextures;

    std::atomic<u32> _numTotalAnimatedVertices;
    u32 _numOccluderSurvivingDrawCalls;
    u32 _numOpaqueSurvivingDrawCalls;
    u32 _numTransparentSurvivingDrawCalls;

    u32 _numOpaqueTriangles;
    u32 _numOccluderSurvivingTriangles;
    u32 _numOpaqueSurvivingTriangles;
    u32 _numTransparentTriangles;
    u32 _numTransparentSurvivingTriangles;

    DebugRenderer* _debugRenderer;
};