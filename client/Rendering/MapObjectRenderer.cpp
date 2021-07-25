#include "MapObjectRenderer.h"
#include "DebugRenderer.h"

#include <filesystem>
#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Utils/FileReader.h>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "../ECS/Components/Singletons/TextureSingleton.h"

#include "Camera.h"
#include "../Gameplay/Map/Map.h"
#include "../Gameplay/Map/Chunk.h"
#include "../Gameplay/Map/MapObjectRoot.h"
#include "../Gameplay/Map/MapObject.h"
#include "../Utils/ServiceLocator.h"
#include "CVar/CVarSystem.h"

#define PARALLEL_LOADING 0

namespace fs = std::filesystem;

AutoCVar_Int CVAR_MapObjectOcclusionCullEnabled("mapObjects.occlusionCullEnable", "enable culling of map objects", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_MapObjectCullingEnabled("mapObjects.cullEnable", "enable culling of map objects", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_MapObjectLockCullingFrustum("mapObjects.lockCullingFrustum", "lock frustrum for map object culling", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_MapObjectDrawBoundingBoxes("mapObjects.drawBoundingBoxes", "draw bounding boxes for mapobjects", 0, CVarFlags::EditCheckbox);

MapObjectRenderer::MapObjectRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    , _debugRenderer(debugRenderer)
{
    CreatePermanentResources();
}

void GetFrustumPlanes(const mat4x4& m, vec4* planes)
{
    planes[0] = (m[3] + m[0]);
    planes[1] = (m[3] - m[0]);
    planes[2] = (m[3] + m[1]);
    planes[3] = (m[3] - m[1]);
    planes[4] = (m[3] + m[2]);
    planes[5] = (m[3] - m[2]);
}

void MapObjectRenderer::Update(f32 deltaTime)
{
    bool drawBoundingBoxes = CVAR_MapObjectDrawBoundingBoxes.Get() == 1;
    if (drawBoundingBoxes)
    {
        // Draw bounding boxes
        _drawParameters.ReadLock([&](const std::vector<DrawParameters>& drawParameters)
        {
            for (u32 i = 0; i < drawParameters.size(); i++)
            {
                const DrawParameters& drawParameter = drawParameters[i];
                u32 instanceID = drawParameter.firstInstance;

                const InstanceLookupData& instanceLookupData = _instanceLookupData.ReadGet(instanceID);

                const InstanceData& instanceData = _instances.ReadGet(instanceLookupData.instanceID);

                const Terrain::CullingData& cullingData = _cullingData.ReadGet(instanceLookupData.cullingDataID);

                vec3 center = (cullingData.minBoundingBox + cullingData.maxBoundingBox) * f16(0.5f);
                vec3 extents = vec3(cullingData.maxBoundingBox) - center;

                // transform center
                const mat4x4& m = instanceData.instanceMatrix;
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

    // Read back from the culling counter
    u32 numDrawCalls = static_cast<u32>(_drawParameters.Size());
    _numSurvivingDrawCalls = numDrawCalls;
    _numSurvivingTriangles = _numTriangles;

    const bool cullingEnabled = CVAR_MapObjectCullingEnabled.Get();
    if (cullingEnabled && _drawCountReadBackBuffer != Renderer::BufferID::Invalid())
    {
        // Drawcalls
        {
            u32* count = static_cast<u32*>(_renderer->MapBuffer(_drawCountReadBackBuffer));
            if (count != nullptr)
            {
                _numSurvivingDrawCalls = *count;
            }
            _renderer->UnmapBuffer(_drawCountReadBackBuffer);
        }
        
        // Triangles
        {
            u32* count = static_cast<u32*>(_renderer->MapBuffer(_triangleCountReadBackBuffer));
            if (count != nullptr)
            {
                _numSurvivingTriangles = *count;
            }
            _renderer->UnmapBuffer(_triangleCountReadBackBuffer);
        }
    }
}

void MapObjectRenderer::AddMapObjectDepthPrepass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    // Map Object Depth Prepass
    {
        struct MapObjectDepthPrepassData
        {
            Renderer::RenderPassMutableResource depth;
        };

        const bool cullingEnabled = CVAR_MapObjectCullingEnabled.Get();
        const bool lockFrustum = CVAR_MapObjectLockCullingFrustum.Get();

        renderGraph->AddPass<MapObjectDepthPrepassData>("MapObject Depth Prepass",
            [=](MapObjectDepthPrepassData& data, Renderer::RenderGraphBuilder& builder) // Setup
            {
                data.depth = builder.Write(resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::CLEAR);

                return true; // Return true from setup to enable this pass, return false to disable it
            },
            [=](MapObjectDepthPrepassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
            {
                GPU_SCOPED_PROFILER_ZONE(commandList, MapObjectPass);

                u32 drawCount = static_cast<u32>(_drawParameters.Size());
                if (drawCount == 0)
                    return;

                // -- Cull MapObjects --
                if (cullingEnabled)
                {
                    // Reset the counters
                    commandList.FillBuffer(_drawCountBuffer, 0, 4, 0);
                    commandList.FillBuffer(_triangleCountBuffer, 0, 4, 0);

                    commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _drawCountBuffer);
                    commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _triangleCountBuffer);

                    // Do culling
                    Renderer::ComputePipelineDesc pipelineDesc;
                    graphResources.InitializePipelineDesc(pipelineDesc);

                    Renderer::ComputeShaderDesc shaderDesc;
                    shaderDesc.path = "mapObjectCulling.cs.hlsl";
                    pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

                    Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);
                    commandList.BeginPipeline(pipeline);

                    if (!lockFrustum)
                    {
                        Camera* camera = ServiceLocator::GetCamera();
                        memcpy(_cullingConstantBuffer->resource.frustumPlanes, camera->GetFrustumPlanes(), sizeof(vec4[6]));
                        _cullingConstantBuffer->resource.cameraPos = camera->GetPosition();
                        _cullingConstantBuffer->resource.maxDrawCount = drawCount;
                        _cullingConstantBuffer->resource.occlusionEnabled = CVAR_MapObjectOcclusionCullEnabled.Get();
                        _cullingConstantBuffer->Apply(frameIndex);
                    }

                    _cullingDescriptorSet.Bind("_constants", _cullingConstantBuffer->GetBuffer(frameIndex));
                    _cullingDescriptorSet.Bind("_drawCommands", _argumentBuffer);
                    _cullingDescriptorSet.Bind("_culledDrawCommands", _culledArgumentBuffer);
                    _cullingDescriptorSet.Bind("_drawCount", _drawCountBuffer);
                    _cullingDescriptorSet.Bind("_triangleCount", _triangleCountBuffer);

                    Renderer::SamplerDesc samplerDesc;
                    samplerDesc.filter = Renderer::SamplerFilter::MINIMUM_MIN_MAG_MIP_LINEAR;

                    samplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
                    samplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
                    samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
                    samplerDesc.minLOD = 0.f;
                    samplerDesc.maxLOD = 16.f;
                    samplerDesc.mode = Renderer::SamplerReductionMode::MIN;

                    Renderer::SamplerID occlusionSampler = _renderer->CreateSampler(samplerDesc);

                    _cullingDescriptorSet.Bind("_depthSampler", occlusionSampler);
                    _cullingDescriptorSet.Bind("_depthPyramid", resources.depthPyramid);

                    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, &_cullingDescriptorSet, frameIndex);
                    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);

                    commandList.Dispatch((drawCount + 31) / 32, 1, 1);

                    commandList.EndPipeline(pipeline);

                    commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _culledArgumentBuffer);
                    commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _drawCountBuffer);
                }
                else
                {
                    // Reset the counter
                    commandList.FillBuffer(_drawCountBuffer, 0, 4, drawCount);
                    commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToIndirectArguments, _drawCountBuffer);
                }

                // -- Render MapObjects --
                Renderer::GraphicsPipelineDesc pipelineDesc;
                graphResources.InitializePipelineDesc(pipelineDesc);

                // Shaders
                Renderer::VertexShaderDesc vertexShaderDesc;
                vertexShaderDesc.path = "mapObject.vs.hlsl";
                vertexShaderDesc.AddPermutationField("COLOR_PASS", "0");

                pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

                Renderer::PixelShaderDesc pixelShaderDesc;
                pixelShaderDesc.path = "mapObject.ps.hlsl";
                pixelShaderDesc.AddPermutationField("COLOR_PASS", "0");

                pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

                // Depth state
                pipelineDesc.states.depthStencilState.depthEnable = true;
                pipelineDesc.states.depthStencilState.depthWriteEnable = true;
                pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

                // Rasterizer state
                pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
                pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

                // Render targets
                pipelineDesc.depthStencil = data.depth;

                // Set pipeline
                Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
                commandList.BeginPipeline(pipeline);

                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
                commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, &_passDescriptorSet, frameIndex);

                commandList.SetIndexBuffer(_indexBuffer, Renderer::IndexFormat::UInt16);

                Renderer::BufferID argumentBuffer = (cullingEnabled) ? _culledArgumentBuffer : _argumentBuffer;
                commandList.DrawIndexedIndirectCount(argumentBuffer, 0, _drawCountBuffer, 0, drawCount);

                commandList.EndPipeline(pipeline);

                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, _drawCountBuffer);
                commandList.CopyBuffer(_drawCountReadBackBuffer, 0, _drawCountBuffer, 0, 4);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, _drawCountReadBackBuffer);

                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, _triangleCountBuffer);
                commandList.CopyBuffer(_triangleCountReadBackBuffer, 0, _triangleCountBuffer, 0, 4);
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, _triangleCountReadBackBuffer);
            });
    }
}

