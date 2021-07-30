#pragma once
#include <NovusTypes.h>
#include <mutex>

#include <Utils/StringUtils.h>
#include <Utils/ConcurrentQueue.h>
#include <Utils/SafeVector.h>
#include <Utils/SafeUnorderedMap.h>
#include <Memory/BufferRangeAllocator.h>

#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/Descriptors/DepthImageDesc.h>
#include <Renderer/Descriptors/TextureDesc.h>
#include <Renderer/Descriptors/TextureArrayDesc.h>
#include <Renderer/Descriptors/SamplerDesc.h>
#include <Renderer/Descriptors/BufferDesc.h>
#include <Renderer/Buffer.h>
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
        u32 firstInstance;
    };

    struct DrawCallData
    {
        u32 instanceID;
        u32 cullingDataID;
        u16 textureUnitOffset;
        u16 numTextureUnits;
        u32 renderPriority;
    };

    struct LoadedComplexModel
    {
        LoadedComplexModel() {}

        // We have to manually implement a copy constructor because std::mutex is not copyable
        LoadedComplexModel(const LoadedComplexModel& other)
        {
            objectID = other.objectID;
            debugName = other.debugName;
            cullingDataID = other.cullingDataID;
            numBones = other.numBones;
            isAnimated = other.isAnimated;
            numOpaqueDrawCalls = other.numOpaqueDrawCalls;
            opaqueDrawCallTemplates = other.opaqueDrawCallTemplates;
            opaqueDrawCallDataTemplates = other.opaqueDrawCallDataTemplates;
            numTransparentDrawCalls = other.numTransparentDrawCalls;
            transparentDrawCallTemplates = other.transparentDrawCallTemplates;
            transparentDrawCallDataTemplates = other.transparentDrawCallDataTemplates;
        };

        u32 objectID;
        std::string debugName = "";
        bool failedToLoad = false;

        u32 cullingDataID = std::numeric_limits<u32>().max();
        u32 numBones = 0;
        bool isAnimated = false;

        u32 numOpaqueDrawCalls = 0;
        std::vector<DrawCall> opaqueDrawCallTemplates;
        std::vector<DrawCallData> opaqueDrawCallDataTemplates;

        u32 numTransparentDrawCalls = 0;
        std::vector<DrawCall> transparentDrawCallTemplates;
        std::vector<DrawCallData> transparentDrawCallDataTemplates;

        std::mutex mutex;
    };

    struct Instance
    {
        mat4x4 instanceMatrix;
        
        u32 modelId = 0;
        u32 boneDeformOffset;
        u32 boneInstanceDataOffset;
        u16 editorSequenceId; // This is used by the editor to display the sequenceId we want to play.
        u16 editorIsLoop; // This is used by the editor to display if the animation we want to play should looping.

        /*u32 modelId = 0;
        u32 activeSequenceId = 0;
        f32 animProgress = 0.0f;
        u32 boneDeformOffset = 0;*/
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
        u32 sequenceIndex = 0;
        u32 animationframeIndex = 0;
        u32 animateState = 0; // 0 == STOPPED, 1 == PLAY_ONCE, 2 == PLAY_LOOP
    };

    struct AnimationRequest
    {
        struct Flags
        {
            u32 isPlaying : 1;
            u32 isLooping : 1;
        };
        u32 instanceId = 0;
        u32 sequenceId = 0;

        Flags flags;
    };

