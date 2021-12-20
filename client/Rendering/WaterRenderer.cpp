#include "WaterRenderer.h"
#include "DebugRenderer.h"
#include "../Utils/ServiceLocator.h"
#include "../Utils/MapUtils.h"
#include "RenderResources.h"
#include "CVar/CVarSystem.h"
#include "Camera.h"
#include "RenderUtils.h"

#include <filesystem>
#include <GLFW/glfw3.h>
#include <InputManager.h>
#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Utils/FileReader.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <tracy/TracyVulkan.hpp>

#include "../ECS/Components/Singletons/MapSingleton.h"
#include "../ECS/Components/Singletons/NDBCSingleton.h"
#include "../ECS/Components/Singletons/TextureSingleton.h"

namespace fs = std::filesystem;

AutoCVar_Int CVAR_WaterCullingEnabled("water.cullEnable", "enable culling of water", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_WaterLockCullingFrustum("water.lockCullingFrustum", "lock frustrum for water culling", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_WaterDrawBoundingBoxes("water.drawBoundingBoxes", "draw bounding boxes for water", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_WaterOcclusionCullEnabled("water.occlusionCullEnable", "enable culling of water", 1, CVarFlags::EditCheckbox);
AutoCVar_Float CVAR_WaterVisibilityRange("water.visibilityRange", "How far underwater you should see", 3.0f, CVarFlags::EditFloatDrag);

WaterRenderer::WaterRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    , _debugRenderer(debugRenderer)
{
    CreatePermanentResources();
}

WaterRenderer::~WaterRenderer()
{

}

void WaterRenderer::Update(f32 deltaTime)
{
    f32 waterProgress = deltaTime * 30.0f;
    _drawConstants.currentTime = std::fmodf(_drawConstants.currentTime + waterProgress, 30.0f);

    // Update arealight water color
    {
        entt::registry* registry = ServiceLocator::GetGameRegistry();
        MapSingleton& mapSingleton = registry->ctx<MapSingleton>();

        i32 lockLight = *CVarSystem::Get()->GetIntCVar("lights.lock");
        if (!lockLight)
        {
            const AreaUpdateLightColorData& lightColor = mapSingleton.GetLightColorData();

            _drawConstants.shallowOceanColor = lightColor.shallowOceanColor;
            _drawConstants.deepOceanColor = lightColor.deepOceanColor;
            _drawConstants.shallowRiverColor = lightColor.shallowRiverColor;
            _drawConstants.deepRiverColor = lightColor.deepRiverColor;
        }
    }

    _drawConstants.waterVisibilityRange = Math::Max(CVAR_WaterVisibilityRange.GetFloat(), 1.0f);

    bool drawBoundingBoxes = CVAR_WaterDrawBoundingBoxes.Get() == 1;
    if (drawBoundingBoxes)
    {
        _boundingBoxes.ReadLock([&](const std::vector<AABB>& boundingBoxes)
        {
            for (const AABB& boundingBox : boundingBoxes)
            {
                vec3 center = (vec3(boundingBox.min) + vec3(boundingBox.max)) * 0.5f;
                vec3 extents = vec3(boundingBox.max) - center;

                _debugRenderer->DrawAABB3D(center, extents, 0xff00ffff);
            }
        });
    }

    u32 numDrawCalls = static_cast<u32>(_drawCalls.Size());

    _numSurvivingDrawCalls = numDrawCalls;
    _numSurvivingTriangles = _numTriangles;

    const bool cullingEnabled = CVAR_WaterCullingEnabled.Get();
    if (cullingEnabled && numDrawCalls > 0)
    {
        // Drawcalls
        {
            u32* count = static_cast<u32*>(_renderer->MapBuffer(_culledDrawCountReadBackBuffer));
            if (count != nullptr)
            {
                _numSurvivingDrawCalls = *count;
            }
            _renderer->UnmapBuffer(_culledDrawCountReadBackBuffer);
        }

        // Triangles
        {
            u32* count = static_cast<u32*>(_renderer->MapBuffer(_culledTriangleCountReadBackBuffer));
            if (count != nullptr)
            {
                _numSurvivingTriangles = *count;
            }
            _renderer->UnmapBuffer(_culledTriangleCountReadBackBuffer);
        }
    }
}

void WaterRenderer::LoadWater(SafeVector<u16>& chunkIDs)
{
    RegisterChunksToBeLoaded(chunkIDs);
    ExecuteLoad();
}

void WaterRenderer::Clear()
{
    _drawCalls.Clear();
    _drawCallDatas.Clear();

    _vertices.Clear();
    _indices.Clear();
    
    _boundingBoxes.Clear();
    _waterTextureInfos.clear();

    _renderer->UnloadTexturesInArray(_waterTextures, 0);
}

void WaterRenderer::AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    u32 numDrawCalls = static_cast<u32>(_drawCalls.Size());
    if (numDrawCalls == 0)
        return;

    const bool cullingEnabled = CVAR_WaterCullingEnabled.Get();
    if (!cullingEnabled)
        return;

    const bool lockFrustum = CVAR_WaterLockCullingFrustum.Get();

    struct WaterCullingPassData
    {
        Renderer::RenderPassMutableResource depth;
    };

    renderGraph->AddPass<WaterCullingPassData>("Water Culling",
        [=](WaterCullingPassData& data, Renderer::RenderGraphBuilder& builder)
        {
            data.depth = builder.Write(resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](WaterCullingPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, WaterCullingPass);

            // Update constants
            if (!lockFrustum)
            {
                Camera* camera = ServiceLocator::GetCamera();
                memcpy(_cullConstants.frustumPlanes, camera->GetFrustumPlanes(), sizeof(vec4[6]));
                _cullConstants.cameraPos = camera->GetPosition();
            }

            // Reset the counters
            commandList.FillBuffer(_culledDrawCountBuffer, 0, 4, 0);
            commandList.FillBuffer(_culledTriangleCountBuffer, 0, 4, 0);

            commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _culledDrawCountBuffer);
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _culledTriangleCountBuffer);

            Renderer::ComputePipelineDesc cullingPipelineDesc;
            graphResources.InitializePipelineDesc(cullingPipelineDesc);

            Renderer::ComputeShaderDesc shaderDesc;
            shaderDesc.path = "waterCulling.cs.hlsl";
            cullingPipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

            Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(cullingPipelineDesc);
            commandList.BeginPipeline(pipeline);

            // Make a framelocal copy of our cull constants
            CullConstants* cullConstants = graphResources.FrameNew<CullConstants>();
            memcpy(cullConstants, &_cullConstants, sizeof(CullConstants));
            cullConstants->maxDrawCount = numDrawCalls;
            cullConstants->occlusionCull = CVAR_WaterOcclusionCullEnabled.Get();
            commandList.PushConstant(cullConstants, 0, sizeof(CullConstants));

            _cullingDescriptorSet.Bind("_depthPyramid"_h, resources.depthPyramid);

            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, &_cullingDescriptorSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);

            commandList.Dispatch((numDrawCalls + 31) / 32, 1, 1);

            commandList.EndPipeline(pipeline);
        });
}