void MapObjectRenderer::AddMapObjectPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    // Map Object Pass
    {
        struct MapObjectPassData
        {
            Renderer::RenderPassMutableResource color;
            Renderer::RenderPassMutableResource objectIDs;
            Renderer::RenderPassMutableResource depth;
        };

        const bool cullingEnabled = CVAR_MapObjectCullingEnabled.Get();
        const bool lockFrustum = CVAR_MapObjectLockCullingFrustum.Get();

        renderGraph->AddPass<MapObjectPassData>("MapObject Pass",
            [=](MapObjectPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.color = builder.Write(resources.color, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::CLEAR);
            data.objectIDs = builder.Write(resources.objectIDs, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::CLEAR);
            data.depth = builder.Write(resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::CLEAR);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
            [=](MapObjectPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, MapObjectPass);

            u32 drawCount = static_cast<u32>(_drawParameters.Size());
            if (drawCount == 0)
                return;

            // -- Render MapObjects --
            Renderer::GraphicsPipelineDesc pipelineDesc;
            graphResources.InitializePipelineDesc(pipelineDesc);

            // Shaders
            Renderer::VertexShaderDesc vertexShaderDesc;
            vertexShaderDesc.path = "mapObject.vs.hlsl";
            vertexShaderDesc.AddPermutationField("COLOR_PASS", "1");

            pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

            Renderer::PixelShaderDesc pixelShaderDesc;
            pixelShaderDesc.path = "mapObject.ps.hlsl";
            pixelShaderDesc.AddPermutationField("COLOR_PASS", "1");

            pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

            // Depth state
            pipelineDesc.states.depthStencilState.depthEnable = true;
            pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::EQUAL;

            // Rasterizer state
            pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
            pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

            // Render targets
            pipelineDesc.renderTargets[0] = data.color;
            pipelineDesc.renderTargets[1] = data.objectIDs;

            pipelineDesc.depthStencil = data.depth;

            // Set pipeline
            Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
            commandList.BeginPipeline(pipeline);

            _passDescriptorSet.Bind("_ambientOcclusion", resources.ambientObscurance);

            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, &_passDescriptorSet, frameIndex);

            commandList.SetIndexBuffer(_indexBuffer, Renderer::IndexFormat::UInt16);

            Renderer::BufferID argumentBuffer = (cullingEnabled) ? _culledArgumentBuffer : _argumentBuffer;
            commandList.DrawIndexedIndirectCount(argumentBuffer, 0, _drawCountBuffer, 0, drawCount);

            commandList.EndPipeline(pipeline);
        });
    }
}

