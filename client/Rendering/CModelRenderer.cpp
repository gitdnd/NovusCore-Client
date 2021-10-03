#include "CModelRenderer.h"
#include "ClientRenderer.h"
#include "DebugRenderer.h"
#include "PixelQuery.h"
#include "../Utils/ServiceLocator.h"
#include "../Rendering/CModel/CModel.h"
#include "../Editor/Editor.h"
#include "SortUtils.h"

#include <filesystem>
#include <GLFW/glfw3.h>
#include <tracy/TracyVulkan.hpp>

#include <InputManager.h>
#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/RenderGraphBuilder.h>
#include <Utils/DebugHandler.h>
#include <Utils/FileReader.h>
#include <Utils/ByteBuffer.h>
#include <Utils/SafeVector.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>

#include <entt.hpp>
#include "../ECS/Components/Singletons/TimeSingleton.h"
#include "../ECS/Components/Singletons/NDBCSingleton.h"
#include "../ECS/Components/Singletons/TextureSingleton.h"

#include "Camera.h"
#include "../Gameplay/Map/Map.h"
#include "CVar/CVarSystem.h"

#define PARALLEL_LOADING 1

namespace fs = std::filesystem;

AutoCVar_Int CVAR_ComplexModelCullingEnabled("complexModels.cullEnable", "enable culling of complex models", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ComplexModelLockCullingFrustum("complexModels.lockCullingFrustum", "lock frustrum for complex model culling", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ComplexModelDrawBoundingBoxes("complexModels.drawBoundingBoxes", "draw bounding boxes for complex models", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ComplexModelOcclusionCullEnabled("complexModels.occlusionCullEnable", "enable culling of complex models", 1, CVarFlags::EditCheckbox);
AutoCVar_VecFloat CVAR_ComplexModelWireframeColor("complexModels.wireframeColor", "set the wireframe color for complex models", vec4(1.0f, 1.0f, 1.0f, 1.0f));

CModelRenderer::CModelRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    , _debugRenderer(debugRenderer)
{
    CreatePermanentResources();
}

CModelRenderer::~CModelRenderer()
{

}

void CModelRenderer::Update(f32 deltaTime)
{
    bool drawBoundingBoxes = CVAR_ComplexModelDrawBoundingBoxes.Get() == 1;
    if (drawBoundingBoxes)
    {
        _complexModelPlacementDetails.ReadLock([&](const std::vector<Terrain::PlacementDetails>& complexModelPlacementDetails)
        {
            for (const Terrain::PlacementDetails& placementDetails : complexModelPlacementDetails)
            {
                const Instance& instance = _instances.ReadGet(placementDetails.instanceIndex);
                const LoadedComplexModel& loadedComplexModel = _loadedComplexModels.ReadGet(placementDetails.loadedIndex);

                // Particle Emitters have no culling data
                if (loadedComplexModel.cullingDataID == std::numeric_limits<u32>().max())
                    continue;

                const CModel::CullingData& cullingData = _cullingDatas.ReadGet(loadedComplexModel.cullingDataID);

                vec3 minBoundingBox = cullingData.minBoundingBox;
                vec3 maxBoundingBox = cullingData.maxBoundingBox;

                vec3 center = (minBoundingBox + maxBoundingBox) * 0.5f;
                vec3 extents = maxBoundingBox - center;

                // transform center
                const mat4x4& m = instance.instanceMatrix;
                vec3 transformedCenter = vec3(m * vec4(center, 1.0f));

                // Transform extents (take maximum)
                glm::mat3x3 absMatrix = glm::mat3x3(glm::abs(vec3(m[0])), glm::abs(vec3(m[1])), glm::abs(vec3(m[2])));
                vec3 transformedExtents = absMatrix * extents;

                // Transform to min/max box representation
                vec3 transformedMin = transformedCenter - transformedExtents;
                vec3 transformedMax = transformedCenter + transformedExtents;

                _debugRenderer->DrawAABB3D(transformedMin, transformedMax, 0xff00ffff);
            }
        });
    }

    // Read back from the culling counters
    u32 numOpaqueDrawCalls = static_cast<u32>(_opaqueDrawCalls.Size());
    u32 numTransparentDrawCalls = static_cast<u32>(_transparentDrawCalls.Size());

    _numOpaqueSurvivingDrawCalls = numOpaqueDrawCalls;
    _numTransparentSurvivingDrawCalls = numTransparentDrawCalls;

    _numOpaqueSurvivingTriangles = _numOpaqueTriangles;
    _numTransparentSurvivingTriangles = _numTransparentTriangles;

    const bool cullingEnabled = CVAR_ComplexModelCullingEnabled.Get();
    if (cullingEnabled)
    {
        // Drawcalls
        {
            u32* count = static_cast<u32*>(_renderer->MapBuffer(_opaqueDrawCountReadBackBuffer));
            if (count != nullptr)
            {
                _numOpaqueSurvivingDrawCalls = *count;
            }
            _renderer->UnmapBuffer(_opaqueDrawCountReadBackBuffer);
        }

        {
            u32* count = static_cast<u32*>(_renderer->MapBuffer(_transparentDrawCountReadBackBuffer));
            if (count != nullptr)
            {
                _numTransparentSurvivingDrawCalls = *count;
            }
            _renderer->UnmapBuffer(_transparentDrawCountReadBackBuffer);
        }

        // Triangles
        {
            u32* count = static_cast<u32*>(_renderer->MapBuffer(_opaqueTriangleCountReadBackBuffer));
            if (count != nullptr)
            {
                _numOpaqueSurvivingTriangles = *count;
            }
            _renderer->UnmapBuffer(_opaqueTriangleCountReadBackBuffer);
        }

        {
            u32* count = static_cast<u32*>(_renderer->MapBuffer(_transparentTriangleCountReadBackBuffer));
            if (count != nullptr)
            {
                _numTransparentSurvivingTriangles = *count;
            }
            _renderer->UnmapBuffer(_transparentTriangleCountReadBackBuffer);
        }
    }
}

void CModelRenderer::AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    const u32 numInstances = static_cast<u32>(_instances.Size());
    if (numInstances == 0)
        return;

    const bool cullingEnabled = CVAR_ComplexModelCullingEnabled.Get();
    if (!cullingEnabled)
        return;

    const bool lockFrustum = CVAR_ComplexModelLockCullingFrustum.Get();

    struct CModelCullingPassData
    {
        Renderer::RenderPassMutableResource visibilityBuffer;
        Renderer::RenderPassMutableResource depth;
    };

    renderGraph->AddPass<CModelCullingPassData>("CModel Culling",
        [=](CModelCullingPassData& data, Renderer::RenderGraphBuilder& builder)
        {
            data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](CModelCullingPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, CModelCullingPass);

            Renderer::ComputePipelineDesc cullingPipelineDesc;
            graphResources.InitializePipelineDesc(cullingPipelineDesc);

            if (cullingEnabled && !lockFrustum)
            {
                Camera* camera = ServiceLocator::GetCamera();
                memcpy(_cullConstants.frustumPlanes, camera->GetFrustumPlanes(), sizeof(vec4[6]));
                _cullConstants.cameraPos = camera->GetPosition();
            }

            const u32 numOpaqueDrawCalls = static_cast<u32>(_opaqueDrawCalls.Size());
            const u32 numTransparentDrawCalls = static_cast<u32>(_transparentDrawCalls.Size());

            // clear visible instance counter
            if (numOpaqueDrawCalls > 0 || numTransparentDrawCalls > 0)
            {
                commandList.PushMarker("Clear instance visibility", Color::Grey);
                commandList.FillBuffer(_visibleInstanceCountBuffer, 0, sizeof(u32), 0);
                commandList.FillBuffer(_visibleInstanceMaskBuffer, 0, sizeof(u32) * ((numInstances + 31) / 32), 0);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _visibleInstanceCountBuffer);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _visibleInstanceMaskBuffer);
                commandList.PopMarker();
            }

            // Cull opaque
            if (numOpaqueDrawCalls > 0)
            {
                commandList.PushMarker("Opaque Culling", Color::Yellow);

                // Reset the counters
                commandList.FillBuffer(_opaqueDrawCountBuffer, 0, 4, 0);
                commandList.FillBuffer(_opaqueTriangleCountBuffer, 0, 4, 0);

                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _opaqueDrawCountBuffer);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _opaqueTriangleCountBuffer);

                Renderer::ComputeShaderDesc shaderDesc;
                shaderDesc.path = "cModelCulling.cs.hlsl";
                shaderDesc.AddPermutationField("PREPARE_SORT", "0");
                cullingPipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

                // Do culling
                Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(cullingPipelineDesc);
                commandList.BeginPipeline(pipeline);

                // Make a framelocal copy of our cull constants
                CullConstants* cullConstants = graphResources.FrameNew<CullConstants>();
                memcpy(cullConstants, &_cullConstants, sizeof(CullConstants));
                cullConstants->maxDrawCount = numOpaqueDrawCalls;
                cullConstants->occlusionCull = CVAR_ComplexModelOcclusionCullEnabled.Get();
                commandList.PushConstant(cullConstants, 0, sizeof(CullConstants));

                _opaqueCullingDescriptorSet.Bind("_depthPyramid"_h, resources.depthPyramid);

                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::CMODEL, &_opaqueCullingDescriptorSet, frameIndex);
                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);

                commandList.Dispatch((numOpaqueDrawCalls + 31) / 32, 1, 1);

                commandList.EndPipeline(pipeline);

                commandList.PopMarker();
            }
            else
            {
                // Reset the counter
                commandList.FillBuffer(_opaqueDrawCountBuffer, 0, 4, numOpaqueDrawCalls);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToIndirectArguments, _opaqueDrawCountBuffer);
            }

            // Cull transparent
            if (numTransparentDrawCalls > 0)
            {
                commandList.PushMarker("Transparent Culling", Color::Yellow);

                // Reset the counters
                commandList.FillBuffer(_transparentDrawCountBuffer, 0, 4, 0);
                commandList.FillBuffer(_transparentTriangleCountBuffer, 0, 4, 0);

                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _transparentDrawCountBuffer);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _transparentTriangleCountBuffer);

                // Do culling
                Renderer::ComputeShaderDesc shaderDesc;
                shaderDesc.path = "cModelCulling.cs.hlsl";
                shaderDesc.AddPermutationField("PREPARE_SORT", "0");
                cullingPipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

                Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(cullingPipelineDesc);
                commandList.BeginPipeline(pipeline);

                // Make a framelocal copy of our cull constants
                CullConstants* cullConstants = graphResources.FrameNew<CullConstants>();
                memcpy(cullConstants, &_cullConstants, sizeof(CullConstants));
                cullConstants->maxDrawCount = numTransparentDrawCalls;
                cullConstants->occlusionCull = CVAR_ComplexModelOcclusionCullEnabled.Get();
                commandList.PushConstant(cullConstants, 0, sizeof(CullConstants));

                _transparentCullingDescriptorSet.Bind("_depthPyramid"_h, resources.depthPyramid);

                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::CMODEL, &_transparentCullingDescriptorSet, frameIndex);
                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);

                commandList.Dispatch((numTransparentDrawCalls + 31) / 32, 1, 1);

                commandList.EndPipeline(pipeline);

                commandList.PopMarker();
            }
            else
            {
                // Reset the counter
                commandList.FillBuffer(_transparentDrawCountBuffer, 0, 4, numTransparentDrawCalls);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToIndirectArguments, _transparentDrawCountBuffer);
            }

            // Compact visible instance IDs
            {
                commandList.PushMarker("Visible Instance Compaction", Color::Grey);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToComputeShaderRead, _visibleInstanceMaskBuffer);

                Renderer::ComputePipelineDesc compactPipelineDesc;
                graphResources.InitializePipelineDesc(compactPipelineDesc);

                Renderer::ComputeShaderDesc shaderDesc;
                shaderDesc.path = "compactVisibleInstances.cs.hlsl";
                compactPipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

                Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(compactPipelineDesc);

                commandList.BeginPipeline(pipeline);
                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::CMODEL, &_compactDescriptorSet, frameIndex);
                commandList.Dispatch((numInstances + 31) / 32, 1, 1);
                commandList.EndPipeline(pipeline);

                commandList.PopMarker();
            }

            {
                commandList.PushMarker("Visible Instance Arguments", Color::Grey);

                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToComputeShaderRead, _visibleInstanceCountBuffer);

                Renderer::ComputePipelineDesc createArgumentsPipelineDesc;
                graphResources.InitializePipelineDesc(createArgumentsPipelineDesc);

                Renderer::ComputeShaderDesc shaderDesc;
                shaderDesc.path = "Utils/dispatchArguments1D.cs.hlsl";
                createArgumentsPipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

                Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(createArgumentsPipelineDesc);

                struct PushConstants
                {
                    u32 sourceByteOffset;
                    u32 targetByteOffset;
                    u32 threadGroupSize;
                };
                
                PushConstants* constants = graphResources.FrameNew<PushConstants>();
                constants->sourceByteOffset = 0;
                constants->targetByteOffset = 0;
                constants->threadGroupSize = 32;

                commandList.BeginPipeline(pipeline);
                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_DRAW, &_visibleInstanceArgumentDescriptorSet, frameIndex);
                commandList.PushConstant(constants, 0, sizeof(PushConstants));
                commandList.Dispatch(1, 1, 1);
                commandList.EndPipeline(pipeline);

                commandList.PopMarker();
            }
        });
}