public:
    CModelRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer);
    ~CModelRenderer();

    void Update(f32 deltaTime);

    void AddComplexModelDepthPrepass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddComplexModelPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddComplexModelEditorPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex, u32 drawCallDataID, bool isOpaque, u32 selectedRenderBatch, bool wireframeEntireObject);

    void RegisterLoadFromChunk(u16 chunkID, const Terrain::Chunk& chunk, StringTable& stringTable);
    void RegisterLoadFromDecoration(const std::string& modelPath, const u32& modelPathHash, vec3 position, quaternion rotation, f32 scale);
    void ExecuteLoad();

    void Clear();

    SafeVector<DrawCallData>& GetOpaqueDrawCallData() { return _opaqueDrawCallDatas; }
    SafeVector<DrawCallData>& GetTransparentDrawCallData() { return _transparentDrawCallDatas; }
    SafeVector<LoadedComplexModel>& GetLoadedComplexModels() { return _loadedComplexModels; }
    SafeVector<Instance>& GetInstances() { return _instances; }
    const Instance& GetInstance(size_t index) { return _instances.ReadGet(index); }
    const SafeVector<Terrain::PlacementDetails>& GetPlacementDetails() { return _complexModelPlacementDetails; }
    const SafeVector<CModel::CullingData>& GetCullingData() { return _cullingDatas; }

    void AddAnimationRequest(AnimationRequest request)
    {
        _animationRequests.enqueue(request);
    }
    u32 GetNumSequencesForModelId(u32 modelId)
    {
        return _animationModelInfo.ReadGet(modelId).numSequences;
    }

    u32 GetNumLoadedCModels() { return static_cast<u32>(_loadedComplexModels.Size()); }
    u32 GetNumCModelPlacements() { return static_cast<u32>(_instances.Size()); }
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
    u32 GetNumOpaqueSurvivingDrawCalls() { return _numOpaqueSurvivingDrawCalls; }
    u32 GetNumTransparentDrawCalls() { return static_cast<u32>(_transparentDrawCalls.Size()); }
    u32 GetNumTransparentSurvivingDrawCalls() { return _numTransparentSurvivingDrawCalls; }

    // Triangle stats
    u32 GetNumOpaqueTriangles() { return _numOpaqueTriangles; }
    u32 GetNumOpaqueSurvivingTriangles() { return _numOpaqueSurvivingTriangles; }
    u32 GetNumTransparentTriangles() { return _numTransparentTriangles; }
    u32 GetNumTransparentSurvivingTriangles() { return _numTransparentSurvivingTriangles; }

private:
    struct ComplexModelToBeLoaded
    {
        const Terrain::Placement* placement = nullptr;
        const std::string* name = nullptr;
        u32 nameHash = 0;
    };

    struct TextureUnit
    {
        u16 data = 0; // Texture Flag + Material Flag + Material Blending Mode
        u16 materialType = 0; // Shader ID
        u32 textureIds[2] = { CMODEL_INVALID_TEXTURE_ID, CMODEL_INVALID_TEXTURE_ID };
        u32 pad;
    };

    struct AnimationModelInfo
    {
        u16 numSequences = 0;
        u16 numBones = 0;

        u32 sequenceOffset = 0;
        u32 boneInfoOffset = 0;
        u32 padding = 0;
    };

    struct AnimationSequence
    {
        u16 animationId = 0;
        u16 animationSubId = 0;
        u16 nextSubAnimationId = 0;
        u16 nextAliasId = 0;

        u32 flags = 0;

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
        u32 shouldPrepareSort = false;
        u32 occlusionCull = false;
    };

private:
    void CreatePermanentResources();

    bool LoadComplexModel(ComplexModelToBeLoaded& complexModelToBeLoaded, LoadedComplexModel& complexModel);
    bool LoadFile(const std::string& cModelPathString, CModel::ComplexModel& cModel);

    bool IsRenderBatchTransparent(const CModel::ComplexRenderBatch& renderBatch, const CModel::ComplexModel& cModel);

    void AddInstance(LoadedComplexModel& complexModel, const Terrain::Placement& placement, u32& instanceIndex);

    void CreateBuffers();