void WaterRenderer::AddWaterPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    u32 numDrawCalls = static_cast<u32>(_drawCalls.Size());
    if (numDrawCalls == 0)
        return;

    const bool cullingEnabled = CVAR_WaterCullingEnabled.Get();

    struct WaterPassData
    {
        Renderer::RenderPassMutableResource transparency;
        Renderer::RenderPassMutableResource transparencyWeights;
        Renderer::RenderPassMutableResource depth;
    };

    renderGraph->AddPass<WaterPassData>("Water OIT Pass", 
        [=](WaterPassData& data, Renderer::RenderGraphBuilder& builder)
        {
            data.transparency = builder.Write(resources.transparency, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);
            data.transparencyWeights = builder.Write(resources.transparencyWeights, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);

            return true; // Return true from setup to enable this pass, return false to disable it
        }, 
        [=](WaterPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, WaterPass);

            commandList.PushMarker("Water", Color::White);

            RenderUtils::CopyDepthToColorRT(_renderer, graphResources, commandList, frameIndex, resources.depth, resources.depthColorCopy, 0);

            commandList.ImageBarrier(resources.transparency);
            commandList.ImageBarrier(resources.transparencyWeights);

            if (cullingEnabled)
            {
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _culledDrawCallsBuffer);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _culledDrawCountBuffer);
            }

            Renderer::GraphicsPipelineDesc pipelineDesc;
            graphResources.InitializePipelineDesc(pipelineDesc);

            // Shaders
            Renderer::VertexShaderDesc vertexShaderDesc;
            vertexShaderDesc.path = "water.vs.hlsl";
            pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

            Renderer::PixelShaderDesc pixelShaderDesc;
            pixelShaderDesc.path = "water.ps.hlsl";
            pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

            // Depth state
            pipelineDesc.states.depthStencilState.depthEnable = true;
            pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

            // Rasterizer state
            pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
            pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;

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

            // Set pipeline
            Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
            commandList.BeginPipeline(pipeline);

            _passDescriptorSet.Bind("_depthRT"_h, resources.depthColorCopy);

            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, &_passDescriptorSet, frameIndex);

            DrawConstants* constants = graphResources.FrameNew<DrawConstants>();
            memcpy(constants, &_drawConstants, sizeof(DrawConstants));

            commandList.PushConstant(constants, 0, sizeof(DrawConstants));

            commandList.SetIndexBuffer(_indices.GetBuffer(), Renderer::IndexFormat::UInt16);

            if (cullingEnabled)
            {
                commandList.DrawIndexedIndirectCount(_culledDrawCallsBuffer, 0, _culledDrawCountBuffer, 0, numDrawCalls);
            }
            else
            {
                commandList.DrawIndexedIndirect(_drawCalls.GetBuffer(), 0, numDrawCalls);
            }

            commandList.PopMarker();

            commandList.EndPipeline(pipeline);

            // Copy from our draw count buffer to the readback buffer
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, _culledDrawCountBuffer);
            commandList.CopyBuffer(_culledDrawCountReadBackBuffer, 0, _culledDrawCountBuffer, 0, 4);
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, _culledDrawCountReadBackBuffer);

            commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToTransferSrc, _culledTriangleCountBuffer);
            commandList.CopyBuffer(_culledTriangleCountReadBackBuffer, 0, _culledTriangleCountBuffer, 0, 4);
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToTransferSrc, _culledTriangleCountReadBackBuffer);
        });
}

