#include "MaterialRenderer.h"
#include "TerrainRenderer.h"
#include "MapObjectRenderer.h"
#include "CModelRenderer.h"
#include "RenderResources.h"
#include "CVar/CVarSystem.h"
#include "../Utils/ServiceLocator.h"

#include "../ECS/Components/Singletons/MapSingleton.h"

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>

AutoCVar_Int CVAR_VisibilityBufferDebugID("material.visibilityBufferDebugID", "Visibility Buffer Debug ID, between 0-3 inclusive", 0);

MaterialRenderer::MaterialRenderer(Renderer::Renderer* renderer, TerrainRenderer* terrainRenderer, MapObjectRenderer* mapObjectRenderer, CModelRenderer* cModelRenderer)
    : _renderer(renderer)
    , _terrainRenderer(terrainRenderer)
    , _mapObjectRenderer(mapObjectRenderer)
    , _cModelRenderer(cModelRenderer)
{
    CreatePermanentResources();
}

MaterialRenderer::~MaterialRenderer()
{

}

void MaterialRenderer::Update(f32 deltaTime)
{

}

void MaterialRenderer::AddMaterialPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct MaterialPassData
    {
        Renderer::RenderPassMutableResource visibilityBuffer;
        Renderer::RenderPassMutableResource transparency;
        Renderer::RenderPassMutableResource transparencyWeights;
        Renderer::RenderPassMutableResource resolvedColor;
    };

    const i32 visibilityBufferDebugID = Math::Clamp(CVAR_VisibilityBufferDebugID.Get(), 0, 3);

    renderGraph->AddPass<MaterialPassData>("Material Pass",
        [=](MaterialPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::RenderGraphBuilder::WriteMode::UAV, Renderer::RenderGraphBuilder::LoadMode::LOAD);
            data.transparency = builder.Write(resources.transparency, Renderer::RenderGraphBuilder::WriteMode::UAV, Renderer::RenderGraphBuilder::LoadMode::LOAD);
            data.transparencyWeights = builder.Write(resources.transparencyWeights, Renderer::RenderGraphBuilder::WriteMode::UAV, Renderer::RenderGraphBuilder::LoadMode::LOAD);
            data.resolvedColor = builder.Write(resources.resolvedColor, Renderer::RenderGraphBuilder::WriteMode::UAV, Renderer::RenderGraphBuilder::LoadMode::LOAD);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](MaterialPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, MaterialPass);

            commandList.ImageBarrier(resources.visibilityBuffer);
            commandList.ImageBarrier(resources.transparency);
            commandList.ImageBarrier(resources.transparencyWeights);
            commandList.ImageBarrier(resources.resolvedColor);

            Renderer::ComputePipelineDesc pipelineDesc;
            graphResources.InitializePipelineDesc(pipelineDesc);

            Renderer::ComputeShaderDesc shaderDesc;
            shaderDesc.path = "materialPass.cs.hlsl";
            shaderDesc.AddPermutationField("DEBUG_ID", std::to_string(visibilityBufferDebugID));
            pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

            Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);
            commandList.BeginPipeline(pipeline);

            _materialPassDescriptorSet.Bind("_visibilityBuffer", resources.visibilityBuffer);
            _materialPassDescriptorSet.Bind("_transparency", resources.transparency);
            _materialPassDescriptorSet.Bind("_transparencyWeights", resources.transparencyWeights);
            _materialPassDescriptorSet.Bind("_ambientOcclusion", resources.ambientObscurance);
            _materialPassDescriptorSet.BindStorage("_resolvedColor", resources.resolvedColor, 0);

            // Bind descriptorset
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::DEBUG, &resources.debugDescriptorSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, &_materialPassDescriptorSet, frameIndex);

            Renderer::DescriptorSet& terrainDescriptorSet = _terrainRenderer->GetMaterialPassDescriptorSet();
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::TERRAIN, &terrainDescriptorSet, frameIndex);

            Renderer::DescriptorSet& mapObjectDescriptorSet = _mapObjectRenderer->GetMaterialPassDescriptorSet();
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::MAPOBJECT, &mapObjectDescriptorSet, frameIndex);

            Renderer::DescriptorSet& cModelDescriptorSet = _cModelRenderer->GetMaterialPassDescriptorSet();
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::CMODEL, &cModelDescriptorSet, frameIndex);

            const uvec2& outputSize = _renderer->GetImageDimension(resources.resolvedColor, 0);

            uvec2 dispatchSize = uvec2((outputSize.x + 7) / 8, (outputSize.y + 7) / 8);
            commandList.Dispatch(dispatchSize.x, dispatchSize.y, 1);

            commandList.EndPipeline(pipeline);

            commandList.ImageBarrier(resources.resolvedColor);
        });
}

void MaterialRenderer::CreatePermanentResources()
{
    Renderer::SamplerDesc samplerDesc;
    samplerDesc.enabled = true;
    samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
    samplerDesc.addressU = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressV = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.shaderVisibility = Renderer::ShaderVisibility::ALL;

    _sampler = _renderer->CreateSampler(samplerDesc);
    _materialPassDescriptorSet.Bind("_sampler"_h, _sampler);
}