void CModelRenderer::AddAnimationPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    const u32 numInstances = static_cast<u32>(_instances.Size());
    if (numInstances == 0)
        return;

    struct CModelAnimationPassData
    {
        Renderer::RenderPassMutableResource visibilityBuffer;
        Renderer::RenderPassMutableResource depth;
    };

    renderGraph->AddPass<CModelAnimationPassData>("CModel Animation",
        [=](CModelAnimationPassData& data, Renderer::RenderGraphBuilder& builder)
        {
            data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](CModelAnimationPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, CModelAnimationPass);

            if (_hasToResizeAnimationBoneDeformMatrixBuffer)
            {
                Renderer::BufferDesc desc;
                desc.name = "AnimationBoneDeformMatrixBuffer";
                desc.size = _newAnimationBoneDeformMatrixBufferSize;
                desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_SOURCE | Renderer::BufferUsage::TRANSFER_DESTINATION;

                Renderer::BufferID newBoneDeformMatrixBuffer = _renderer->CreateBuffer(desc);

                if (_animationBoneDeformMatrixBuffer != Renderer::BufferID::Invalid())
                {
                    commandList.QueueDestroyBuffer(_animationBoneDeformMatrixBuffer); 
                    commandList.CopyBuffer(newBoneDeformMatrixBuffer, 0, _animationBoneDeformMatrixBuffer, 0, _previousAnimationBoneDeformMatrixBufferSize);
                    commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, newBoneDeformMatrixBuffer);
                }

                _animationBoneDeformMatrixBuffer = newBoneDeformMatrixBuffer;
                _previousAnimationBoneDeformMatrixBufferSize = _newAnimationBoneDeformMatrixBufferSize;
                _hasToResizeAnimationBoneDeformMatrixBuffer = false;

                _animationPrepassDescriptorSet.Bind("_animationBoneDeformMatrices"_h, _animationBoneDeformMatrixBuffer);
                _geometryPassDescriptorSet.Bind("_cModelAnimationBoneDeformMatrices"_h, _animationBoneDeformMatrixBuffer);
                _materialPassDescriptorSet.Bind("_cModelAnimationBoneDeformMatrices"_h, _animationBoneDeformMatrixBuffer);
                _transparencyPassDescriptorSet.Bind("_cModelAnimationBoneDeformMatrices"_h, _animationBoneDeformMatrixBuffer);
            }

            if (_animationRequests.size_approx() > 0)
            {
                commandList.PushMarker("Animation Request", Color::White);

                _animationBoneInstances.WriteLock([&](std::vector<AnimationBoneInstance>& animationBoneInstances)
                {
                    _animationBoneInfo.ReadLock([&](const std::vector<AnimationBoneInfo>& animationBoneInfos)
                    {
                        AnimationRequest animationRequest;
                        while (_animationRequests.try_dequeue(animationRequest))
                        {
                            const Instance& instance = _instances.ReadGet(animationRequest.instanceId);

                            const LoadedComplexModel& complexModel = _loadedComplexModels.ReadGet(instance.modelId);
                            const AnimationModelInfo& modelInfo = _animationModelInfo.ReadGet(instance.modelId);
                
                            u32 sequenceIndex = animationRequest.sequenceId;
                            if (!complexModel.isAnimated)
                                continue;

                            for (u32 i = 0; i < modelInfo.numBones; i++)
                            {
                                const AnimationBoneInfo& animationBoneInfo = animationBoneInfos[modelInfo.boneInfoOffset + i];

                                for (u32 j = 0; j < animationBoneInfo.numTranslationSequences; j++)
                                {
                                    const AnimationTrackInfo& animationTrackInfo = _animationTrackInfo[animationBoneInfo.translationSequenceOffset + j];

                                    if (animationTrackInfo.sequenceIndex != sequenceIndex)
                                        continue;

                                    AnimationBoneInstance& boneInstance = animationBoneInstances[instance.boneInstanceDataOffset + i];
                                    boneInstance.animationProgress = 0;

                                    if (animationRequest.flags.isPlaying)
                                    {
                                        bool animationIsLooping = animationRequest.flags.isLooping || animationBoneInfo.flags.isTranslationTrackGlobalSequence;

                                        boneInstance.animateState = (AnimationBoneInstance::AnimateState::PLAY_ONCE * !animationIsLooping) + (AnimationBoneInstance::AnimateState::PLAY_LOOP * animationIsLooping);
                                        boneInstance.sequenceIndex = sequenceIndex;
                                    }
                                    else
                                    {
                                        boneInstance.animateState = AnimationBoneInstance::AnimateState::STOPPED;
                                        boneInstance.sequenceIndex = 0;
                                    }

                                    _animationBoneInstances.SetDirtyElement(instance.boneInstanceDataOffset + i);
                                    break;
                                }

                                for (u32 j = 0; j < animationBoneInfo.numRotationSequences; j++)
                                {
                                    const AnimationTrackInfo& animationTrackInfo = _animationTrackInfo[animationBoneInfo.rotationSequenceOffset + j];

                                    if (animationTrackInfo.sequenceIndex != sequenceIndex)
                                        continue;

                                    AnimationBoneInstance& boneInstance = animationBoneInstances[instance.boneInstanceDataOffset + i];
                                    boneInstance.animationProgress = 0;

                                    if (animationRequest.flags.isPlaying)
                                    {
                                        bool animationIsLooping = animationRequest.flags.isLooping || animationBoneInfo.flags.isRotationTrackGlobalSequence;

                                        boneInstance.animateState = (AnimationBoneInstance::AnimateState::PLAY_ONCE * !animationIsLooping) + (AnimationBoneInstance::AnimateState::PLAY_LOOP * animationIsLooping);
                                        boneInstance.sequenceIndex = sequenceIndex;
                                    }
                                    else
                                    {
                                        boneInstance.animateState = AnimationBoneInstance::AnimateState::STOPPED;
                                        boneInstance.sequenceIndex = 0;
                                    }

                                    _animationBoneInstances.SetDirtyElement(instance.boneInstanceDataOffset + i);
                                    break;
                                }

                                for (u32 j = 0; j < animationBoneInfo.numScaleSequences; j++)
                                {
                                    const AnimationTrackInfo& animationTrackInfo = _animationTrackInfo[animationBoneInfo.scaleSequenceOffset + j];

                                    if (animationTrackInfo.sequenceIndex != sequenceIndex)
                                        continue;

                                    AnimationBoneInstance& boneInstance = animationBoneInstances[instance.boneInstanceDataOffset + i];
                                    boneInstance.animationProgress = 0;

                                    if (animationRequest.flags.isPlaying)
                                    {
                                        bool animationIsLooping = animationRequest.flags.isLooping || animationBoneInfo.flags.isScaleTrackGlobalSequence;

                                        boneInstance.animateState = (AnimationBoneInstance::AnimateState::PLAY_ONCE * !animationIsLooping) + (AnimationBoneInstance::AnimateState::PLAY_LOOP * animationIsLooping);
                                        boneInstance.sequenceIndex = sequenceIndex;
                                    }
                                    else
                                    {
                                        boneInstance.animateState = AnimationBoneInstance::AnimateState::STOPPED;
                                        boneInstance.sequenceIndex = 0;
                                    }

                                    _animationBoneInstances.SetDirtyElement(instance.boneInstanceDataOffset + i);
                                    break;
                                }
                            }
                        }
                    });
                });

                commandList.PopMarker();

                bool didResize = _animationBoneInstances.SyncToGPU(_renderer, &commandList);
                if (didResize)
                {
                    _animationPrepassDescriptorSet.Bind("_animationBoneInstances"_h, _animationBoneInstances.GetBuffer());
                }
            }

            // Add pipeline barriers for the animationBoneInstances buffer
            {
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferDest, _animationBoneInstances.GetBuffer());
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _animationBoneInstances.GetBuffer());
            }

            const u32 numOpaqueDrawCalls = static_cast<u32>(_opaqueDrawCalls.Size());
            const u32 numTransparentDrawCalls = static_cast<u32>(_transparentDrawCalls.Size());

            // Set Animation Prepass Pipeline
            if (numOpaqueDrawCalls > 0 || numTransparentDrawCalls > 0)
            {
                commandList.PushMarker("Animation Prepass", Color::Cyan);

                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToComputeShaderRead, _visibleInstanceIndexBuffer);

                Renderer::ComputePipelineDesc animationprepassPipelineDesc;
                graphResources.InitializePipelineDesc(animationprepassPipelineDesc);

                {
                    Renderer::ComputeShaderDesc shaderDesc;
                    shaderDesc.path = "CModelAnimationPrepass.cs.hlsl";
                    animationprepassPipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);
                }

                Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(animationprepassPipelineDesc);
                commandList.BeginPipeline(pipeline);

                entt::registry* registry = ServiceLocator::GetGameRegistry();
                TimeSingleton& timeSingleton = registry->ctx<TimeSingleton>();

                struct AnimationConstants
                {
                    u32 numInstances;
                    f32 deltaTime;
                };

                AnimationConstants* deltaTimeConstant = graphResources.FrameNew<AnimationConstants>();
                {
                    deltaTimeConstant->numInstances = numInstances;
                    deltaTimeConstant->deltaTime = timeSingleton.deltaTime;

                    commandList.PushConstant(deltaTimeConstant, 0, sizeof(AnimationConstants));
                }

                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::DEBUG, &resources.debugDescriptorSet, frameIndex);
                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::CMODEL, &_animationPrepassDescriptorSet, frameIndex);

                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _visibleInstanceCountArgumentBuffer32);
                commandList.DispatchIndirect(_visibleInstanceCountArgumentBuffer32, 0);

                commandList.EndPipeline(pipeline);

                commandList.PopMarker();
            }
        });
}

void CModelRenderer::AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    const u32 numInstances = static_cast<u32>(_instances.Size());
    if (numInstances == 0)
        return;

    const bool cullingEnabled = CVAR_ComplexModelCullingEnabled.Get();

    struct CModelGeometryPassData
    {
        Renderer::RenderPassMutableResource visibilityBuffer;
        Renderer::RenderPassMutableResource depth;
    };

    renderGraph->AddPass<CModelGeometryPassData>("CModel Geometry",
        [=](CModelGeometryPassData& data, Renderer::RenderGraphBuilder& builder)
        {
            data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](CModelGeometryPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, CModelGeometryPass);

            commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToVertexShaderRead, _animationBoneDeformMatrixBuffer);
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToPixelShaderRead, _animationBoneDeformMatrixBuffer);

            Renderer::GraphicsPipelineDesc pipelineDesc;
            graphResources.InitializePipelineDesc(pipelineDesc);

            // Shaders
            Renderer::VertexShaderDesc vertexShaderDesc;
            vertexShaderDesc.path = "cModel.vs.hlsl";
            vertexShaderDesc.AddPermutationField("EDITOR_PASS", "0");

            pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

            Renderer::PixelShaderDesc pixelShaderDesc;
            pixelShaderDesc.path = "cModel.ps.hlsl";
            pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

            // Depth state
            pipelineDesc.states.depthStencilState.depthEnable = true;
            pipelineDesc.states.depthStencilState.depthWriteEnable = true;
            pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

            // Rasterizer state
            pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
            pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

            // Render targets
            pipelineDesc.renderTargets[0] = data.visibilityBuffer;
            pipelineDesc.depthStencil = data.depth;

            const u32 numOpaqueDrawCalls = static_cast<u32>(_opaqueDrawCalls.Size());

            if (cullingEnabled)
            {
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _opaqueCulledDrawCallBuffer);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _opaqueDrawCountBuffer);
            }
            else
            {
                // Reset the counters
                commandList.FillBuffer(_opaqueDrawCountBuffer, 0, 4, numOpaqueDrawCalls);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToIndirectArguments, _opaqueDrawCountBuffer);
            }

            // Set Opaque Pipeline
            if (numOpaqueDrawCalls > 0)
            {
                commandList.PushMarker("Opaque " + std::to_string(numOpaqueDrawCalls), Color::White);

                // Draw
                Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
                commandList.BeginPipeline(pipeline);

                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);

                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::CMODEL, &_geometryPassDescriptorSet, frameIndex);

                commandList.SetIndexBuffer(_indices.GetBuffer(), Renderer::IndexFormat::UInt16);

                Renderer::BufferID argumentBuffer = (cullingEnabled) ? _opaqueCulledDrawCallBuffer : _opaqueDrawCalls.GetBuffer();
                commandList.DrawIndexedIndirectCount(argumentBuffer, 0, _opaqueDrawCountBuffer, 0, numOpaqueDrawCalls);

                commandList.EndPipeline(pipeline);

                // Copy from our draw count buffer to the readback buffer
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, _opaqueDrawCountBuffer);
                commandList.CopyBuffer(_opaqueDrawCountReadBackBuffer, 0, _opaqueDrawCountBuffer, 0, 4);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, _opaqueDrawCountReadBackBuffer);

                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToTransferSrc, _opaqueTriangleCountBuffer);
                commandList.CopyBuffer(_opaqueTriangleCountReadBackBuffer, 0, _opaqueTriangleCountBuffer, 0, 4);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToTransferSrc, _opaqueTriangleCountReadBackBuffer);

                commandList.PopMarker();
            }

            // We skip transparencies since they don't get rendered through visibility buffers
        });
}