void WaterRenderer::CreatePermanentResources()
{
    Renderer::TextureArrayDesc textureArrayDesc;
    textureArrayDesc.size = 1024;

    _waterTextures = _renderer->CreateTextureArray(textureArrayDesc);
    _passDescriptorSet.Bind("_textures"_h, _waterTextures);

    Renderer::SamplerDesc samplerDesc;
    samplerDesc.enabled = true;
    samplerDesc.filter = Renderer::SamplerFilter::ANISOTROPIC;//Renderer::SamplerFilter::SAMPLER_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    samplerDesc.addressU = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressV = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;
    samplerDesc.maxAnisotropy = 8;

    _sampler = _renderer->CreateSampler(samplerDesc);
    _passDescriptorSet.Bind("_sampler"_h, _sampler);

    Renderer::SamplerDesc occlusionSamplerDesc;
    occlusionSamplerDesc.filter = Renderer::SamplerFilter::MINIMUM_MIN_MAG_MIP_LINEAR;

    occlusionSamplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    occlusionSamplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    occlusionSamplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    occlusionSamplerDesc.minLOD = 0.f;
    occlusionSamplerDesc.maxLOD = 16.f;
    occlusionSamplerDesc.mode = Renderer::SamplerReductionMode::MIN;

    _occlusionSampler = _renderer->CreateSampler(occlusionSamplerDesc);
    _cullingDescriptorSet.Bind("_depthSampler"_h, _occlusionSampler);
}