void MapObjectRenderer::RegisterMapObjectToBeLoaded(const std::string& mapObjectName, const Terrain::Placement& mapObjectPlacement)
{
    u32 uniqueID = mapObjectPlacement.uniqueID;

    _uniqueIdCounter.WriteLock([&](robin_hood::unordered_map<u32, u8>& uniqueIdCounter)
        {
            if (uniqueIdCounter[uniqueID]++ == 0)
            {
                MapObjectToBeLoaded& mapObjectToBeLoaded = _mapObjectsToBeLoaded.EmplaceBack();
                mapObjectToBeLoaded.placement = &mapObjectPlacement;
                mapObjectToBeLoaded.nmorName = &mapObjectName;
                mapObjectToBeLoaded.nmorNameHash = StringUtils::fnv1a_32(mapObjectName.c_str(), mapObjectName.length());
            }
        });
}

void MapObjectRenderer::RegisterMapObjectsToBeLoaded(u16 chunkID, const Terrain::Chunk& chunk, StringTable& stringTable)
{
    for (u32 i = 0; i < chunk.mapObjectPlacements.size(); i++)
    {
        const Terrain::Placement& mapObjectPlacement = chunk.mapObjectPlacements[i];

        u32 uniqueID = mapObjectPlacement.uniqueID;

        _uniqueIdCounter.WriteLock([&](robin_hood::unordered_map<u32, u8>& uniqueIdCounter)
        {
            if (uniqueIdCounter[uniqueID]++ == 0)
            {
                MapObjectToBeLoaded& mapObjectToBeLoaded = _mapObjectsToBeLoaded.EmplaceBack();
                mapObjectToBeLoaded.placement = &mapObjectPlacement;
                mapObjectToBeLoaded.nmorName = &stringTable.GetString(mapObjectPlacement.nameID);
                mapObjectToBeLoaded.nmorNameHash = stringTable.GetStringHash(mapObjectPlacement.nameID);
            }
        });
    }
}

void MapObjectRenderer::ExecuteLoad()
{
    ZoneScopedN("MapObjectRenderer::ExecuteLoad()");

    std::atomic<size_t> numMapObjectsToLoad = 0;

    _mapObjectsToBeLoaded.WriteLock([&](std::vector<MapObjectToBeLoaded>& mapObjectsToBeLoaded)
        {
            size_t numMapObjectsToBeLoaded = mapObjectsToBeLoaded.size();

            _loadedMapObjects.WriteLock([&](std::vector<LoadedMapObject>& loadedMapObjects)
            {
                loadedMapObjects.reserve(numMapObjectsToBeLoaded);
            });

#if PARALLEL_LOADING
            tf::Taskflow tf;
            tf.parallel_for(mapObjectsToBeLoaded.begin(), mapObjectsToBeLoaded.end(), [&](MapObjectToBeLoaded& mapObjectToBeLoaded)
#else
            for (MapObjectToBeLoaded& mapObjectToBeLoaded : mapObjectsToBeLoaded)
#endif // PARALLEL_LOAD
            {
                ZoneScoped;
                ZoneText(mapObjectToBeLoaded.nmorName->c_str(), mapObjectToBeLoaded.nmorName->length());

                // Placements reference a path to a MapObject, several placements can reference the same object
                // Because of this we want only the first load to actually load the object, subsequent loads should just return the id to the already loaded version
                u32 mapObjectID;
                LoadedMapObject* mapObject = nullptr;

                bool shouldLoad = false;
                _nameHashToIndexMap.WriteLock([&](robin_hood::unordered_map<u32, u32>& nameHashToIndexMap)
                {
                    // See if anything has already loaded this one
                    auto it = nameHashToIndexMap.find(mapObjectToBeLoaded.nmorNameHash);
                    if (it == nameHashToIndexMap.end())
                    {
                        // If it hasn't, we should load it
                        shouldLoad = true;
                        
                        _loadedMapObjects.WriteLock([&](std::vector<LoadedMapObject>& loadedMapObjects)
                        {
                            mapObjectID = static_cast<u32>(loadedMapObjects.size());
                            mapObject = &loadedMapObjects.emplace_back();
                        });

                        nameHashToIndexMap[mapObjectToBeLoaded.nmorNameHash] = mapObjectID; // And set its index so the next thread to check this will find it
                    }
                    else
                    {
                        _loadedMapObjects.WriteLock([&](std::vector<LoadedMapObject>& loadedMapObjects)
                        {
                            mapObject = &loadedMapObjects[it->second];
                        });
                    }
                });

                std::scoped_lock lock(mapObject->mutex);

                if (shouldLoad)
                {
                    mapObject->objectID = mapObjectID;
                    if (!LoadMapObject(mapObjectToBeLoaded, *mapObject))
                    {
                        //_loadedMapObjects.pop_back(); // Is this needed?
#if PARALLEL_LOADING
                        return;
#else
                        continue;
#endif // PARALLEL_LOADING
                    }
                }

                // Add Placement Details (This is used to go from a placement to LoadedMapObject or InstanceData
                Terrain::PlacementDetails& placementDetails = _mapObjectPlacementDetails.EmplaceBack();
                placementDetails.loadedIndex = mapObjectID;

                // Add placement as an instance here
                AddInstance(*mapObject, mapObjectToBeLoaded.placement, placementDetails.instanceIndex);

                numMapObjectsToLoad++;
            }
#if PARALLEL_LOADING
            );
        tf.wait_for_all();
#endif // PARALLEL_LOADING
        });

    _mapObjectsToBeLoaded.Clear();

    if (numMapObjectsToLoad == 0)
        return;

    {
        ZoneScopedN("MapObjectRenderer::ExecuteLoad()::CreateBuffers()");

        CreateBuffers();

        // Calculate triangles
        _numTriangles = 0;

        _drawParameters.ReadLock([&](const std::vector<DrawParameters>& drawParameters)
            {
                for (const DrawParameters& drawParameter : drawParameters)
                {
                    _numTriangles += drawParameter.indexCount / 3;
                }
            });
        
    }
}