void CModelRenderer::AddEditorPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    const u32 numInstances = static_cast<u32>(_instances.Size());
    if (numInstances == 0)
        return;

    Editor::Editor* editor = ServiceLocator::GetEditor();
    if (!editor->HasSelectedObject())
        return;
    
    u32 activeToken = editor->GetActiveToken();

    ClientRenderer* clientRenderer = ServiceLocator::GetClientRenderer();
    PixelQuery* pixelQuery = clientRenderer->GetPixelQuery();

    PixelQuery::PixelData pixelData;

    if (!pixelQuery->GetQueryResult(activeToken, pixelData))
        return;
    
    if (pixelData.type != Editor::QueryObjectType::ComplexModelOpaque) // Transparent mapobjects won't show up in the visibility buffer so they are TODO
        return;
    
    const Editor::Editor::SelectedComplexModelData& selectedComplexModelData = editor->GetSelectedComplexModelData();
    if (!selectedComplexModelData.drawWireframe)
        return;
    
    u32 drawCallDataID = pixelData.value;
    u32 selectedRenderBatch = selectedComplexModelData.selectedRenderBatch - 1;
    
    struct CModelPassData
    {
        Renderer::RenderPassMutableResource color;
        Renderer::RenderPassMutableResource depth;
    };

    // Read back from the culling counters
    renderGraph->AddPass<CModelPassData>("CModel Editor",
        [=](CModelPassData& data, Renderer::RenderGraphBuilder& builder)
        {
            data.color = builder.Write(resources.resolvedColor, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](CModelPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, CModelEditorPass);

            Renderer::GraphicsPipelineDesc pipelineDesc;
            graphResources.InitializePipelineDesc(pipelineDesc);

            // Shaders
            Renderer::VertexShaderDesc vertexShaderDesc;
            vertexShaderDesc.path = "cModel.vs.hlsl";
            vertexShaderDesc.AddPermutationField("EDITOR_PASS", "1");

            pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

            Renderer::PixelShaderDesc pixelShaderDesc;
            pixelShaderDesc.path = "solidColor.ps.hlsl";

            pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

            // Depth state
            pipelineDesc.states.depthStencilState.depthEnable = false;
            pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER_EQUAL;

            // Rasterizer state
            pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
            pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;
            pipelineDesc.states.rasterizerState.fillMode = Renderer::FillMode::WIREFRAME;
            // Render targets
            pipelineDesc.renderTargets[0] = data.color;
            pipelineDesc.depthStencil = data.depth;

            struct ColorConstant
            {
                vec4 value;
            };

            commandList.PushMarker("Opaque Editor" + std::to_string(selectedRenderBatch), Color::White);

            // Draw
            Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
            commandList.BeginPipeline(pipeline);

            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::CMODEL, &_geometryPassDescriptorSet, frameIndex);

            ColorConstant* colorConstant = graphResources.FrameNew<ColorConstant>();
            colorConstant->value = CVAR_ComplexModelWireframeColor.Get();
            commandList.PushConstant(colorConstant, 0, sizeof(ColorConstant));

            commandList.SetIndexBuffer(_indices.GetBuffer(), Renderer::IndexFormat::UInt16);

            const SafeVector<CModelRenderer::DrawCallData>& drawCallDatas = GetOpaqueDrawCallData();
            const CModelRenderer::DrawCallData& drawCallData = drawCallDatas.ReadGet(drawCallDataID);

            const CModelRenderer::Instance& instance = _instances.ReadGet(drawCallData.instanceID);
            const CModelRenderer::LoadedComplexModel& loadedComplexModel = _loadedComplexModels.ReadGet(instance.modelId);

            u32 numDrawCalls = static_cast<u32>(loadedComplexModel.opaqueDrawCallTemplates.size());

            if (numDrawCalls)
            {
                if (selectedComplexModelData.wireframeEntireObject)
                {
                    for (u32 i = 0; i < numDrawCalls; i++)
                    {
                        const CModelRenderer::DrawCall& drawCall = loadedComplexModel.opaqueDrawCallTemplates[i];

                        u32 vertexOffset = drawCall.vertexOffset;
                        u32 firstIndex = drawCall.firstIndex;
                        u32 indexCount = drawCall.indexCount;

                        commandList.DrawIndexed(indexCount, 1, firstIndex, vertexOffset, drawCallDataID);
                    }
                }
                else
                {
                    const CModelRenderer::DrawCall& drawCall = loadedComplexModel.opaqueDrawCallTemplates[selectedRenderBatch];

                    u32 vertexOffset = drawCall.vertexOffset;
                    u32 firstIndex = drawCall.firstIndex;
                    u32 indexCount = drawCall.indexCount;

                    commandList.DrawIndexed(indexCount, 1, firstIndex, vertexOffset, drawCallDataID);
                }
            }

            commandList.EndPipeline(pipeline);
            commandList.PopMarker();
        });
}

void CModelRenderer::AddTransparencyPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    const u32 numInstances = static_cast<u32>(_instances.Size());
    if (numInstances == 0)
        return;

    const bool cullingEnabled = CVAR_ComplexModelCullingEnabled.Get();

    struct CModelTransparencyPassData
    {
        Renderer::RenderPassMutableResource transparency;
        Renderer::RenderPassMutableResource transparencyWeights;
        Renderer::RenderPassMutableResource depth;
    };

    renderGraph->AddPass<CModelTransparencyPassData>("CModel OIT Transparency",
        [=](CModelTransparencyPassData& data, Renderer::RenderGraphBuilder& builder)
        {
            data.transparency = builder.Write(resources.transparency, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::CLEAR);
            data.transparencyWeights = builder.Write(resources.transparencyWeights, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::CLEAR);
            data.depth = builder.Write(resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](CModelTransparencyPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, CModelOITTransparencyPass);

            Renderer::GraphicsPipelineDesc pipelineDesc;
            graphResources.InitializePipelineDesc(pipelineDesc);

            // Shaders
            Renderer::VertexShaderDesc vertexShaderDesc;
            vertexShaderDesc.path = "cModelTransparency.vs.hlsl";
            pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

            Renderer::PixelShaderDesc pixelShaderDesc;
            pixelShaderDesc.path = "cModelTransparency.ps.hlsl";
            pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

            // Depth state
            pipelineDesc.states.depthStencilState.depthEnable = true;
            pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

            // Rasterizer state
            pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
            pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

            // Blend state
            pipelineDesc.states.blendState.independentBlendEnable = true;

            pipelineDesc.states.blendState.renderTargets[0].blendEnable = true;
            pipelineDesc.states.blendState.renderTargets[0].blendOp = Renderer::BlendOp::ADD;
            pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::ONE;
            pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::ONE;
            pipelineDesc.states.blendState.renderTargets[0].srcBlendAlpha = Renderer::BlendMode::ONE;
            pipelineDesc.states.blendState.renderTargets[0].destBlendAlpha = Renderer::BlendMode::ONE;
            pipelineDesc.states.blendState.renderTargets[0].blendOpAlpha = Renderer::BlendOp::ADD;

            pipelineDesc.states.blendState.renderTargets[1].blendEnable = true;
            pipelineDesc.states.blendState.renderTargets[1].blendOp = Renderer::BlendOp::ADD;
            pipelineDesc.states.blendState.renderTargets[1].srcBlend = Renderer::BlendMode::ZERO;
            pipelineDesc.states.blendState.renderTargets[1].destBlend = Renderer::BlendMode::INV_SRC_COLOR;
            pipelineDesc.states.blendState.renderTargets[1].srcBlendAlpha = Renderer::BlendMode::ZERO;
            pipelineDesc.states.blendState.renderTargets[1].destBlendAlpha = Renderer::BlendMode::INV_SRC_ALPHA;
            pipelineDesc.states.blendState.renderTargets[1].blendOpAlpha = Renderer::BlendOp::ADD;

            // Render targets
            pipelineDesc.renderTargets[0] = data.transparency;
            pipelineDesc.renderTargets[1] = data.transparencyWeights;
            pipelineDesc.depthStencil = data.depth;

            const u32 numTransparentDrawCalls = static_cast<u32>(_transparentDrawCalls.Size());

            if (cullingEnabled)
            {
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _transparentCulledDrawCallBuffer);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _transparentDrawCountBuffer);
            }
            else
            {
                // Reset the counters
                commandList.FillBuffer(_transparentDrawCountBuffer, 0, 4, numTransparentDrawCalls);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToIndirectArguments, _transparentDrawCountBuffer);
            }

            // Set Transparent Pipeline
            if (numTransparentDrawCalls > 0)
            {
                commandList.PushMarker("Transparent " + std::to_string(numTransparentDrawCalls), Color::White);

                // Draw
                Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
                commandList.BeginPipeline(pipeline);

                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);

                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::CMODEL, &_transparencyPassDescriptorSet, frameIndex);

                commandList.SetIndexBuffer(_indices.GetBuffer(), Renderer::IndexFormat::UInt16);

                Renderer::BufferID argumentBuffer = (cullingEnabled) ? _transparentCulledDrawCallBuffer : _transparentDrawCalls.GetBuffer();
                commandList.DrawIndexedIndirectCount(argumentBuffer, 0, _transparentDrawCountBuffer, 0, numTransparentDrawCalls);

                commandList.EndPipeline(pipeline);

                // Copy from our draw count buffer to the readback buffer
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, _transparentDrawCountBuffer);
                commandList.CopyBuffer(_transparentDrawCountReadBackBuffer, 0, _transparentDrawCountBuffer, 0, 4);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, _transparentDrawCountReadBackBuffer);

                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToTransferSrc, _transparentTriangleCountBuffer);
                commandList.CopyBuffer(_transparentTriangleCountReadBackBuffer, 0, _transparentTriangleCountBuffer, 0, 4);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToTransferSrc, _transparentTriangleCountReadBackBuffer);

                commandList.PopMarker();
            }
        });
}

void CModelRenderer::RegisterLoadFromChunk(u16 chunkID, const Terrain::Chunk& chunk, StringTable& stringTable)
{
    for (const Terrain::Placement& placement : chunk.complexModelPlacements)
    {
        u32 uniqueID = placement.uniqueID;

        {
            std::unique_lock lock(_uniqueIdCounterMutex);
            if (_uniqueIdCounter[uniqueID]++ == 0)
            {
                ComplexModelToBeLoaded& modelToBeLoaded = _complexModelsToBeLoaded.EmplaceBack();
                modelToBeLoaded.placement = &placement;
                modelToBeLoaded.name = &stringTable.GetString(placement.nameID);
                modelToBeLoaded.nameHash = stringTable.GetStringHash(placement.nameID);
            }
        }
    }
}

void CModelRenderer::RegisterLoadFromDecoration(const std::string& modelPath, const u32& modelPathHash, vec3 position, quaternion rotation, f32 scale)
{
    ComplexModelToBeLoaded& modelToBeLoaded = _complexModelsToBeLoaded.EmplaceBack();

    Terrain::Placement* placement = new Terrain::Placement();
    placement->position = position;
    placement->rotation = rotation;
    placement->scale = static_cast<u16>(scale * 1024);

    modelToBeLoaded.placement = placement;
    modelToBeLoaded.name = new std::string(modelPath);
    modelToBeLoaded.nameHash = modelPathHash;
}

