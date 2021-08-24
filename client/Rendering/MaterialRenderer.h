#pragma once
#include <NovusTypes.h>
#include <Renderer/DescriptorSet.h>

namespace Renderer
{
    class RenderGraph;
    class Renderer;
}

class TerrainRenderer;
class MapObjectRenderer;
class CModelRenderer;
struct RenderResources;

class MaterialRenderer
{
public:
    MaterialRenderer(Renderer::Renderer* renderer, TerrainRenderer* terrainRenderer, MapObjectRenderer* mapObjectRenderer, CModelRenderer* cModelRenderer);
    ~MaterialRenderer();

    void Update(f32 deltaTime);

    void AddMaterialPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

private:
    void CreatePermanentResources();

private:
    Renderer::Renderer* _renderer;

    Renderer::DescriptorSet _materialPassDescriptorSet;

    Renderer::SamplerID _sampler;

    TerrainRenderer* _terrainRenderer = nullptr;
    MapObjectRenderer* _mapObjectRenderer = nullptr;
    CModelRenderer* _cModelRenderer = nullptr;
};