void MapObjectRenderer::Clear()
{
    _uniqueIdCounter.Clear();
    _mapObjectPlacementDetails.Clear();
    _loadedMapObjects.Clear();
    _nameHashToIndexMap.Clear();
    _indices.Clear();
    _vertices.Clear();
    _drawParameters.Clear();
    _instances.Clear();
    _instanceLookupData.Clear();
    _materials.Clear();
    _materialParameters.Clear();
    _cullingData.Clear();

    // Unload everything but the first texture in our array
    _renderer->UnloadTexturesInArray(_mapObjectTextures, 1);
}

void MapObjectRenderer::CreatePermanentResources()
{
    Renderer::TextureArrayDesc textureArrayDesc;
    textureArrayDesc.size = 4096;

    _mapObjectTextures = _renderer->CreateTextureArray(textureArrayDesc);
    _passDescriptorSet.Bind("_textures", _mapObjectTextures);

    // Create a 1x1 pixel black texture
    Renderer::DataTextureDesc dataTextureDesc;
    dataTextureDesc.width = 1;
    dataTextureDesc.height = 1;
    dataTextureDesc.format = Renderer::ImageFormat::B8G8R8A8_UNORM;
    dataTextureDesc.data = new u8[4]{ 0, 0, 0, 0 };

    u32 textureID;
    _renderer->CreateDataTextureIntoArray(dataTextureDesc, _mapObjectTextures, textureID);

    delete[] dataTextureDesc.data;

    Renderer::SamplerDesc samplerDesc;
    samplerDesc.enabled = true;
    samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
    samplerDesc.addressU = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressV = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;

    _sampler = _renderer->CreateSampler(samplerDesc);
    _passDescriptorSet.Bind("_sampler", _sampler);

    _cullingConstantBuffer = new Renderer::Buffer<CullingConstants>(_renderer, "CullingConstantBuffer", Renderer::BufferUsage::UNIFORM_BUFFER, Renderer::BufferCPUAccess::WriteOnly);

    // Create draw count buffer
    {
        Renderer::BufferDesc desc;
        desc.name = "MapObjectDrawCount";
        desc.size = sizeof(u32);
        desc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;
        _drawCountBuffer = _renderer->CreateBuffer(_drawCountBuffer, desc);

        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        desc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _drawCountReadBackBuffer = _renderer->CreateBuffer(_drawCountReadBackBuffer, desc);
    }
    
    // Create triangle count buffer
    {
        Renderer::BufferDesc desc;
        desc.name = "MapObjectTriangleCount";
        desc.size = sizeof(u32);
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;
        _triangleCountBuffer = _renderer->CreateBuffer(_triangleCountBuffer, desc);

        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        desc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _triangleCountReadBackBuffer = _renderer->CreateBuffer(_triangleCountReadBackBuffer, desc);
    }
}