void CModelRenderer::ExecuteLoad()
{
    ZoneScopedN("CModelRenderer::ExecuteLoad()");

    _numTotalAnimatedVertices = 0;
    std::atomic<size_t> numComplexModelsToLoad = 0;
    
    _animationBoneDeformRangeAllocator.Reset();

    _complexModelsToBeLoaded.WriteLock([&](std::vector<ComplexModelToBeLoaded>& complexModelsToBeLoaded)
    {
        size_t numComplexModelsToBeLoaded = complexModelsToBeLoaded.size();

        _loadedComplexModels.WriteLock([&](std::vector<LoadedComplexModel>& loadedComplexModels)
        {
            loadedComplexModels.reserve(numComplexModelsToBeLoaded);
        });

        _instances.WriteLock([&](std::vector<Instance>& instances)
        {
            instances.reserve(numComplexModelsToBeLoaded);
        });

        _animationModelInfo.WriteLock([&](std::vector<AnimationModelInfo>& animationModelInfo)
        {
            animationModelInfo.reserve(numComplexModelsToBeLoaded);
        });

        _complexModelPlacementDetails.WriteLock([&](std::vector<Terrain::PlacementDetails>& complexModelPlacementDetails)
        {
            complexModelPlacementDetails.reserve(numComplexModelsToBeLoaded);
        });

#if PARALLEL_LOADING
        tf::Taskflow tf;
        tf.parallel_for(complexModelsToBeLoaded.begin(), complexModelsToBeLoaded.end(), [&](ComplexModelToBeLoaded& modelToBeLoaded)
#else
        for (ComplexModelToBeLoaded& modelToBeLoaded : complexModelsToBeLoaded)
#endif // PARALLEL_LOAD
        {
            ZoneScoped;
            ZoneText(modelToBeLoaded.name->c_str(), modelToBeLoaded.name->length());

            // Placements reference a path to a ComplexModel, several placements can reference the same object
            // Because of this we want only the first load to actually load the object, subsequent loads should reuse the loaded version
            u32 modelID;
            LoadedComplexModel* complexModel = nullptr;

            bool shouldLoad = false;
            _nameHashToIndexMap.WriteLock([&](robin_hood::unordered_map<u32, u32>& nameHashToIndexMap)
            {
                // See if anything has already loaded this one
                auto it = nameHashToIndexMap.find(modelToBeLoaded.nameHash);
                if (it == nameHashToIndexMap.end())
                {
                    // If it hasn't, we should load it
                    shouldLoad = true;

                    _loadedComplexModels.WriteLock([&](std::vector<LoadedComplexModel>& loadedComplexModels)
                    {
                        modelID = static_cast<u32>(loadedComplexModels.size());
                        complexModel = &loadedComplexModels.emplace_back();
                    });

                    nameHashToIndexMap[modelToBeLoaded.nameHash] = modelID;
                }
                else
                {
                    _loadedComplexModels.WriteLock([&](std::vector<LoadedComplexModel>& loadedComplexModels)
                    {
                        modelID = it->second;
                        complexModel = &loadedComplexModels[it->second];
                    });
                }
            });

            std::scoped_lock lock(complexModel->mutex);

            if (shouldLoad)
            {
                complexModel->objectID = modelID;
                if (!LoadComplexModel(modelToBeLoaded, *complexModel))
                {
                    complexModel->failedToLoad = true;
                    DebugHandler::PrintError("Failed to load Complex Model: %s", complexModel->debugName.c_str());
                }
            }

            if (complexModel->failedToLoad)
            {
                //DebugHandler::PrintWarning("Failed to Add Instance of Complex Model: %s", complexModel->debugName.c_str());

#if PARALLEL_LOADING
                return;
#else // PARALLEL_LOADING
                continue;
#endif // PARALLEL_LOADING
            }

            // Add Placement Details (This is used to go from a placement to LoadedComplexModel or InstanceData
            Terrain::PlacementDetails& placementDetails = _complexModelPlacementDetails.EmplaceBack();
            placementDetails.loadedIndex = modelID;

            // Add placement as an instance
            AddInstance(*complexModel, *modelToBeLoaded.placement, placementDetails.instanceIndex);

            numComplexModelsToLoad++;
        }
#if PARALLEL_LOADING
        );
        tf.wait_for_all();
#endif // PARALLEL_LOADING
    });

    _complexModelsToBeLoaded.Clear();

    if (numComplexModelsToLoad == 0)
        return;

    {
        ZoneScopedN("CModelRenderer::ExecuteLoad()::CreateBuffers()");
        CreateBuffers();

        // Calculate triangles
        _numOpaqueTriangles = 0;
        _numTransparentTriangles = 0;

        _opaqueDrawCalls.ReadLock([&](const std::vector<DrawCall>& opaqueDrawCalls)
        {
            for (const DrawCall& drawCall : opaqueDrawCalls)
            {
                _numOpaqueTriangles += drawCall.indexCount / 3;
            }
        });
        
        _transparentDrawCalls.ReadLock([&](const std::vector<DrawCall>& transparentDrawCalls)
        {
            for (const DrawCall& drawCall : transparentDrawCalls)
            {
                _numTransparentTriangles += drawCall.indexCount / 3;
            }
        });
    }
}

void CModelRenderer::Clear()
{
    {
        std::unique_lock lock(_uniqueIdCounterMutex);
        _uniqueIdCounter.clear();
    }
    
    _complexModelPlacementDetails.Clear();
    _loadedComplexModels.Clear();
    _nameHashToIndexMap.Clear();
    _opaqueDrawCallDataIndexToLoadedModelIndex.Clear();
    _transparentDrawCallDataIndexToLoadedModelIndex.Clear();

    _vertices.Clear();
    _indices.Clear();
    _textureUnits.Clear();
    _instances.Clear();
    _cullingDatas.Clear();

    _animationSequences.Clear();
    _animationModelInfo.Clear();
    _animationBoneInfo.Clear();
    _animationTrackInfo.clear();
    _animationTrackTimestamps.clear();
    _animationTrackValues.clear();
    _animationBoneInstances.Clear();

    _opaqueDrawCalls.Clear();
    _opaqueDrawCallDatas.Clear();

    _transparentDrawCalls.Clear();
    _transparentDrawCallDatas.Clear();

    _renderer->UnloadTexturesInArray(_cModelTextures, 0);
}

void CModelRenderer::CreatePermanentResources()
{
    Renderer::TextureArrayDesc textureArrayDesc;
    textureArrayDesc.size = 4096;

    _cModelTextures = _renderer->CreateTextureArray(textureArrayDesc);
    _geometryPassDescriptorSet.Bind("_cModelTextures"_h, _cModelTextures);
    _materialPassDescriptorSet.Bind("_cModelTextures"_h, _cModelTextures);
    _transparencyPassDescriptorSet.Bind("_cModelTextures"_h, _cModelTextures);

    Renderer::SamplerDesc samplerDesc;
    samplerDesc.enabled = true;
    samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
    samplerDesc.addressU = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressV = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;

    _sampler = _renderer->CreateSampler(samplerDesc);
    _geometryPassDescriptorSet.Bind("_sampler"_h, _sampler);
    _transparencyPassDescriptorSet.Bind("_sampler"_h, _sampler);

    Renderer::SamplerDesc occlusionSamplerDesc;
    occlusionSamplerDesc.filter = Renderer::SamplerFilter::MINIMUM_MIN_MAG_MIP_LINEAR;

    occlusionSamplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    occlusionSamplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    occlusionSamplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    occlusionSamplerDesc.minLOD = 0.f;
    occlusionSamplerDesc.maxLOD = 16.f;
    occlusionSamplerDesc.mode = Renderer::SamplerReductionMode::MIN;

    _occlusionSampler = _renderer->CreateSampler(occlusionSamplerDesc);
    _opaqueCullingDescriptorSet.Bind("_depthSampler"_h, _occlusionSampler);
    _transparentCullingDescriptorSet.Bind("_depthSampler"_h, _occlusionSampler);

    // Create OpaqueDrawCountBuffer
    {
        Renderer::BufferDesc desc;
        desc.name = "CModelOpaqueDrawCountBuffer";
        desc.size = sizeof(u32);
        desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;
        _opaqueDrawCountBuffer = _renderer->CreateBuffer(_opaqueDrawCountBuffer, desc);

        _opaqueCullingDescriptorSet.Bind("_drawCount"_h, _opaqueDrawCountBuffer);

        desc.name = "CModelOpaqueDrawCountRBBuffer";
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        desc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _opaqueDrawCountReadBackBuffer = _renderer->CreateBuffer(_opaqueDrawCountReadBackBuffer, desc);
    }

    // Create TransparentDrawCountBuffer
    {
        Renderer::BufferDesc desc;
        desc.name = "CModelTransparentDrawCountBuffer";
        desc.size = sizeof(u32);
        desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;
        _transparentDrawCountBuffer = _renderer->CreateBuffer(_transparentDrawCountBuffer, desc);

        _transparentCullingDescriptorSet.Bind("_drawCount"_h, _transparentDrawCountBuffer);

        desc.name = "CModelTransparentDrawCountRBBuffer";
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        desc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _transparentDrawCountReadBackBuffer = _renderer->CreateBuffer(_transparentDrawCountReadBackBuffer, desc);
    }

    // Create OpaqueTriangleCountReadBackBuffer
    {
        Renderer::BufferDesc desc;
        desc.name = "CModelOpaqueTriangleCountBuffer";
        desc.size = sizeof(u32);
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;
        _opaqueTriangleCountBuffer = _renderer->CreateBuffer(_opaqueTriangleCountBuffer, desc);

        _opaqueCullingDescriptorSet.Bind("_triangleCount"_h, _opaqueTriangleCountBuffer);

        desc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _opaqueTriangleCountReadBackBuffer = _renderer->CreateBuffer(_opaqueTriangleCountReadBackBuffer, desc);
    }

    // Create TransparentTriangleCountReadBackBuffer
    {
        Renderer::BufferDesc desc;
        desc.name = "CModelTransparentTriangleCountBuffer";
        desc.size = sizeof(u32);
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;
        _transparentTriangleCountBuffer = _renderer->CreateBuffer(_transparentTriangleCountBuffer, desc);

        _transparentCullingDescriptorSet.Bind("_triangleCount"_h, _transparentTriangleCountBuffer);

        desc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _transparentTriangleCountReadBackBuffer = _renderer->CreateBuffer(_transparentTriangleCountReadBackBuffer, desc);
    }

    // Create AnimationBoneDeformMatrixBuffer
    {
        size_t boneDeformMatrixBufferSize = (sizeof(mat4x4) * 255) * 1000;
        _previousAnimationBoneDeformMatrixBufferSize = boneDeformMatrixBufferSize;

        Renderer::BufferDesc desc;
        desc.name = "AnimationBoneDeformMatrixBuffer";
        desc.size = boneDeformMatrixBufferSize;
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_SOURCE | Renderer::BufferUsage::TRANSFER_DESTINATION;
        _animationBoneDeformMatrixBuffer = _renderer->CreateBuffer(_animationBoneDeformMatrixBuffer, desc);

        _animationPrepassDescriptorSet.Bind("_animationBoneDeformMatrices"_h, _animationBoneDeformMatrixBuffer);
        _geometryPassDescriptorSet.Bind("_cModelAnimationBoneDeformMatrices"_h, _animationBoneDeformMatrixBuffer);
        _materialPassDescriptorSet.Bind("_cModelAnimationBoneDeformMatrices"_h, _animationBoneDeformMatrixBuffer);
        _transparencyPassDescriptorSet.Bind("_cModelAnimationBoneDeformMatrices"_h, _animationBoneDeformMatrixBuffer);

        _animationBoneDeformRangeAllocator.Init(0, boneDeformMatrixBufferSize);
    }

    _animationBoneInstances.SetDebugName("animationBoneInstances");
    _animationBoneInstances.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    CreateBuffers();
}