bool WaterRenderer::RegisterChunksToBeLoaded(SafeVector<u16>& chunkIDs)
{
    DebugHandler::Print("Loading Water");

    entt::registry* registry = ServiceLocator::GetGameRegistry();
    MapSingleton& mapSingleton = registry->ctx<MapSingleton>();
    NDBCSingleton& ndbcSingleton = registry->ctx<NDBCSingleton>();

    NDBC::File* liquidTypesNDBC = ndbcSingleton.GetNDBCFile("LiquidTypes"_h);
    StringTable*& liquidTypesStringTable = liquidTypesNDBC->GetStringTable();

    Terrain::Map& currentMap = mapSingleton.GetCurrentMap();

    _numTriangles = 0;

    chunkIDs.ReadLock([&](const std::vector<u16>& chunkIDsVector)
        {
            SafeVectorScopedWriteLock verticesLock(_vertices);
            std::vector<WaterVertex>& vertices = verticesLock.Get();

            SafeVectorScopedWriteLock indicesLock(_indices);
            std::vector<u16>& indices = indicesLock.Get();

            SafeVectorScopedWriteLock drawCallsLock(_drawCalls);
            std::vector<DrawCall>& drawCalls = drawCallsLock.Get();

            SafeVectorScopedWriteLock drawCallDatasLock(_drawCallDatas);
            std::vector<DrawCallData>& drawCallDatas = drawCallDatasLock.Get();

            SafeVectorScopedWriteLock boundingBoxesLock(_boundingBoxes);
            std::vector<AABB>& boundingBoxes = boundingBoxesLock.Get();

            for (const u16& chunkID : chunkIDsVector)
            {
                Terrain::Chunk& chunk = currentMap.chunks[chunkID];

                u16 chunkX = chunkID % Terrain::MAP_CHUNKS_PER_MAP_STRIDE;
                u16 chunkY = chunkID / Terrain::MAP_CHUNKS_PER_MAP_STRIDE;

                vec2 chunkPos = Terrain::MapUtils::GetChunkPosition(chunkID);

                vec3 chunkBasePos = Terrain::MAP_HALF_SIZE - vec3(Terrain::MAP_CHUNK_SIZE * chunkY, Terrain::MAP_CHUNK_SIZE * chunkX, Terrain::MAP_HALF_SIZE);

                if (chunk.liquidHeaders.size() == 0)
                    continue;

                u32 liquidInfoOffset = 0;

                for (u32 i = 0; i < chunk.liquidHeaders.size(); i++)
                {
                    Terrain::CellLiquidHeader& liquidHeader = chunk.liquidHeaders[i];

                    bool hasAttributes = liquidHeader.attributesOffset > 0; // liquidHeader.packedData >> 7;
                    u8 numInstances = liquidHeader.layerCount; // liquidHeader.packedData & 0x7F;

                    if (numInstances == 0)
                        continue;

                    u16 cellX = i % Terrain::MAP_CELLS_PER_CHUNK_SIDE;
                    u16 cellY = i / Terrain::MAP_CELLS_PER_CHUNK_SIDE;
                    vec3 liquidBasePos = chunkBasePos - vec3(Terrain::MAP_CELL_SIZE * cellY, Terrain::MAP_CELL_SIZE * cellX, 0);

                    const vec2 cellPos = Terrain::MapUtils::GetCellPosition(chunkPos, i);

                    for (u32 j = 0; j < numInstances; j++)
                    {
                        Terrain::CellLiquidInstance& liquidInstance = chunk.liquidInstances[liquidInfoOffset + j];

                        // Packed Format
                        // Bit 1-6 (liquidVertexFormat)
                        // Bit 7 (hasBitMaskForPatches)
                        // Bit 8 (hasVertexData)

                        bool hasVertexData = liquidInstance.vertexDataOffset > 0; // liquidInstance.packedData >> 7;
                        bool hasBitMaskForPatches = liquidInstance.bitmapExistsOffset > 0; // (liquidInstance.packedData >> 6) & 0x1;
                        u16 liquidVertexFormat = liquidInstance.liquidVertexFormat; // liquidInstance.packedData & 0x3F;

                        u8 posY = liquidInstance.yOffset; //liquidInstance.packedOffset & 0xf;
                        u8 posX = liquidInstance.xOffset; //liquidInstance.packedOffset >> 4;

                        u8 height = liquidInstance.height; // liquidInstance.packedSize & 0xf;
                        u8 width = liquidInstance.width; // liquidInstance.packedSize >> 4;

                        u32 vertexCount = (width + 1) * (height + 1);

                        f32* heightMap = nullptr;
                        if (hasVertexData)
                        {
                            if (liquidVertexFormat == 0) // LiquidVertexFormat_Height_Depth
                            {
                                heightMap = reinterpret_cast<f32*>(&chunk.liquidBytes[liquidInstance.vertexDataOffset]);
                            }
                            else if (liquidVertexFormat == 1) // LiquidVertexFormat_Height_UV
                            {
                                heightMap = reinterpret_cast<f32*>(&chunk.liquidBytes[liquidInstance.vertexDataOffset]);
                            }
                            else if (liquidVertexFormat == 3) // LiquidVertexFormat_Height_UV_Depth
                            {
                                heightMap = reinterpret_cast<f32*>(&chunk.liquidBytes[liquidInstance.vertexDataOffset]);
                            }
                        }

                        DrawCall& drawCall = drawCalls.emplace_back();
                        drawCall.instanceCount = 1;
                        drawCall.vertexOffset = static_cast<u32>(vertices.size());
                        drawCall.firstIndex = static_cast<u32>(indices.size());
                        drawCall.firstInstance = static_cast<u32>(drawCalls.size()) - 1;

                        DrawCallData& drawCallData = drawCallDatas.emplace_back();
                        drawCallData.chunkID = chunkID;
                        drawCallData.cellID = i;

                        NDBC::LiquidType* liquidType = liquidTypesNDBC->GetRowById<NDBC::LiquidType>(liquidInstance.liquidType);
                        const std::string& liquidTexture = liquidTypesStringTable->GetString(liquidType->texture);
                        u32 liquidTextureHash = liquidTypesStringTable->GetStringHash(liquidType->texture);

                        u32 textureIndex;
                        if (!TryLoadTexture(liquidTexture, liquidTextureHash, liquidType->numTextureFrames, textureIndex))
                        {
                            DebugHandler::PrintFatal("WaterRenderer::RegisterChunksToBeLoaded : failed to load texture %s", liquidTexture.c_str());
                        }

                        drawCallData.textureStartIndex = static_cast<u16>(textureIndex);
                        drawCallData.textureCount = liquidType->numTextureFrames;
                        drawCallData.hasDepth = liquidType->hasDepthEnabled;

                        vec3 min = vec3(100000, 100000, 100000);
                        vec3 max = vec3(-100000, -100000, -100000);

                        for (u8 y = 0; y <= height; y++)
                        {
                            // This represents World (Forward/Backward) in other words, our X axis
                            f32 offsetY = -(static_cast<f32>(posY + y) * Terrain::MAP_PATCH_SIZE);

                            for (u8 x = 0; x <= width; x++)
                            {
                                // This represents World (West/East) in other words, our Y axis
                                f32 offsetX = -(static_cast<f32>(posX + x) * Terrain::MAP_PATCH_SIZE);

                                f32 vertexHeight = liquidBasePos.z - liquidInstance.minHeightLevel;

                                u32 vertexIndex = x + (y * (width + 1));
                                if (heightMap && liquidInstance.liquidType != 2 && liquidVertexFormat != 2)
                                {
                                    vertexHeight = heightMap[vertexIndex];
                                }

                                WaterVertex vertex;
                                vertex.xCellOffset = y + liquidInstance.yOffset; // These are intended to be flipped, we are going from 2D to 3D
                                vertex.yCellOffset = x + liquidInstance.xOffset;
                                vertex.height = f16(vertexHeight);
                                vertex.uv = hvec2(static_cast<f32>(x) / 2.0f, static_cast<f32>(y) / 2.0f);

                                vertices.push_back(vertex);

                                // Calculate worldspace pos for AABB usage
                                vec3 pos = liquidBasePos - vec3(Terrain::MAP_PATCH_SIZE * (y + liquidInstance.yOffset), Terrain::MAP_PATCH_SIZE * (x + liquidInstance.xOffset), 0.0f);
                                pos.z = vertexHeight;

                                min = glm::min(min, pos);
                                max = glm::max(max, pos);

                                if (y < height && x < width)
                                {
                                    u16 topLeftVert = x + (y * (width + 1));
                                    u16 topRightVert = topLeftVert + 1;
                                    u16 bottomLeftVert = topLeftVert + (width + 1);
                                    u16 bottomRightVert = bottomLeftVert + 1;

                                    indices.push_back(topLeftVert);
                                    indices.push_back(bottomLeftVert);
                                    indices.push_back(topRightVert);

                                    indices.push_back(topRightVert);
                                    indices.push_back(bottomLeftVert);
                                    indices.push_back(bottomRightVert);

                                    drawCall.indexCount += 6;
                                }
                            }
                        }

                        _numTriangles += drawCall.indexCount / 3;

                        // The water could literally be a flat plane, so we have to add offsets to min.z and max.z
                        min.z -= 1.0f;
                        max.z += 1.0f;

                        AABB& boundingBox = boundingBoxes.emplace_back();
                        boundingBox.min = vec4(min, 0.0f);
                        boundingBox.max = vec4(max, 0.0f);
                }

                liquidInfoOffset += numInstances;
            }
        }
    });

    DebugHandler::Print("Water: Loaded (%u, %u) Vertices/Indices", _vertices.Size(), _indices.Size());
    return true;
}