bool MapObjectRenderer::LoadMapObject(MapObjectToBeLoaded& mapObjectToBeLoaded, LoadedMapObject& mapObject)
{
    // Load root
    if (!StringUtils::EndsWith(*mapObjectToBeLoaded.nmorName, ".nmor"))
    {
        DebugHandler::PrintFatal("For some reason, a Chunk had a MapObjectPlacement with a reference to a file that didn't end with .nmor");
        return false;
    }

    const std::string& modelPath = *mapObjectToBeLoaded.nmorName;
    mapObject.debugName = modelPath;

    fs::path nmorPath = "Data/extracted/MapObjects/" + *mapObjectToBeLoaded.nmorName;
    nmorPath.make_preferred();
    nmorPath = fs::absolute(nmorPath);

    if (!LoadRoot(nmorPath, mapObjectToBeLoaded.meshRoot, mapObject))
        return false;

    // Load meshes
    std::string nmorNameWithoutExtension = mapObjectToBeLoaded.nmorName->substr(0, mapObjectToBeLoaded.nmorName->length() - 5); // Remove .nmor
    std::stringstream ss;

    for (u32 i = 0; i < mapObjectToBeLoaded.meshRoot.numMeshes; i++)
    {
        ss.clear();
        ss.str("");

        // Load MapObject
        ss << nmorNameWithoutExtension << "_" << std::setw(3) << std::setfill('0') << i << ".nmo";

        fs::path nmoPath = "Data/extracted/MapObjects/" + ss.str();
        nmoPath.make_preferred();
        nmoPath = fs::absolute(nmoPath);

        Mesh& mesh = mapObjectToBeLoaded.meshes.emplace_back();
        if (!LoadMesh(nmoPath, mesh, mapObject))
            return false;
    }

    static u32 vertexColorTextureCount = 0;

    // Create vertex color texture
    for (u32 i = 0; i < 2; i++)
    {
        u32 vertexColorCount = static_cast<u32>(mapObject.vertexColors[i].size());
        if (vertexColorCount > 0)
        {
            // Calculate padded size
            u32 width = 1024;
            u32 height = static_cast<u32>(glm::ceil(static_cast<f32>(vertexColorCount) / static_cast<f32>(width)));

            // Resize the vector
            u32 newVertexColorCount = width * height;
            mapObject.vertexColors[i].resize(newVertexColorCount);

            // Set the padded data to black
            u32 sizeDifference = (newVertexColorCount - vertexColorCount) * sizeof(u32);
            memset(&mapObject.vertexColors[i].data()[vertexColorCount], 0, sizeDifference);

            // Create texture
            Renderer::DataTextureDesc vertexColorTextureDesc;
            vertexColorTextureDesc.debugName = "VertexColorTexture";
            vertexColorTextureDesc.width = width;
            vertexColorTextureDesc.height = height;
            vertexColorTextureDesc.format = Renderer::ImageFormat::B8G8R8A8_UNORM;
            vertexColorTextureDesc.data = reinterpret_cast<u8*>(mapObject.vertexColors[i].data());

            _renderer->CreateDataTextureIntoArray(vertexColorTextureDesc, _mapObjectTextures, mapObject.vertexColorTextureIDs[i]);
            vertexColorTextureCount++;
        }
    }

    // Create per-MapObject culling data
    Terrain::CullingData* mapObjectCullingData = nullptr;

    _cullingData.WriteLock([&](std::vector<Terrain::CullingData>& cullingData)
    {
        mapObject.baseCullingDataOffset = static_cast<u32>(cullingData.size());
        mapObjectCullingData = &cullingData.emplace_back();
    });

    for (const Terrain::CullingData& cullingData : mapObject.cullingData)
    {
        for (u32 i = 0; i < 3; i++)
        {
            if (cullingData.minBoundingBox[i] < mapObjectCullingData->minBoundingBox[i])
            {
                mapObjectCullingData->minBoundingBox[i] = cullingData.minBoundingBox[i];
            }
            if (cullingData.maxBoundingBox[i] > mapObjectCullingData->maxBoundingBox[i])
            {
                mapObjectCullingData->maxBoundingBox[i] = cullingData.maxBoundingBox[i];
            }
        }
    }

    vec3 minPos = mapObjectCullingData->minBoundingBox;
    vec3 maxPos = mapObjectCullingData->maxBoundingBox;

    mapObjectCullingData->boundingSphereRadius = glm::distance(minPos, maxPos) / 2.0f;

    return true;
}

bool MapObjectRenderer::LoadRoot(std::filesystem::path nmorPath, MeshRoot& meshRoot, LoadedMapObject& mapObject)
{
    FileReader nmorFile(nmorPath.string(), nmorPath.filename().string());
    if (!nmorFile.Open())
    {
        DebugHandler::PrintFatal("Failed to load Map Object Root file: %s", nmorPath.string().c_str());
        return false;
    }

    Bytebuffer buffer(nullptr, nmorFile.Length());
    nmorFile.Read(&buffer, buffer.size);
    nmorFile.Close();

    Terrain::MapObjectRootHeader header;

    // Read header
    if (!buffer.Get<Terrain::MapObjectRootHeader>(header))
        return false;

    if (header.token != Terrain::MAP_OBJECT_ROOT_TOKEN)
    {
        DebugHandler::PrintFatal("Found MapObjectRoot file (%s) with invalid token %u instead of expected token %u", nmorPath.string().c_str(), header.token, Terrain::MAP_OBJECT_ROOT_TOKEN);
        return false;
    }

    if (header.version != Terrain::MAP_OBJECT_ROOT_VERSION)
    {
        if (header.version < Terrain::MAP_OBJECT_ROOT_VERSION)
        {
            DebugHandler::PrintFatal("Found MapObjectRoot file (%s) with older version %u instead of expected version %u, rerun dataextractor", nmorPath.string().c_str(), header.version, Terrain::MAP_OBJECT_ROOT_VERSION);
            return false;
        }
        else
        {
            DebugHandler::PrintFatal("Found MapObjectRoot file (%s) with newer version %u instead of expected version %u, update your client", nmorPath.string().c_str(), header.version, Terrain::MAP_OBJECT_ROOT_VERSION);
            return false;
        }
    }

    // Read number of materials
    if (!buffer.Get<u32>(meshRoot.numMaterials))
        return false;

    // Read materials
    entt::registry* registry = ServiceLocator::GetGameRegistry();
    TextureSingleton& textureSingleton = registry->ctx<TextureSingleton>();

    bool failed = false;

    _materials.WriteLock([&](std::vector<Material>& materials)
    {
        mapObject.baseMaterialOffset = static_cast<u32>(materials.size());

        for (u32 i = 0; i < meshRoot.numMaterials; i++)
        {
            Terrain::MapObjectMaterial mapObjectMaterial;
            if (!buffer.GetBytes(reinterpret_cast<u8*>(&mapObjectMaterial), sizeof(Terrain::MapObjectMaterial)))
            {
                failed = true;
                return;
            }

            Material& material = materials.emplace_back();
            material.materialType = mapObjectMaterial.materialType;
            material.unlit = mapObjectMaterial.flags.unlit;

            // TransparencyMode 1 means that it checks the alpha of the texture if it should discard the pixel or not
            if (mapObjectMaterial.transparencyMode == 1)
            {
                material.alphaTestVal = 128.0f / 255.0f;
            }

            constexpr u32 maxTexturesPerMaterial = 3;
            for (u32 j = 0; j < maxTexturesPerMaterial; j++)
            {
                if (mapObjectMaterial.textureNameID[j] < std::numeric_limits<u32>().max())
                {
                    Renderer::TextureDesc textureDesc;
                    textureDesc.path = textureSingleton.textureHashToPath[mapObjectMaterial.textureNameID[j]];

                    u32 textureID;
                    _renderer->LoadTextureIntoArray(textureDesc, _mapObjectTextures, textureID);

                    material.textureIDs[j] = static_cast<u16>(textureID);
                }
            }
        }
    });

    if (failed)
        return false;

    // Read number of meshes
    if (!buffer.Get<u32>(meshRoot.numMeshes))
        return false;

    return true;
}