bool CModelRenderer::LoadComplexModel(ComplexModelToBeLoaded& toBeLoaded, LoadedComplexModel& complexModel)
{
    const std::string& modelPath = *toBeLoaded.name;
    complexModel.debugName = modelPath;

    // This needs to run before LoadFile until we have a proper fix for LoadFile failing
    AnimationModelInfo& animationModelInfo = _animationModelInfo.EmplaceBack();

    CModel::ComplexModel cModel;
    cModel.name = complexModel.debugName.data();
    fs::path modelTexturePath = "Data/extracted/Textures/" + modelPath;
    if (!LoadFile(modelPath, cModel))
        return false;

    vec3 minBounding = cModel.cullingData.minBoundingBox;
    vec3 maxBounding = cModel.cullingData.maxBoundingBox;
    entt::registry* registry = ServiceLocator::GetGameRegistry();
    TextureSingleton& textureSingleton = registry->ctx<TextureSingleton>();

    // Add Sequences
    {
        _animationSequences.WriteLock([&](std::vector<AnimationSequence>& animationSequence)
        {
            size_t numSequenceInfoBefore = animationSequence.size();
            size_t numSequencesToAdd = cModel.sequences.size();

            animationModelInfo.numSequences = static_cast<u16>(numSequencesToAdd);
            animationModelInfo.sequenceOffset = static_cast<u32>(numSequenceInfoBefore);

            animationSequence.resize(numSequenceInfoBefore + numSequencesToAdd);

            for (u32 i = 0; i < numSequencesToAdd; i++)
            {
                AnimationSequence& sequence = animationSequence[numSequenceInfoBefore + i];
                CModel::ComplexAnimationSequence& cmodelSequence = cModel.sequences[i];

                sequence.animationId = cmodelSequence.id;
                sequence.animationSubId = cmodelSequence.subId;
                sequence.nextSubAnimationId = cmodelSequence.nextVariationId;
                sequence.nextAliasId = cmodelSequence.nextAliasId;

                sequence.flags.isAlwaysPlaying = cmodelSequence.flags.isAlwaysPlaying;
                sequence.flags.isAlias = cmodelSequence.flags.isAlias;
                sequence.flags.blendTransition = cmodelSequence.flags.blendTransition;

                sequence.duration = static_cast<float>(cmodelSequence.duration) / 1000.f;
                sequence.repeatMin = cmodelSequence.repetitionRange.x;
                sequence.repeatMax = cmodelSequence.repetitionRange.y;
                sequence.blendTimeStart = cmodelSequence.blendTimeStart;
                sequence.blendTimeEnd = cmodelSequence.blendTimeEnd;
            }
        });
    }

    // Add Bones
    _animationBoneInfo.WriteLock([&](std::vector<AnimationBoneInfo>& animationBoneInfo)
    {
        size_t numBoneInfoBefore = animationBoneInfo.size();
        size_t numBonesToAdd = cModel.bones.size();

        complexModel.numBones = static_cast<u32>(numBonesToAdd);

        animationModelInfo.numBones = static_cast<u16>(numBonesToAdd);
        animationModelInfo.boneInfoOffset = static_cast<u32>(numBoneInfoBefore);

        u32 numSequences = 0;
        u32 numTracksWithValues = 0;

        animationBoneInfo.resize(numBoneInfoBefore + numBonesToAdd);
        for (u32 i = 0; i < numBonesToAdd; i++)
        {
            AnimationBoneInfo& boneInfo = animationBoneInfo[numBoneInfoBefore + i];
            CModel::ComplexBone& bone = cModel.bones[i];

            boneInfo.numTranslationSequences = static_cast<u16>(bone.translation.tracks.size());
            if (boneInfo.numTranslationSequences > 0)
            {
                std::scoped_lock lock(_animationTrackMutex);

                boneInfo.translationSequenceOffset = static_cast<u32>(_animationTrackInfo.size());
                for (u32 j = 0; j < boneInfo.numTranslationSequences; j++)
                {
                    CModel::ComplexAnimationTrack<vec3>& track = bone.translation.tracks[j];
                    AnimationTrackInfo& trackInfo = _animationTrackInfo.emplace_back();

                    trackInfo.sequenceIndex = track.sequenceId;

                    trackInfo.numTimestamps = static_cast<u16>(track.timestamps.size());
                    trackInfo.numValues = static_cast<u16>(track.values.size());

                    trackInfo.timestampOffset = static_cast<u32>(_animationTrackTimestamps.size());
                    trackInfo.valueOffset = static_cast<u32>(_animationTrackValues.size());

                    // Add Timestamps
                    {
                        size_t numTimestampsBefore = _animationTrackTimestamps.size();
                        size_t numTimestampsToAdd = track.timestamps.size();

                        _animationTrackTimestamps.resize(numTimestampsBefore + numTimestampsToAdd);
                        memcpy(&_animationTrackTimestamps[numTimestampsBefore], track.timestamps.data(), numTimestampsToAdd * sizeof(u32));
                    }

                    // Add Values
                    {
                        size_t numValuesBefore = _animationTrackValues.size();
                        size_t numValuesToAdd = track.values.size();

                        _animationTrackValues.resize(numValuesBefore + numValuesToAdd);

                        for (size_t x = numValuesBefore; x < numValuesBefore + numValuesToAdd; x++)
                        {
                            _animationTrackValues[x] = vec4(track.values[x - numValuesBefore], 0.f);
                        }
                    }

                    numTracksWithValues += trackInfo.numValues;
                }
            }

            boneInfo.numRotationSequences = static_cast<u16>(bone.rotation.tracks.size());
            if (boneInfo.numRotationSequences > 0)
            {
                std::scoped_lock lock(_animationTrackMutex);

                boneInfo.rotationSequenceOffset = static_cast<u32>(_animationTrackInfo.size());
                for (u32 j = 0; j < boneInfo.numRotationSequences; j++)
                {
                    CModel::ComplexAnimationTrack<vec4>& track = bone.rotation.tracks[j];
                    AnimationTrackInfo& trackInfo = _animationTrackInfo.emplace_back();

                    trackInfo.sequenceIndex = track.sequenceId;

                    trackInfo.numTimestamps = static_cast<u16>(track.timestamps.size());
                    trackInfo.numValues = static_cast<u16>(track.values.size());

                    trackInfo.timestampOffset = static_cast<u32>(_animationTrackTimestamps.size());
                    trackInfo.valueOffset = static_cast<u32>(_animationTrackValues.size());

                    // Add Timestamps
                    {
                        size_t numTimestampsBefore = _animationTrackTimestamps.size();
                        size_t numTimestampsToAdd = track.timestamps.size();

                        _animationTrackTimestamps.resize(numTimestampsBefore + numTimestampsToAdd);
                        memcpy(&_animationTrackTimestamps[numTimestampsBefore], track.timestamps.data(), numTimestampsToAdd * sizeof(u32));
                    }

                    // Add Values
                    {
                        size_t numValuesBefore = _animationTrackValues.size();
                        size_t numValuesToAdd = track.values.size();

                        _animationTrackValues.resize(numValuesBefore + numValuesToAdd);
                        memcpy(&_animationTrackValues[numValuesBefore], track.values.data(), numValuesToAdd * sizeof(vec4));
                    }

                    numTracksWithValues += trackInfo.numValues;
                }
            }

            boneInfo.numScaleSequences = static_cast<u16>(bone.scale.tracks.size());
            if (boneInfo.numScaleSequences > 0)
            {
                std::scoped_lock lock(_animationTrackMutex);

                boneInfo.scaleSequenceOffset = static_cast<u32>(_animationTrackInfo.size());
                for (u32 j = 0; j < boneInfo.numScaleSequences; j++)
                {
                    CModel::ComplexAnimationTrack<vec3>& track = bone.scale.tracks[j];
                    AnimationTrackInfo& trackInfo = _animationTrackInfo.emplace_back();

                    trackInfo.sequenceIndex = track.sequenceId;

                    trackInfo.numTimestamps = static_cast<u16>(track.timestamps.size());
                    trackInfo.numValues = static_cast<u16>(track.values.size());

                    trackInfo.timestampOffset = static_cast<u32>(_animationTrackTimestamps.size());
                    trackInfo.valueOffset = static_cast<u32>(_animationTrackValues.size());

                    // Add Timestamps
                    {
                        size_t numTimestampsBefore = _animationTrackTimestamps.size();
                        size_t numTimestampsToAdd = track.timestamps.size();

                        _animationTrackTimestamps.resize(numTimestampsBefore + numTimestampsToAdd);
                        memcpy(&_animationTrackTimestamps[numTimestampsBefore], track.timestamps.data(), numTimestampsToAdd * sizeof(u32));
                    }

                    // Add Values
                    {
                        size_t numValuesBefore = _animationTrackValues.size();
                        size_t numValuesToAdd = track.values.size();

                        _animationTrackValues.resize(numValuesBefore + numValuesToAdd);

                        for (size_t x = numValuesBefore; x < numValuesBefore + numValuesToAdd; x++)
                        {
                            _animationTrackValues[x] = vec4(track.values[x - numValuesBefore], 0.f);
                        }
                    }

                    numTracksWithValues += trackInfo.numValues;
                }
            }

            numSequences += boneInfo.numTranslationSequences + boneInfo.numRotationSequences + boneInfo.numScaleSequences;

            boneInfo.flags.isTranslationTrackGlobalSequence = bone.translation.isGlobalSequence;
            boneInfo.flags.isRotationTrackGlobalSequence = bone.rotation.isGlobalSequence;
            boneInfo.flags.isScaleTrackGlobalSequence = bone.scale.isGlobalSequence;

            boneInfo.parentBoneId = bone.parentBoneId;
            boneInfo.flags.animate = (bone.flags.transformed | bone.flags.unk_0x80) > 0;
            boneInfo.pivotPointX = bone.pivot.x;
            boneInfo.pivotPointY = bone.pivot.y;
            boneInfo.pivotPointZ = bone.pivot.z;
        }

        // We also need to account for the possibility that a model comes with no included values due to the values being found in a separate '.anim' file
        complexModel.isAnimated = complexModel.numBones > 0 && numSequences > 0 && numTracksWithValues > 0;
    });

    // Add vertices
    size_t numVerticesBeforeAdd = 0;
    _vertices.WriteLock([&](std::vector<CModel::ComplexVertex>& vertices)
    {
        numVerticesBeforeAdd = vertices.size();
        size_t numVerticesToAdd = cModel.vertices.size();

        vertices.resize(numVerticesBeforeAdd + numVerticesToAdd);
        memcpy(&vertices[numVerticesBeforeAdd], cModel.vertices.data(), numVerticesToAdd * sizeof(CModel::ComplexVertex));
    });

    complexModel.numVertices = static_cast<u32>(cModel.vertices.size());
    complexModel.vertexOffset = static_cast<u32>(numVerticesBeforeAdd);

    // Handle the CullingData
    size_t numCullingDataBeforeAdd = 0;
    _cullingDatas.WriteLock([&](std::vector<CModel::CullingData>& cullingDatas)
    {
        numCullingDataBeforeAdd = cullingDatas.size();
        complexModel.cullingDataID = static_cast<u32>(numCullingDataBeforeAdd);

        CModel::CullingData& cullingData = cullingDatas.emplace_back();
        cullingData = cModel.cullingData;
    });

    // Handle this models renderbatches
    size_t numRenderBatches = static_cast<u32>(cModel.modelData.renderBatches.size());
    for (size_t i = 0; i < numRenderBatches; i++)
    {
        CModel::ComplexRenderBatch& renderBatch = cModel.modelData.renderBatches[i];

        // Select where to store the DrawCall templates, this won't be necessary once we do backface culling in the culling compute shader
        bool isTransparent = IsRenderBatchTransparent(renderBatch, cModel);
        std::vector<DrawCall>& drawCallTemplates = (isTransparent) ? complexModel.transparentDrawCallTemplates : complexModel.opaqueDrawCallTemplates;
        std::vector<DrawCallData>& drawCallDataTemplates = (isTransparent) ? complexModel.transparentDrawCallDataTemplates : complexModel.opaqueDrawCallDataTemplates;

        if (isTransparent)
        {
            complexModel.numTransparentDrawCalls++;
        }
        else
        {
            complexModel.numOpaqueDrawCalls++;
        }

        // For each renderbatch we want to create a template DrawCall and DrawCallData inside of the LoadedComplexModel
        DrawCall& drawCallTemplate = drawCallTemplates.emplace_back();
        DrawCallData& drawCallDataTemplate = drawCallDataTemplates.emplace_back();
        drawCallDataTemplate.cullingDataID = complexModel.cullingDataID;

        drawCallTemplate.instanceCount = 1;
        drawCallTemplate.vertexOffset = static_cast<u32>(numVerticesBeforeAdd);
        
        // Add indices
        size_t numIndicesBeforeAdd = 0;
        size_t numIndicesToAdd = 0;
        _indices.WriteLock([&](std::vector<u16>& indices)
        {
            numIndicesBeforeAdd = indices.size();
            numIndicesToAdd = renderBatch.indexCount;

            indices.resize(numIndicesBeforeAdd + numIndicesToAdd);
            memcpy(&indices[numIndicesBeforeAdd], &cModel.modelData.indices[renderBatch.indexStart], numIndicesToAdd * sizeof(u16));
        });
        
        drawCallTemplate.firstIndex = static_cast<u32>(numIndicesBeforeAdd);
        drawCallTemplate.indexCount = static_cast<u32>(numIndicesToAdd);

        // Add texture units
        size_t numTextureUnitsBeforeAdd = 0;
        size_t numTextureUnitsToAdd = 0;
        size_t numUnlitTextureUnits = 0;
        _textureUnits.WriteLock([&](std::vector<TextureUnit>& textureUnits)
        {
            numTextureUnitsBeforeAdd = textureUnits.size();
            numTextureUnitsToAdd = renderBatch.textureUnits.size();

            textureUnits.resize(numTextureUnitsBeforeAdd + numTextureUnitsToAdd);
            for (size_t j = 0; j < numTextureUnitsToAdd; j++)
            {
                TextureUnit& textureUnit = textureUnits[numTextureUnitsBeforeAdd + j];

                CModel::ComplexTextureUnit& complexTextureUnit = renderBatch.textureUnits[j];
                CModel::ComplexMaterial& complexMaterial = cModel.materials[complexTextureUnit.materialIndex];

                bool isProjectedTexture = (static_cast<u8>(complexTextureUnit.flags) & static_cast<u8>(CModel::ComplexTextureUnitFlag::PROJECTED_TEXTURE)) != 0;
                u16 materialFlag = *reinterpret_cast<u16*>(&complexMaterial.flags) << 1;
                u16 blendingMode = complexMaterial.blendingMode << 11;

                textureUnit.data = static_cast<u16>(isProjectedTexture) | materialFlag | blendingMode;
                textureUnit.materialType = complexTextureUnit.shaderId;

                numUnlitTextureUnits += materialFlag & 0x1;

                // Load Textures into Texture Array
                {
                    // TODO: Wotlk only supports 2 textures, when we upgrade to cata+ this might need to be reworked
                    for (u32 t = 0; t < complexTextureUnit.textureCount; t++)
                    {
                        // Load Texture
                        CModel::ComplexTexture& complexTexture = cModel.textures[complexTextureUnit.textureIndices[t]];

                        if (complexTexture.type == CModel::ComplexTextureType::NONE)
                        {
                            Renderer::TextureDesc textureDesc;
                            textureDesc.path = textureSingleton.textureHashToPath[complexTexture.textureNameIndex];
                            _renderer->LoadTextureIntoArray(textureDesc, _cModelTextures, textureUnit.textureIds[t]);
                        }
                        /*else if (complexTexture.type == CModel::ComplexTextureType::COMPONENT_MONSTER_SKIN_1)
                        {
                            Renderer::TextureDesc textureDesc;
                            //textureDesc.path = modelTexturePath.replace_filename("SahauginskinBlue.dds").string();
                            //textureDesc.path = modelTexturePath.replace_filename("SnakeSkinBlack.dds").string();
                            textureDesc.path = modelTexturePath.replace_filename("druidcatskinpurple.dds").string();
                            _renderer->LoadTextureIntoArray(textureDesc, _cModelTextures, textureUnit.textureIds[t]);
                        }*/
                    }
                }
            }
        });

        drawCallDataTemplate.cullingDataID = static_cast<u32>(numCullingDataBeforeAdd);
        drawCallDataTemplate.textureUnitOffset = static_cast<u16>(numTextureUnitsBeforeAdd);
        drawCallDataTemplate.numTextureUnits = static_cast<u16>(numTextureUnitsToAdd);
        drawCallDataTemplate.renderPriority = renderBatch.renderPriority;
        drawCallDataTemplate.numUnlitTextureUnits = static_cast<u16>(numUnlitTextureUnits);
    }

    return true;
}