private:
    Renderer::Renderer* _renderer;

    Renderer::SamplerID _sampler;
    Renderer::DescriptorSet _animationPrepassDescriptorSet;
    Renderer::DescriptorSet _cullingDescriptorSet;
    Renderer::DescriptorSet _sortingDescriptorSet;
    Renderer::DescriptorSet _passDescriptorSet;

    robin_hood::unordered_map<u32, u8> _uniqueIdCounter;
    std::shared_mutex _uniqueIdCounterMutex;

    SafeVector<Terrain::PlacementDetails> _complexModelPlacementDetails;

    SafeVector<ComplexModelToBeLoaded> _complexModelsToBeLoaded;
    SafeVector<LoadedComplexModel> _loadedComplexModels;
    SafeUnorderedMap<u32, u32> _nameHashToIndexMap;
    SafeUnorderedMap<u32, u32> _opaqueDrawCallDataIndexToLoadedModelIndex;
    SafeUnorderedMap<u32, u32> _transparentDrawCallDataIndexToLoadedModelIndex;

    SafeVector<CModel::ComplexVertex> _vertices;
    SafeVector<u16> _indices;
    SafeVector<TextureUnit> _textureUnits;
    SafeVector<Instance> _instances;
    SafeVector<BufferRangeFrame> _instanceBoneDeformRangeFrames;
    SafeVector<BufferRangeFrame> _instanceBoneInstanceRangeFrames;
    SafeVector<CModel::CullingData> _cullingDatas;

    SafeVector<AnimationSequence> _animationSequence;
    SafeVector<AnimationModelInfo> _animationModelInfo;
    SafeVector<AnimationBoneInfo> _animationBoneInfo;
    SafeVector<AnimationBoneInstance> _animationBoneInstances;

    std::vector<AnimationTrackInfo> _animationTrackInfo;
    std::vector<u32> _animationTrackTimestamps;
    std::vector<vec4> _animationTrackValues;
    std::mutex _animationTrackMutex;

    BufferRangeAllocator _animationBoneDeformRangeAllocator;
    BufferRangeAllocator _animationBoneInstancesRangeAllocator;
    moodycamel::ConcurrentQueue<AnimationRequest> _animationRequests;

    SafeVector<DrawCall> _opaqueDrawCalls;
    SafeVector<DrawCallData> _opaqueDrawCallDatas;

    SafeVector<DrawCall> _transparentDrawCalls;
    SafeVector<DrawCallData> _transparentDrawCallDatas;

    Renderer::BufferID _vertexBuffer;
    Renderer::BufferID _indexBuffer;
    Renderer::BufferID _textureUnitBuffer;
    Renderer::BufferID _instanceBuffer;
    Renderer::BufferID _cullingDataBuffer;
    Renderer::BufferID _visibleInstanceMaskBuffer;
    Renderer::BufferID _visibleInstanceCountBuffer;
    Renderer::BufferID _visibleInstanceIndexBuffer;
    Renderer::BufferID _visibleInstanceCountArgumentBuffer32;

    Renderer::BufferID _animationSequenceBuffer;
    Renderer::BufferID _animationModelInfoBuffer;
    Renderer::BufferID _animationBoneInfoBuffer;
    Renderer::BufferID _animationBoneDeformMatrixBuffer;
    Renderer::BufferID _animationBoneInstancesBuffer;
    Renderer::BufferID _animationTrackInfoBuffer;
    Renderer::BufferID _animationTrackTimestampBuffer;
    Renderer::BufferID _animationTrackValueBuffer;

    Renderer::BufferID _opaqueDrawCallBuffer;
    Renderer::BufferID _opaqueCulledDrawCallBuffer;
    Renderer::BufferID _opaqueDrawCallDataBuffer;
    Renderer::BufferID _opaqueDrawCountBuffer;
    Renderer::BufferID _opaqueDrawCountReadBackBuffer;
    Renderer::BufferID _opaqueTriangleCountBuffer;
    Renderer::BufferID _opaqueTriangleCountReadBackBuffer;

    Renderer::BufferID _transparentDrawCallBuffer;
    Renderer::BufferID _transparentCulledDrawCallBuffer;
    Renderer::BufferID _transparentSortedCulledDrawCallBuffer;
    Renderer::BufferID _transparentDrawCallDataBuffer;
    Renderer::BufferID _transparentDrawCountBuffer;
    Renderer::BufferID _transparentDrawCountReadBackBuffer;
    Renderer::BufferID _transparentTriangleCountBuffer;
    Renderer::BufferID _transparentTriangleCountReadBackBuffer;

    Renderer::BufferID _transparentSortKeys;
    Renderer::BufferID _transparentSortValues;

    CullConstants _cullConstants;

    Renderer::TextureArrayID _cModelTextures;

    u32 _numOpaqueSurvivingDrawCalls;
    u32 _numTransparentSurvivingDrawCalls;

    u32 _numOpaqueTriangles;
    u32 _numOpaqueSurvivingTriangles;
    u32 _numTransparentTriangles;
    u32 _numTransparentSurvivingTriangles;

    DebugRenderer* _debugRenderer;
};