bool MapObjectRenderer::LoadMesh(const std::filesystem::path nmoPath, Mesh& mesh, LoadedMapObject& mapObject)
{
    FileReader nmoFile(nmoPath.string(), nmoPath.filename().string());
    if (!nmoFile.Open())
    {
        DebugHandler::PrintFatal("Failed to load Map Object file: %s", nmoPath.string().c_str());
        return false;
    }

    Bytebuffer nmoBuffer(nullptr, nmoFile.Length());
    nmoFile.Read(&nmoBuffer, nmoBuffer.size);
    nmoFile.Close();

    // Read header
    Terrain::MapObjectHeader header;
    nmoBuffer.Get<Terrain::MapObjectHeader>(header);

    if (header.token != Terrain::MAP_OBJECT_TOKEN)
    {
        DebugHandler::PrintFatal("Found MapObject file (%s) with invalid token %u instead of expected token %u", nmoPath.string().c_str(), header.token, Terrain::MAP_OBJECT_TOKEN);
        return false;
    }

    if (header.version != Terrain::MAP_OBJECT_VERSION)
    {
        if (header.version < Terrain::MAP_OBJECT_VERSION)
        {
            DebugHandler::PrintFatal("Found MapObject file (%s) with older version %u instead of expected version %u, rerun dataextractor", nmoPath.string().c_str(), header.version, Terrain::MAP_OBJECT_VERSION);
            return false;
        }
        else
        {
            DebugHandler::PrintFatal("Found MapObject file (%s) with newer version %u instead of expected version %u, update your client", nmoPath.string().c_str(), header.version, Terrain::MAP_OBJECT_VERSION);
            return false;
        }
    }

    // Read flags
    if (!nmoBuffer.Get<Terrain::MapObjectFlags>(mesh.renderFlags))
        return false;

    // Read indices and vertices
    if (!LoadIndicesAndVertices(nmoBuffer, mesh, mapObject))
        return false;

    // Read renderbatches
    if (!LoadRenderBatches(nmoBuffer, mesh, mapObject))
        return false;

    return true;
}

bool MapObjectRenderer::LoadIndicesAndVertices(Bytebuffer& buffer, Mesh& mesh, LoadedMapObject& mapObject)
{
    // Read number of indices
    u32 indexCount;
    if (!buffer.Get<u32>(indexCount))
        return false;

    bool failed = false;
    _indices.WriteLock([&](std::vector<u16>& indices)
    {
        mesh.baseIndexOffset = static_cast<u32>(indices.size());
        indices.resize(mesh.baseIndexOffset + indexCount);

        // Read indices
        if (!buffer.GetBytes(reinterpret_cast<u8*>(indices.data() + mesh.baseIndexOffset), indexCount * sizeof(u16)))
        {
            failed = true;
        }
    });

    if (failed) 
        return false;

    // Read number of vertices
    u32 vertexCount;
    if (!buffer.Get<u32>(vertexCount))
        return false;

    _vertices.WriteLock([&](std::vector<Terrain::MapObjectVertex>& vertices)
    {
        mesh.baseVertexOffset = static_cast<u32>(vertices.size());
        vertices.resize(mesh.baseVertexOffset + indexCount);

        // Read vertices
        if (!buffer.GetBytes(reinterpret_cast<u8*>(&vertices.data()[mesh.baseVertexOffset]), vertexCount * sizeof(Terrain::MapObjectVertex)))
        {
            failed = true;
        }
    });

    if (failed)
        return false;
    
    // Read number of vertex color sets
    u32 numVertexColorSets;
    if (!buffer.Get<u32>(numVertexColorSets))
        return false;

    // Vertex colors
    mesh.baseVertexColor1Offset = numVertexColorSets > 0 ? static_cast<u32>(mapObject.vertexColors[0].size()) : std::numeric_limits<u32>().max();
    mesh.baseVertexColor2Offset = numVertexColorSets > 1 ? static_cast<u32>(mapObject.vertexColors[1].size()) : std::numeric_limits<u32>().max();

    for (u32 i = 0; i < numVertexColorSets; i++)
    {
        // Read number of vertex colors
        u32 numVertexColors;
        if (!buffer.Get<u32>(numVertexColors))
            return false;

        if (numVertexColors == 0)
            continue;

        u32 vertexColorSize = numVertexColors * sizeof(u32);
        
        bool failed = false;

        u32 beforeSize = static_cast<u32>(mapObject.vertexColors[i].size());
        mapObject.vertexColors[i].resize(beforeSize + numVertexColors);

        if (!buffer.GetBytes(reinterpret_cast<u8*>(&mapObject.vertexColors[i][beforeSize]), vertexColorSize))
            return false;
    }

    return true;
}