bool CModelRenderer::LoadFile(const std::string& cModelPathString, CModel::ComplexModel& cModel)
{
    if (!StringUtils::EndsWith(cModelPathString, ".cmodel"))
    {
        DebugHandler::PrintFatal("Tried to call 'LoadCModel' with a reference to a file that didn't end with '.cmodel'");
        return false;
    }

    fs::path cModelPath = "Data/extracted/CModels/" + cModelPathString;
    cModelPath.make_preferred();
    cModelPath = fs::absolute(cModelPath);

    FileReader cModelFile(cModelPath.string(), cModelPath.filename().string());
    if (!cModelFile.Open())
    {
        DebugHandler::PrintFatal("Failed to open CModel file: %s", cModelPath.string().c_str());
        return false;
    }

    Bytebuffer cModelBuffer(nullptr, cModelFile.Length());
    cModelFile.Read(&cModelBuffer, cModelBuffer.size);
    cModelFile.Close();

    if (!cModelBuffer.Get(cModel.header))
    {
        DebugHandler::PrintFatal("Failed to load Header for Complex Model: %s", cModel.name);
        return false;
    }

    if (cModel.header.typeID != CModel::COMPLEX_MODEL_TOKEN)
    {
        DebugHandler::PrintFatal("We opened ComplexModel file (%s) with invalid token %u instead of expected token %u", cModelPath.string().c_str(), cModel.header.typeID, CModel::COMPLEX_MODEL_TOKEN);
    }

    if (cModel.header.typeVersion != CModel::COMPLEX_MODEL_VERSION)
    {
        if (cModel.header.typeVersion < CModel::COMPLEX_MODEL_VERSION)
        {
            DebugHandler::PrintFatal("Loaded ComplexModel file (%s) with too old version %u instead of expected version of %u, rerun dataextractor", cModelPath.string().c_str(), cModel.header.typeVersion, CModel::COMPLEX_MODEL_VERSION);
        }
        else
        {
            DebugHandler::PrintFatal("Loaded ComplexModel file (%s) with too new version %u instead of expected version of %u, update your client", cModelPath.string().c_str(), cModel.header.typeVersion, CModel::COMPLEX_MODEL_VERSION);
        }
    }

    if (!cModelBuffer.Get(cModel.flags))
    {
        DebugHandler::PrintError("Failed to load Flags for Complex Model: %s", cModel.name);
        return false;
    }

    // Read Sequences
    {
        u32 numSequences = 0;
        if (!cModelBuffer.GetU32(numSequences))
        {
            DebugHandler::PrintError("Failed to load Sequences for Complex Model: %s", cModel.name);
            return false;
        }

        if (numSequences > 0)
        {
            cModel.sequences.resize(numSequences);
            cModelBuffer.GetBytes(reinterpret_cast<u8*>(cModel.sequences.data()), numSequences * sizeof(CModel::ComplexAnimationSequence));
        }
    }

    // Read Bones
    {
        u32 numBones = 0;
        if (!cModelBuffer.GetU32(numBones))
        {
            DebugHandler::PrintError("Failed to load Bones for Complex Model: %s", cModel.name);
            return false;
        }

        if (numBones > 0)
        {
            cModel.bones.resize(numBones);

            for (u32 i = 0; i < numBones; i++)
            {
                CModel::ComplexBone& bone = cModel.bones[i];

                if (!cModelBuffer.GetI32(bone.primaryBoneIndex))
                {
                    DebugHandler::PrintError("Failed to load Primary Bone Index for Complex Model: %s", cModel.name);
                    return false;
                }

                if (!cModelBuffer.Get(bone.flags))
                {
                    DebugHandler::PrintError("Failed to load Bone Flags for Complex Model: %s", cModel.name);
                    return false;
                }

                if (!cModelBuffer.GetI16(bone.parentBoneId))
                {
                    DebugHandler::PrintError("Failed to load Parent Bone Id for Complex Model: %s", cModel.name);
                    return false;
                }

                if (!cModelBuffer.GetU16(bone.submeshId))
                {
                    DebugHandler::PrintError("Failed to load Bone Submesh Id for Complex Model: %s", cModel.name);
                    return false;
                }

                if (!bone.translation.Deserialize(&cModelBuffer))
                {
                    DebugHandler::PrintError("Failed to load Bone Translation Track for Complex Model: %s", cModel.name);
                    return false;
                }

                if (!bone.rotation.Deserialize(&cModelBuffer))
                {
                    DebugHandler::PrintError("Failed to load Bone Rotation Track for Complex Model: %s", cModel.name);
                    return false;
                }

                if (!bone.scale.Deserialize(&cModelBuffer))
                {
                    DebugHandler::PrintError("Failed to load Bone Scale Track for Complex Model: %s", cModel.name);
                    return false;
                }

                if (!cModelBuffer.Get(bone.pivot))
                {
                    DebugHandler::PrintError("Failed to load Bone Pivot for Complex Model: %s", cModel.name);
                    return false;
                }
            }
        }
    }

    // Read Vertices
    {
        u32 numVertices = 0;
        if (!cModelBuffer.GetU32(numVertices))
        {
            DebugHandler::PrintError("Failed to load Vertices for Complex Model: %s", cModel.name);
            return false;
        }

        // If there are no vertices, we don't need to render it
        if (numVertices == 0)
        {
            DebugHandler::PrintError("Complex Model has no vertices: %s", cModel.name);
            return false;
        }

        cModel.vertices.resize(numVertices);
        cModelBuffer.GetBytes(reinterpret_cast<u8*>(cModel.vertices.data()), numVertices * sizeof(CModel::ComplexVertex));
    }

    // Read Textures
    {
        u32 numTextures = 0;
        if (!cModelBuffer.GetU32(numTextures))
        {
            DebugHandler::PrintError("Failed to load Textures for Complex Model: %s", cModel.name);
            return false;
        }

        if (numTextures > 0)
        {
            cModel.textures.resize(numTextures);
            cModelBuffer.GetBytes(reinterpret_cast<u8*>(cModel.textures.data()), numTextures * sizeof(CModel::ComplexTexture));
        }
    }

    // Read Materials
    {
        u32 numMaterials = 0;
        if (!cModelBuffer.GetU32(numMaterials))
        {
            DebugHandler::PrintError("Failed to load Materials for Complex Model: %s", cModel.name);
            return false;
        }

        if (numMaterials > 0)
        {
            cModel.materials.resize(numMaterials);
            cModelBuffer.GetBytes(reinterpret_cast<u8*>(cModel.materials.data()), numMaterials * sizeof(CModel::ComplexMaterial));
        }
    }

    // Read Texture Index Lookup Table
    {
        u32 numElements = 0;
        if (!cModelBuffer.GetU32(numElements))
        {
            DebugHandler::PrintError("Failed to load Texture Index Table for Complex Model: %s", cModel.name);
            return false;
        }

        if (numElements > 0)
        {
            cModel.textureIndexLookupTable.resize(numElements);
            cModelBuffer.GetBytes(reinterpret_cast<u8*>(cModel.textureIndexLookupTable.data()), numElements * sizeof(u16));
        }
    }

    // Read Texture Unit Lookup Table
    {
        u32 numElements = 0;
        if (!cModelBuffer.GetU32(numElements))
        {
            DebugHandler::PrintError("Failed to load Texture Unit Table for Complex Model: %s", cModel.name);
            return false;
        }

        if (numElements > 0)
        {
            cModel.textureUnitLookupTable.resize(numElements);
            cModelBuffer.GetBytes(reinterpret_cast<u8*>(cModel.textureUnitLookupTable.data()), numElements * sizeof(u16));
        }
    }

    // Read Texture Transparency Lookup Table
    {
        u32 numElements = 0;
        if (!cModelBuffer.GetU32(numElements))
        {
            DebugHandler::PrintError("Failed to load Texture Transparency Table for Complex Model: %s", cModel.name);
            return false;
        }

        if (numElements > 0)
        {
            cModel.textureTransparencyLookupTable.resize(numElements);
            cModelBuffer.GetBytes(reinterpret_cast<u8*>(cModel.textureTransparencyLookupTable.data()), numElements * sizeof(u16));
        }
    }

    // Read Texture Combiner Combos
    {
        u32 numElements = 0;
        if (!cModelBuffer.GetU32(numElements))
        {
            DebugHandler::PrintError("Failed to load Texture Combiner for Complex Model: %s", cModel.name);
            return false;
        }

        if (numElements > 0)
        {
            cModel.textureCombinerCombos.resize(numElements);
            cModelBuffer.GetBytes(reinterpret_cast<u8*>(cModel.textureCombinerCombos.data()), numElements * sizeof(u16));
        }
    }

    // Read Model Data
    {
        if (!cModelBuffer.Get(cModel.modelData.header))
        {
            DebugHandler::PrintError("Failed to load Model Data for Complex Model: %s", cModel.name);
            return false;
        }

        // Read Vertex Lookup Ids
        {
            u32 numElements = 0;
            if (!cModelBuffer.GetU32(numElements))
            {
                DebugHandler::PrintError("Failed to Vertex Lookup Table for Complex Model: %s", cModel.name);
                return false;
            }

            if (numElements > 0)
            {
                cModel.modelData.vertexLookupIds.resize(numElements);
                cModelBuffer.GetBytes(reinterpret_cast<u8*>(cModel.modelData.vertexLookupIds.data()), numElements * sizeof(u16));
            }
        }

        // Read Indices
        {
            u32 numElements = 0;
            if (!cModelBuffer.GetU32(numElements))
            {
                DebugHandler::PrintError("Failed to load Indices for Complex Model: %s", cModel.name);
                return false;
            }

            if (numElements > 0)
            {
                cModel.modelData.indices.resize(numElements);
                cModelBuffer.GetBytes(reinterpret_cast<u8*>(cModel.modelData.indices.data()), numElements * sizeof(u16));
            }
        }

        // Read Render Batches
        {
            u32 numRenderBatches = 0;
            if (!cModelBuffer.GetU32(numRenderBatches))
            {
                DebugHandler::PrintError("Failed to load Renderbatches for Complex Model: %s", cModel.name);
                return false;
            }

            cModel.modelData.renderBatches.reserve(numRenderBatches);
            for (u32 i = 0; i < numRenderBatches; i++)
            {
                CModel::ComplexRenderBatch& renderBatch = cModel.modelData.renderBatches.emplace_back();

                if (!cModelBuffer.GetU16(renderBatch.groupId))
                {
                    DebugHandler::PrintError("Failed to load Renderbatch Group Id for Complex Model: %s", cModel.name);
                    return false;
                }

                if (!cModelBuffer.GetU32(renderBatch.vertexStart))
                {
                    DebugHandler::PrintError("Failed to load Renderbatch Vertex Start Index for Complex Model: %s", cModel.name);
                    return false;
                }

                if (!cModelBuffer.GetU32(renderBatch.vertexCount))
                {
                    DebugHandler::PrintError("Failed to load Renderbatch Vertex Count for Complex Model: %s", cModel.name);
                    return false;
                }

                if (!cModelBuffer.GetU32(renderBatch.indexStart))
                {
                    DebugHandler::PrintError("Failed to load Renderbatch Indices Start Index for Complex Model: %s", cModel.name);
                    return false;
                }

                if (!cModelBuffer.GetU32(renderBatch.indexCount))
                {
                    DebugHandler::PrintError("Failed to load Renderbatch Indices Count for Complex Model: %s", cModel.name);
                    return false;
                }

                // Read Texture Units
                {
                    u32 numTextureUnits = 0;
                    if (!cModelBuffer.GetU32(numTextureUnits))
                    {
                        DebugHandler::PrintError("Failed to load Texture Units for Complex Model: %s", cModel.name);
                        return false;
                    }

                    renderBatch.textureUnits.reserve(numTextureUnits);

                    for (u32 j = 0; j < numTextureUnits; j++)
                    {
                        CModel::ComplexTextureUnit& textureUnit = renderBatch.textureUnits.emplace_back();

                        if (!cModelBuffer.Get(textureUnit.flags))
                        {
                            DebugHandler::PrintError("Failed to load Texture Units Flags for Complex Model: %s", cModel.name);
                            return false;
                        }

                        if (!cModelBuffer.GetU16(textureUnit.shaderId))
                        {
                            DebugHandler::PrintError("Failed to load Texture Units Shader Id for Complex Model: %s", cModel.name);
                            return false;
                        }

                        if (!cModelBuffer.GetU16(textureUnit.materialIndex))
                        {
                            DebugHandler::PrintError("Failed to load Texture Units Material Index for Complex Model: %s", cModel.name);
                            return false;
                        }

                        if (!cModelBuffer.GetU16(textureUnit.materialLayer))
                        {
                            DebugHandler::PrintError("Failed to load Texture Units Material Layer for Complex Model: %s", cModel.name);
                            return false;
                        }

                        if (!cModelBuffer.GetU16(textureUnit.textureCount))
                        {
                            DebugHandler::PrintError("Failed to load Texture Units Texture Count for Complex Model: %s", cModel.name);
                            return false;
                        }

                        if (!cModelBuffer.GetBytes(reinterpret_cast<u8*>(&textureUnit.textureIndices), textureUnit.textureCount * sizeof(u16)))
                        {
                            DebugHandler::PrintError("Failed to load Texture Units Texture Indices for Complex Model: %s", cModel.name);
                            return false;
                        }

                        if (!cModelBuffer.GetBytes(reinterpret_cast<u8*>(&textureUnit.textureUVAnimationIndices), textureUnit.textureCount * sizeof(u16)))
                        {
                            DebugHandler::PrintError("Failed to load Texture Units Texture UV Animation Indices for Complex Model: %s", cModel.name);
                            return false;
                        }

                        if (!cModelBuffer.GetU16(textureUnit.textureUnitLookupId))
                        {
                            DebugHandler::PrintError("Failed to load Texture Units Texture Unit Table for Complex Model: %s", cModel.name);
                            return false;
                        }
                    }
                }
            }
        }
    }

    // Read Culling Data
    if (!cModelBuffer.GetBytes(reinterpret_cast<u8*>(&cModel.cullingData), sizeof(CModel::CullingData)))
    {
        DebugHandler::PrintError("Failed to load Culling Data for Complex Model: %s", cModel.name);
        return false;
    }

    return true;
}

