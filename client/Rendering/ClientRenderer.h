#pragma once
#include <NovusTypes.h>

#include <Renderer/Descriptors/SamplerDesc.h>
#include <Renderer/Descriptors/SemaphoreDesc.h>

#include <Renderer/FrameResource.h>

#include "RenderResources.h"

namespace Renderer
{
    class Renderer;
}

namespace Memory
{
    class StackAllocator;
}

class Window;
class CameraFreeLook;
class UIRenderer;
class TerrainRenderer;
class MapObjectRenderer;
class CModelRenderer;
class WaterRenderer;
class MaterialRenderer;
class SkyboxRenderer;
class PostProcessRenderer;
class RendertargetVisualizer;
class InputManager;
class DebugRenderer;
class PixelQuery;

class ClientRenderer
{
public:
    ClientRenderer();

    bool UpdateWindow(f32 deltaTime);
    void Update(f32 deltaTime);
    void Render();

    u8 GetFrameIndex() { return _frameIndex; }
    uvec2 GetRenderResolution();

    void InitImgui();
    UIRenderer* GetUIRenderer() { return _uiRenderer; }
    TerrainRenderer* GetTerrainRenderer() { return _terrainRenderer; }
    MapObjectRenderer* GetMapObjectRenderer() { return _mapObjectRenderer; }
    CModelRenderer* GetCModelRenderer() { return _cModelRenderer; }
    WaterRenderer* GetWaterRenderer() { return _waterRenderer; }
    DebugRenderer* GetDebugRenderer() { return _debugRenderer; }
    RendertargetVisualizer* GetRendertargetVisualizer() { return _rendertargetVisualizer; }
    PixelQuery* GetPixelQuery() { return _pixelQuery; }

    void ReloadShaders(bool forceRecompileAll);

    const std::string& GetGPUName();

    size_t GetVRAMUsage();
    size_t GetVRAMBudget();

private:
    void CreatePermanentResources();

private:
    Window* _window;
    InputManager* _inputManager;
    Renderer::Renderer* _renderer;
    Memory::StackAllocator* _frameAllocator;

    u8 _frameIndex = 0;

    RenderResources _resources;

    Renderer::SemaphoreID _sceneRenderedSemaphore; // This semaphore tells the present function when the scene is ready to be blitted and presented
    FrameResource<Renderer::SemaphoreID, 2> _frameSyncSemaphores; // This semaphore makes sure the GPU handles frames in order

    // Sub renderers
    DebugRenderer* _debugRenderer;
    UIRenderer* _uiRenderer;
    TerrainRenderer* _terrainRenderer;
    MapObjectRenderer* _mapObjectRenderer;
    CModelRenderer* _cModelRenderer;
    WaterRenderer* _waterRenderer;
    MaterialRenderer* _materialRenderer;
    SkyboxRenderer* _skyboxRenderer;
    PostProcessRenderer* _postProcessRenderer;
    RendertargetVisualizer* _rendertargetVisualizer;

    PixelQuery* _pixelQuery;

    bool _isMinimized = false;
};