bool MapObjectRenderer::LoadRenderBatches(Bytebuffer& buffer, Mesh& mesh, LoadedMapObject& mapObject)
{
    // Read number of triangle data
    u32 numTriangleData;
    if (!buffer.Get<u32>(numTriangleData))
        return false;

    // Skip triangle data for now
    if (!buffer.SkipRead(numTriangleData * sizeof(Terrain::TriangleData)))
        return false;

    // Read number of RenderBatches
    u32 numRenderBatches;
    if (!buffer.Get<u32>(numRenderBatches))
        return false;

    u32 renderBatchesSize = static_cast<u32>(mapObject.renderBatches.size());
    mapObject.renderBatches.resize(renderBatchesSize + numRenderBatches);

    // Read RenderBatches
    if (!buffer.GetBytes(reinterpret_cast<u8*>(&mapObject.renderBatches.data()[renderBatchesSize]), numRenderBatches * sizeof(Terrain::RenderBatch)))
        return false;

    mapObject.renderBatchOffsets.reserve(renderBatchesSize + numRenderBatches);
    for (u32 i = 0; i < numRenderBatches; i++)
    {
        RenderBatchOffsets& renderBatchOffsets = mapObject.renderBatchOffsets.emplace_back();
        renderBatchOffsets.baseVertexOffset = mesh.baseVertexOffset;
        renderBatchOffsets.baseIndexOffset = mesh.baseIndexOffset;
        renderBatchOffsets.baseVertexColor1Offset = mesh.baseVertexColor1Offset;
        renderBatchOffsets.baseVertexColor2Offset = mesh.baseVertexColor2Offset;

        u32 renderBatchIndex = renderBatchesSize + i;
        Terrain::RenderBatch& renderBatch = mapObject.renderBatches[renderBatchIndex];
        // MaterialParameters
        MaterialParameters* materialParameter = nullptr;
        u32 materialParameterID;

        _materialParameters.WriteLock([&](std::vector<MaterialParameters>& materialParameters)
        {
            materialParameterID = static_cast<u32>(materialParameters.size());
            materialParameter = &materialParameters.emplace_back();
        });

        mapObject.materialParameterIDs.push_back(materialParameterID);

        materialParameter->materialID = mapObject.baseMaterialOffset + renderBatch.materialID;
        materialParameter->exteriorLit = static_cast<u32>(mesh.renderFlags.exteriorLit || mesh.renderFlags.exterior);
    }

    // Read culling data
    u32 cullingDataSize = static_cast<u32>(mapObject.cullingData.size());

    mapObject.cullingData.resize(cullingDataSize + numRenderBatches);

    if (!buffer.GetBytes(reinterpret_cast<u8*>(&mapObject.cullingData.data()[cullingDataSize]), numRenderBatches * sizeof(Terrain::CullingData)))
        return false;

    return true;
}

void MapObjectRenderer::AddInstance(LoadedMapObject& mapObject, const Terrain::Placement* placement, u32& instanceIndex)
{
    InstanceData* instance = nullptr;
    _instances.WriteLock([&](std::vector<InstanceData>& instances)
    {
        instanceIndex = static_cast<u32>(instances.size());
        instance = &instances.emplace_back();
    });

    mapObject.instanceIDs.push_back(instanceIndex);

    vec3 pos = placement->position;
    vec3 rot = glm::radians(placement->rotation);
    mat4x4 rotationMatrix = glm::eulerAngleZYX(rot.z, -rot.y, -rot.x);

    instance->instanceMatrix = glm::translate(mat4x4(1.0f), pos) * rotationMatrix;

    for (u32 i = 0; i < mapObject.renderBatches.size(); i++)
    {
        Terrain::RenderBatch& renderBatch = mapObject.renderBatches[i];
        RenderBatchOffsets& renderBatchOffsets = mapObject.renderBatchOffsets[i];

        u32 drawParameterID;
        DrawParameters* drawParameter = nullptr;

        _drawParameters.WriteLock([&](std::vector<DrawParameters>& drawParameters)
        {
            drawParameterID = static_cast<u32>(drawParameters.size());
            drawParameter = &drawParameters.emplace_back();
        });

        mapObject.drawParameterIDs.push_back(drawParameterID);

        drawParameter->vertexOffset = renderBatchOffsets.baseVertexOffset;
        drawParameter->firstIndex = renderBatchOffsets.baseIndexOffset + renderBatch.startIndex;
        drawParameter->indexCount = renderBatch.indexCount;
        drawParameter->firstInstance = drawParameterID;
        drawParameter->instanceCount = 1;

        InstanceLookupData& instanceLookupData = _instanceLookupData.EmplaceBack(); // NOTE: If things load in weird places, investigate this
        instanceLookupData.loadedObjectID = mapObject.objectID;
        instanceLookupData.instanceID = instanceIndex;
        instanceLookupData.materialParamID = mapObject.materialParameterIDs[i];
        instanceLookupData.cullingDataID = mapObject.baseCullingDataOffset;

        instanceLookupData.vertexColorTextureID0 = static_cast<u16>(mapObject.vertexColorTextureIDs[0]);
        instanceLookupData.vertexColorTextureID1 = static_cast<u16>(mapObject.vertexColorTextureIDs[1]);
        instanceLookupData.vertexOffset = renderBatchOffsets.baseVertexOffset;
        instanceLookupData.vertexColor1Offset = renderBatchOffsets.baseVertexColor1Offset;
        instanceLookupData.vertexColor2Offset = renderBatchOffsets.baseVertexColor2Offset;
    }

    mapObject.instanceCount++;
}