bool CModelRenderer::IsRenderBatchTransparent(const CModel::ComplexRenderBatch& renderBatch, const CModel::ComplexModel& cModel)
{
    if (renderBatch.textureUnits.size() > 0)
    {
        const CModel::ComplexMaterial& complexMaterial = cModel.materials[renderBatch.textureUnits[0].materialIndex];

        return complexMaterial.blendingMode != 0 && complexMaterial.blendingMode != 1;
    }

    return false;
}

void CModelRenderer::AddInstance(LoadedComplexModel& complexModel, const Terrain::Placement& placement, u32& instanceIndex)
{
    Instance* instance = nullptr;
    _instances.WriteLock([&](std::vector<Instance>& instances)
    {
        instanceIndex = static_cast<u32>(instances.size());
        instance = &instances.emplace_back();
    });

    // Add the instance
    vec3 pos = placement.position;
    quaternion rot = placement.rotation;
    vec3 scale = vec3(placement.scale) / 1024.0f;

    mat4x4 rotationMatrix = glm::toMat4(rot);
    mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), scale);

    instance->modelId = complexModel.objectID;
    instance->instanceMatrix = glm::translate(mat4x4(1.0f), pos) * rotationMatrix * scaleMatrix;
    instance->vertexOffset = complexModel.vertexOffset;

    BufferRangeFrame& boneDeformRangeFrame = _instanceBoneDeformRangeFrames.EmplaceBack();
    BufferRangeFrame& boneInstanceRangeFrame = _instanceBoneInstanceRangeFrames.EmplaceBack();

    if (complexModel.isAnimated)
    {
        u32 vertexOffset = _numTotalAnimatedVertices.fetch_add(complexModel.numVertices);
        instance->animatedVertexOffset = vertexOffset;

        u32 numBones = complexModel.numBones;

        if (!_animationBoneDeformRangeAllocator.Allocate(numBones * sizeof(mat4x4), boneDeformRangeFrame))
        {
            size_t currentBoneDeformMatrixSize = _animationBoneDeformRangeAllocator.Size();
            size_t newBoneDeformMatrixSize = static_cast<size_t>(static_cast<f64>(currentBoneDeformMatrixSize) * 1.25f);
            newBoneDeformMatrixSize += newBoneDeformMatrixSize % sizeof(mat4x4);

            _hasToResizeAnimationBoneDeformMatrixBuffer = true;
            _newAnimationBoneDeformMatrixBufferSize = newBoneDeformMatrixSize;

            _animationBoneDeformRangeAllocator.Grow(newBoneDeformMatrixSize);

            if (!_animationBoneDeformRangeAllocator.Allocate(numBones * sizeof(mat4x4), boneDeformRangeFrame))
            {
                DebugHandler::PrintFatal("Failed to allocate '_animationBoneDeformMatrixBuffer' to appropriate size");
            }
        }

        assert(boneDeformRangeFrame.offset % sizeof(mat4x4) == 0);
        instance->boneDeformOffset = static_cast<u32>(boneDeformRangeFrame.offset) / sizeof(mat4x4);

        _animationBoneInstances.WriteLock([&](std::vector<AnimationBoneInstance>& animationBoneInstances)
        {
            size_t numBoneInstances = animationBoneInstances.size();
            instance->boneInstanceDataOffset = static_cast<u32>(numBoneInstances);
            animationBoneInstances.resize(numBoneInstances + numBones);
        });

        const AnimationModelInfo& animationModelInfo = _animationModelInfo.ReadGet(complexModel.objectID);

        _animationSequences.ReadLock([&](const std::vector<AnimationSequence>& animationSequences)
        {
            for (u32 i = 0; i < animationModelInfo.numSequences; i++)
            {
                const AnimationSequence& animationSequence = animationSequences[animationModelInfo.sequenceOffset + i];

                if (animationSequence.flags.isAlwaysPlaying)
                {
                    AnimationRequest animationRequest;
                    {
                        animationRequest.instanceId = instanceIndex;
                        animationRequest.sequenceId = i;
                        animationRequest.flags.isPlaying = true;
                        animationRequest.flags.isLooping = true;
                    }

                    _animationRequests.enqueue(animationRequest);
                }
            }
        });
    }
    else
    {
        instance->boneDeformOffset = std::numeric_limits<u32>().max();
        instance->boneInstanceDataOffset = std::numeric_limits<u32>().max();
    }

    // Add the opaque DrawCalls and DrawCallDatas
    if (complexModel.numOpaqueDrawCalls > 0)
    {
        _opaqueDrawCalls.WriteLock([&](std::vector<DrawCall>& opaqueDrawCalls)
        {
            _opaqueDrawCallDatas.WriteLock([&](std::vector<DrawCallData>& opaqueDrawCallDatas)
            {
                size_t numOpaqueDrawCallsBeforeAdd = opaqueDrawCalls.size();
                for (u32 i = 0; i < complexModel.numOpaqueDrawCalls; i++)
                {
                    const DrawCall& drawCallTemplate = complexModel.opaqueDrawCallTemplates[i];
                    const DrawCallData& drawCallDataTemplate = complexModel.opaqueDrawCallDataTemplates[i];

                    DrawCall& drawCall = opaqueDrawCalls.emplace_back();
                    DrawCallData& drawCallData = opaqueDrawCallDatas.emplace_back();

                    _opaqueDrawCallDataIndexToLoadedModelIndex.WriteLock([&](robin_hood::unordered_map<u32, u32>& opaqueDrawCallDataIndexToLoadedModelIndex)
                        {
                            opaqueDrawCallDataIndexToLoadedModelIndex[static_cast<u32>(numOpaqueDrawCallsBeforeAdd) + i] = complexModel.objectID;
                        });

                    // Copy data from the templates
                    drawCall.firstIndex = drawCallTemplate.firstIndex;
                    drawCall.indexCount = drawCallTemplate.indexCount;
                    drawCall.instanceCount = drawCallTemplate.instanceCount;
                    drawCall.vertexOffset = drawCallTemplate.vertexOffset;

                    drawCallData.cullingDataID = drawCallDataTemplate.cullingDataID;
                    drawCallData.textureUnitOffset = drawCallDataTemplate.textureUnitOffset;
                    drawCallData.numTextureUnits = drawCallDataTemplate.numTextureUnits;
                    drawCallData.renderPriority = drawCallDataTemplate.renderPriority;
                    drawCallData.numUnlitTextureUnits = drawCallDataTemplate.numUnlitTextureUnits;

                    // Fill in the data that shouldn't be templated
                    drawCall.firstInstance = static_cast<u32>(numOpaqueDrawCallsBeforeAdd + i); // This is used in the shader to retrieve the DrawCallData
                    drawCallData.instanceID = static_cast<u32>(instanceIndex);
                }
            });
        });
    }

    // Add the transparent DrawCalls and DrawCallDatas
    if (complexModel.numTransparentDrawCalls > 0)
    {
        _transparentDrawCalls.WriteLock([&](std::vector<DrawCall>& transparentDrawCalls)
        {
            _transparentDrawCallDatas.WriteLock([&](std::vector<DrawCallData>& transparentDrawCallDatas)
            {
                size_t numTransparentDrawCallsBeforeAdd = transparentDrawCalls.size();
                for (u32 i = 0; i < complexModel.numTransparentDrawCalls; i++)
                {
                    const DrawCall& drawCallTemplate = complexModel.transparentDrawCallTemplates[i];
                    const DrawCallData& drawCallDataTemplate = complexModel.transparentDrawCallDataTemplates[i];

                    DrawCall& drawCall = transparentDrawCalls.emplace_back();
                    DrawCallData& drawCallData = transparentDrawCallDatas.emplace_back();

                    _transparentDrawCallDataIndexToLoadedModelIndex.WriteLock([&](robin_hood::unordered_map<u32, u32>& transparentDrawCallDataIndexToLoadedModelIndex)
                        {
                            transparentDrawCallDataIndexToLoadedModelIndex[static_cast<u32>(numTransparentDrawCallsBeforeAdd) + i] = complexModel.objectID;
                        });

                    // Copy data from the templates
                    drawCall.firstIndex = drawCallTemplate.firstIndex;
                    drawCall.indexCount = drawCallTemplate.indexCount;
                    drawCall.instanceCount = drawCallTemplate.instanceCount;
                    drawCall.vertexOffset = drawCallTemplate.vertexOffset;

                    drawCallData.cullingDataID = drawCallDataTemplate.cullingDataID;
                    drawCallData.textureUnitOffset = drawCallDataTemplate.textureUnitOffset;
                    drawCallData.numTextureUnits = drawCallDataTemplate.numTextureUnits;
                    drawCallData.renderPriority = drawCallDataTemplate.renderPriority;
                    drawCallData.numUnlitTextureUnits = drawCallDataTemplate.numUnlitTextureUnits;

                    // Fill in the data that shouldn't be templated
                    drawCall.firstInstance = static_cast<u32>(numTransparentDrawCallsBeforeAdd + i); // This is used in the shader to retrieve the DrawCallData
                    drawCallData.instanceID = static_cast<u32>(instanceIndex);
                }
            });
        });
    }
}