void WaterRenderer::ExecuteLoad()
{
    // Sync DrawCalls to GPU
    {
        _drawCalls.SetDebugName("WaterDrawCalls");
        _drawCalls.SetUsage(Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER);
        _drawCalls.SyncToGPU(_renderer);

        _cullingDescriptorSet.Bind("_drawCalls"_h, _drawCalls.GetBuffer());
    }

    // Sync DrawCallDatas to GPU
    {
        _drawCallDatas.SetDebugName("WaterDrawCallDatas");
        _drawCallDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _drawCallDatas.SyncToGPU(_renderer);

        _passDescriptorSet.Bind("_drawCallDatas"_h, _drawCallDatas.GetBuffer());
    }

    // Sync Vertices to GPU
    {
        _vertices.SetDebugName("WaterVertices");
        _vertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _vertices.SyncToGPU(_renderer);

        _passDescriptorSet.Bind("_vertices"_h, _vertices.GetBuffer());
    }

    // Sync Indices to GPU
    {
        _indices.SetDebugName("WaterIndices");
        _indices.SetUsage(Renderer::BufferUsage::INDEX_BUFFER);
        _indices.SyncToGPU(_renderer);
    }

    // Sync BoundingBoxes to GPU
    {
        _boundingBoxes.SetDebugName("WaterBoundingBoxes");
        _boundingBoxes.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _boundingBoxes.SyncToGPU(_renderer);

        _cullingDescriptorSet.Bind("_boundingBoxes"_h, _boundingBoxes.GetBuffer());
    }

    // Create CulledDrawCallsBuffer
    {
        Renderer::BufferDesc desc;
        desc.name = "WaterCulledDrawcalls";
        desc.size = sizeof(DrawCall) * _drawCalls.Size();
        desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

        _culledDrawCallsBuffer = _renderer->CreateBuffer(_culledDrawCallsBuffer, desc);

        _cullingDescriptorSet.Bind("_culledDrawCalls", _culledDrawCallsBuffer);
    }

    // Create CulledDrawCountBuffer
    {
        Renderer::BufferDesc desc;
        desc.name = "WaterDrawCountBuffer";
        desc.size = sizeof(u32);
        desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;
        _culledDrawCountBuffer = _renderer->CreateBuffer(_culledDrawCountBuffer, desc);

        _cullingDescriptorSet.Bind("_drawCount", _culledDrawCountBuffer);

        desc.name = "WaterDrawCountRBBuffer";
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        desc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _culledDrawCountReadBackBuffer = _renderer->CreateBuffer(_culledDrawCountReadBackBuffer, desc);
    }

    // Create CulledTriangleCountBuffer
    {
        Renderer::BufferDesc desc;
        desc.name = "WaterTriangleCountBuffer";
        desc.size = sizeof(u32);
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;
        _culledTriangleCountBuffer = _renderer->CreateBuffer(_culledTriangleCountBuffer, desc);

        _cullingDescriptorSet.Bind("_triangleCount", _culledTriangleCountBuffer);

        desc.name = "WaterTriangleCountRBBuffer";
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        desc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _culledTriangleCountReadBackBuffer = _renderer->CreateBuffer(_culledTriangleCountReadBackBuffer, desc);
    }
}