void MapObjectRenderer::CreateBuffers()
{
    // Create Instance Lookup Buffer
    _instanceLookupData.WriteLock([&](std::vector<InstanceLookupData>& instanceLookupData)
    {
        Renderer::BufferDesc desc;
        desc.name = "InstanceLookupDataBuffer";
        desc.size = sizeof(InstanceLookupData) * instanceLookupData.size();
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        _instanceLookupBuffer = _renderer->CreateAndFillBuffer(_instanceLookupBuffer, desc, instanceLookupData.data(), desc.size);

        _passDescriptorSet.Bind("_packedInstanceLookup", _instanceLookupBuffer);
        _cullingDescriptorSet.Bind("_packedInstanceLookup", _instanceLookupBuffer);
    });
    
    
    _drawParameters.WriteLock([&](std::vector<DrawParameters>& drawParameters)
    {
        // Create Indirect Argument buffer
        Renderer::BufferDesc desc;
        desc.name = "MapObjectIndirectArgs";
        desc.size = sizeof(DrawParameters) * drawParameters.size();
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER;
        _argumentBuffer = _renderer->CreateAndFillBuffer(_argumentBuffer, desc, drawParameters.data(), desc.size);

        // Create Culled Indirect Argument buffer
        desc.name = "MapObjectCulledIndirectArgs";
        _culledArgumentBuffer = _renderer->CreateAndFillBuffer(_culledArgumentBuffer, desc, drawParameters.data(), desc.size);
    });

    // Create Vertex buffer
    _vertices.WriteLock([&](std::vector<Terrain::MapObjectVertex>& vertices)
    {
        Renderer::BufferDesc desc;
        desc.name = "MapObjectVertexBuffer";
        desc.size = sizeof(Terrain::MapObjectVertex) * vertices.size();
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        _vertexBuffer = _renderer->CreateAndFillBuffer(_vertexBuffer, desc, vertices.data(), desc.size);

        _passDescriptorSet.Bind("_packedVertices", _vertexBuffer);
    });

    // Create Index buffer
    _indices.WriteLock([&](std::vector<u16>& indices)
    {
        Renderer::BufferDesc desc;
        desc.name = "MapObjectIndexBuffer";
        desc.size = sizeof(u16) * indices.size();
        desc.usage = Renderer::BufferUsage::INDEX_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        _indexBuffer = _renderer->CreateAndFillBuffer(_indexBuffer, desc, indices.data(), desc.size);
    });

    // Create Instance buffer
    _instances.WriteLock([&](std::vector<InstanceData>& instances)
    {
        Renderer::BufferDesc desc;
        desc.name = "MapObjectInstanceBuffer";
        desc.size = sizeof(InstanceData) * instances.size();
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        _instanceBuffer = _renderer->CreateAndFillBuffer(_instanceBuffer, desc, instances.data(), desc.size);

        _passDescriptorSet.Bind("_instanceData", _instanceBuffer);
        _cullingDescriptorSet.Bind("_instanceData", _instanceBuffer);
    });

    // Create Material buffer
    _materials.WriteLock([&](std::vector<Material>& materials)
    {
        Renderer::BufferDesc desc;
        desc.name = "MapObjectMaterialBuffer";
        desc.size = sizeof(Material) * materials.size();
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        _materialBuffer = _renderer->CreateAndFillBuffer(_materialBuffer, desc, materials.data(), desc.size);

        _passDescriptorSet.Bind("_packedMaterialData", _materialBuffer);
    });

    // Create MaterialParam buffer
    _materialParameters.WriteLock([&](std::vector<MaterialParameters>& materialParameters)
    {
        Renderer::BufferDesc desc;
        desc.name = "MapObjectMaterialParamBuffer";
        desc.size = sizeof(MaterialParameters) * materialParameters.size();
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        _materialParametersBuffer = _renderer->CreateAndFillBuffer(_materialParametersBuffer, desc, materialParameters.data(), desc.size);

        _passDescriptorSet.Bind("_packedMaterialParams", _materialParametersBuffer);
    });

    // Create CullingData buffer
    _cullingData.WriteLock([&](std::vector<Terrain::CullingData>& cullingData)
    {
        Renderer::BufferDesc desc;
        desc.name = "MapObjectCullingDataBuffer";
        desc.size = sizeof(Terrain::CullingData) * cullingData.size();
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        _cullingDataBuffer = _renderer->CreateAndFillBuffer(_cullingDataBuffer, desc, cullingData.data(), desc.size);

        _cullingDescriptorSet.Bind("_packedCullingData", _cullingDataBuffer);
    });
}