void CModelRenderer::CreateBuffers()
{
    // Sync Vertex buffer to GPU
    {
        _vertices.SetDebugName("CModelVertexBuffer");
        _vertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _vertices.SyncToGPU(_renderer, nullptr);

        _geometryPassDescriptorSet.Bind("_packedCModelVertices"_h, _vertices.GetBuffer());
        _materialPassDescriptorSet.Bind("_packedCModelVertices"_h, _vertices.GetBuffer());
        _transparencyPassDescriptorSet.Bind("_packedCModelVertices"_h, _vertices.GetBuffer());
    }

    // Sync Index buffer to GPU
    {
        _indices.SetDebugName("CModelIndexBuffer");
        _indices.SetUsage(Renderer::BufferUsage::INDEX_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER);
        _indices.SyncToGPU(_renderer, nullptr);

        _geometryPassDescriptorSet.Bind("_cModelIndices"_h, _indices.GetBuffer());
        _materialPassDescriptorSet.Bind("_cModelIndices"_h, _indices.GetBuffer());
        _transparencyPassDescriptorSet.Bind("_cModelIndices"_h, _indices.GetBuffer());
    }

    // Sync TextureUnit buffer to GPU
    {
        _textureUnits.SetDebugName("CModelTextureUnitBuffer");
        _textureUnits.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _textureUnits.SyncToGPU(_renderer, nullptr);

        _geometryPassDescriptorSet.Bind("_cModelTextureUnits"_h, _textureUnits.GetBuffer());
        _materialPassDescriptorSet.Bind("_cModelTextureUnits"_h, _textureUnits.GetBuffer());
        _transparencyPassDescriptorSet.Bind("_cModelTextureUnits"_h, _textureUnits.GetBuffer());
    }

    // Sync Instance buffer to GPU
    {
        _instances.SetDebugName("CModelInstanceBuffer");
        _instances.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _instances.SyncToGPU(_renderer, nullptr);

        _opaqueCullingDescriptorSet.Bind("_cModelInstances"_h, _instances.GetBuffer());
        _transparentCullingDescriptorSet.Bind("_cModelInstances"_h, _instances.GetBuffer());
        _animationPrepassDescriptorSet.Bind("_cModelInstances"_h, _instances.GetBuffer());
        _geometryPassDescriptorSet.Bind("_cModelInstances"_h, _instances.GetBuffer());
        _materialPassDescriptorSet.Bind("_cModelInstances"_h, _instances.GetBuffer());
        _transparencyPassDescriptorSet.Bind("_cModelInstances"_h, _instances.GetBuffer());
    }

    // Sync CullingData buffer to GPU
    {
        _cullingDatas.SetDebugName("CModelCullDataBuffer");
        _cullingDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _cullingDatas.SyncToGPU(_renderer, nullptr);

        _opaqueCullingDescriptorSet.Bind("_cullingDatas"_h, _cullingDatas.GetBuffer());
        _transparentCullingDescriptorSet.Bind("_cullingDatas"_h, _cullingDatas.GetBuffer());
    }

    // Sync AnimationSequence buffer to GPU
    {
        _animationSequences.SetDebugName("AnimationSequenceBuffer");
        _animationSequences.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _animationSequences.SyncToGPU(_renderer, nullptr);

        _animationPrepassDescriptorSet.Bind("_animationSequences"_h, _animationSequences.GetBuffer());
    }    
    
    // Sync AnimationModelInfo buffer to GPU
    {
        _animationModelInfo.SetDebugName("AnimationModelInfoBuffer");
        _animationModelInfo.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _animationModelInfo.SyncToGPU(_renderer, nullptr);

        _animationPrepassDescriptorSet.Bind("_animationModelInfos"_h, _animationModelInfo.GetBuffer());
    }    
    
    // Sync AnimationBoneInfo buffer to GPU
    {
        _animationBoneInfo.SetDebugName("AnimationBoneInfoBuffer");
        _animationBoneInfo.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _animationBoneInfo.SyncToGPU(_renderer, nullptr);

        _animationPrepassDescriptorSet.Bind("_animationBoneInfos"_h, _animationBoneInfo.GetBuffer());
    }

    // Create AnimationSequence buffer
    {
        size_t numSequenceInfo = _animationTrackInfo.size();
        Renderer::BufferDesc desc;
        desc.name = "AnimationTrackInfoBuffer";
        desc.size = sizeof(AnimationTrackInfo) * numSequenceInfo;
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

        _animationTrackInfoBuffer = _renderer->CreateAndFillBuffer(_animationTrackInfoBuffer, desc, _animationTrackInfo.data(), desc.size);
        _animationPrepassDescriptorSet.Bind("_animationTrackInfos"_h, _animationTrackInfoBuffer);
    }
    
    // Create AnimationTimestamp buffer
    {
        size_t numTrackTimestamps = _animationTrackTimestamps.size();
        Renderer::BufferDesc desc;
        desc.name = "AnimationTrackTimestampBuffer";
        desc.size = sizeof(u32) * numTrackTimestamps;
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

        _animationTrackTimestampBuffer = _renderer->CreateAndFillBuffer(_animationTrackTimestampBuffer, desc, _animationTrackTimestamps.data(), desc.size);
        _animationPrepassDescriptorSet.Bind("_animationTrackTimestamps"_h, _animationTrackTimestampBuffer);
    }

    // Create AnimationValueVec buffer
    {
        size_t numTrackValues = _animationTrackValues.size();
        Renderer::BufferDesc desc;
        desc.name = "AnimationTrackValueBuffer";
        desc.size = sizeof(vec4) * numTrackValues;
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

        _animationTrackValueBuffer = _renderer->CreateAndFillBuffer(_animationTrackValueBuffer, desc, _animationTrackValues.data(), desc.size);
        _animationPrepassDescriptorSet.Bind("_animationTrackValues"_h, _animationTrackValueBuffer);
    }

    {
        // Create OpaqueDrawCall and OpaqueCulledDrawCall buffer
        {
            _opaqueDrawCalls.SetDebugName("CModelOpaqueDrawCallBuffer");
            _opaqueDrawCalls.SetUsage(Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER);
            _opaqueDrawCalls.SyncToGPU(_renderer, nullptr);
            
            _opaqueCullingDescriptorSet.Bind("_drawCalls"_h, _opaqueDrawCalls.GetBuffer());
            _geometryPassDescriptorSet.Bind("_cModelDraws"_h, _opaqueDrawCalls.GetBuffer());
            _materialPassDescriptorSet.Bind("_cModelDraws"_h, _opaqueDrawCalls.GetBuffer());

            Renderer::BufferDesc desc;
            desc.name = "CModelOpaqueCullDrawCallBuffer";
            desc.size = sizeof(DrawCall) * _opaqueDrawCalls.Size();
            desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
            
            _opaqueCulledDrawCallBuffer = _renderer->CreateBuffer(_opaqueCulledDrawCallBuffer, desc);

            _opaqueCullingDescriptorSet.Bind("_culledDrawCalls"_h, _opaqueCulledDrawCallBuffer);
        }

        {
            _opaqueDrawCallDatas.SetDebugName("CModelOpaqueDrawCallDataBuffer");
            _opaqueDrawCallDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
            _opaqueDrawCallDatas.SyncToGPU(_renderer, nullptr);
            
            _opaqueCullingDescriptorSet.Bind("_packedCModelDrawCallDatas"_h, _opaqueDrawCallDatas.GetBuffer());
            _geometryPassDescriptorSet.Bind("_packedCModelDrawCallDatas"_h, _opaqueDrawCallDatas.GetBuffer());
            _materialPassDescriptorSet.Bind("_packedCModelDrawCallDatas"_h, _opaqueDrawCallDatas.GetBuffer());
        }
    }
    
    {
        // Create TransparentDrawCall, TransparentCulledDrawCall and TransparentSortedCulledDrawCall buffer
        {
            _transparentDrawCalls.SetDebugName("CModelAlphaDrawCalls");
            _transparentDrawCalls.SetUsage(Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER);
            _transparentDrawCalls.SyncToGPU(_renderer, nullptr);

            _transparentCullingDescriptorSet.Bind("_drawCalls"_h, _transparentDrawCalls.GetBuffer());
            _transparencyPassDescriptorSet.Bind("_cModelDraws"_h, _transparentDrawCalls.GetBuffer());

            u32 size = sizeof(DrawCall) * static_cast<u32>(_transparentDrawCalls.Size());

            Renderer::BufferDesc desc;
            desc.name = "CModelAlphaCullDrawCalls";
            desc.size = size;
            desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
            _transparentCulledDrawCallBuffer = _renderer->CreateBuffer(_transparentCulledDrawCallBuffer, desc);
            _transparentCullingDescriptorSet.Bind("_culledDrawCalls"_h, _transparentCulledDrawCallBuffer);
        }

        // Create TransparentDrawCallData buffer
        {
            _transparentDrawCallDatas.SetDebugName("CModelAlphaDrawCallDataBuffer");
            _transparentDrawCallDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
            _transparentDrawCallDatas.SyncToGPU(_renderer, nullptr);

            _transparentCullingDescriptorSet.Bind("_packedCModelDrawCallDatas"_h, _transparentDrawCallDatas.GetBuffer());
            _transparencyPassDescriptorSet.Bind("_packedCModelDrawCallDatas"_h, _transparentDrawCallDatas.GetBuffer());
        }
    }

    // Create GPU-only workbuffers
    {
        Renderer::BufferDesc desc;
        desc.name = "CModelVisibleInstanceMaskBuffer";
        desc.size = sizeof(u32) * ((_instances.Size() + 31) / 32);
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        _visibleInstanceMaskBuffer = _renderer->CreateBuffer(_visibleInstanceMaskBuffer, desc);

        _compactDescriptorSet.Bind("_visibleInstanceMask"_h, _visibleInstanceMaskBuffer);
        _opaqueCullingDescriptorSet.Bind("_visibleInstanceMask"_h, _visibleInstanceMaskBuffer);
        _transparentCullingDescriptorSet.Bind("_visibleInstanceMask"_h, _visibleInstanceMaskBuffer);
    }
    {
        Renderer::BufferDesc desc;
        desc.name = "CModelVisibleInstanceCountBuffer";
        desc.size = sizeof(u32);
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        _visibleInstanceCountBuffer = _renderer->CreateBuffer(_visibleInstanceCountBuffer, desc);

        _compactDescriptorSet.Bind("_visibleInstanceCount"_h, _visibleInstanceCountBuffer);
        _animationPrepassDescriptorSet.Bind("_visibleInstanceCount"_h, _visibleInstanceCountBuffer);
        _visibleInstanceArgumentDescriptorSet.Bind("_source"_h, _visibleInstanceCountBuffer);
    }
    {
        Renderer::BufferDesc desc;
        desc.name = "CModelVisibleInstanceIndexBuffer";
        desc.size = sizeof(u32) * _instances.Size();
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER;
        _visibleInstanceIndexBuffer = _renderer->CreateBuffer(_visibleInstanceIndexBuffer, desc);

        _compactDescriptorSet.Bind("_visibleInstanceIDs"_h, _visibleInstanceIndexBuffer);
        _animationPrepassDescriptorSet.Bind("_visibleInstanceIndices"_h, _visibleInstanceIndexBuffer);
    }
    {
        Renderer::BufferDesc desc;
        desc.name = "CModelVisibleInstanceIndexBuffer";
        desc.size = sizeof(VkDispatchIndirectCommand);
        desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER;
        _visibleInstanceCountArgumentBuffer32 = _renderer->CreateBuffer(_visibleInstanceCountArgumentBuffer32, desc);

        _visibleInstanceArgumentDescriptorSet.Bind("_target"_h, _visibleInstanceCountArgumentBuffer32);
    }
    {
        Renderer::BufferDesc desc;
        desc.name = "CModelVertexBuffer";
        desc.size = sizeof(PackedAnimatedVertexPositions) * _numTotalAnimatedVertices;
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

        _animatedVertexPositions = _renderer->CreateBuffer(_animatedVertexPositions, desc);

        _geometryPassDescriptorSet.Bind("_animatedCModelVertexPositions"_h, _animatedVertexPositions);
        _materialPassDescriptorSet.Bind("_animatedCModelVertexPositions"_h, _animatedVertexPositions);
        _transparencyPassDescriptorSet.Bind("_animatedCModelVertexPositions"_h, _animatedVertexPositions);
    }
}