bool WaterRenderer::TryLoadTexture(const std::string& textureName, u32 textureHash, u32 numTextures, u32& textureIndex)
{
    auto itr = _waterTextureInfos.find(textureHash);

    WaterTextureInfo* waterTextureInfo = nullptr;
    if (itr != _waterTextureInfos.end())
    {
        waterTextureInfo = &itr->second;

        if (numTextures <= waterTextureInfo->numTextures)
        {
            textureIndex = waterTextureInfo->textureArrayIndex;
            return true;
        }
    }
    else
    {
        waterTextureInfo = &_waterTextureInfos[textureHash];
    }

    Renderer::TextureDesc desc;
    u32 index;
    char tempTextureNameBuffer[1024];

    for (u32 i = 1; i <= numTextures; i++)
    {
        i32 length = StringUtils::FormatString(tempTextureNameBuffer, 1024, textureName.c_str(), i);
        if (length == 0)
            return false;

        u32 tempTextureHash = StringUtils::fnv1a_32(tempTextureNameBuffer, length);

        entt::registry* registry = ServiceLocator::GetGameRegistry();
        TextureSingleton& textureSingleton = registry->ctx<TextureSingleton>();

        desc.path = textureSingleton.textureHashToPath[tempTextureHash];
        _renderer->LoadTextureIntoArray(desc, _waterTextures, index);
    }

    textureIndex = (index + 1) - numTextures;

    waterTextureInfo->textureArrayIndex = textureIndex;
    waterTextureInfo->numTextures = numTextures;

    return true;
}
