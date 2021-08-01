#include "ClientRenderer.h"
#include "UIRenderer.h"
#include "TerrainRenderer.h"
#include "CModelRenderer.h"
#include "SkyboxRenderer.h"
#include "PostProcessRenderer.h"
#include "RendertargetVisualizer.h"
#include "DebugRenderer.h"
#include "PixelQuery.h"
#include "CameraFreelook.h"
#include "../Utils/ServiceLocator.h"
#include "../ECS/Components/Singletons/MapSingleton.h"
#include "../ECS/Components/Singletons/AreaUpdateSingleton.h"

#include <Memory/StackAllocator.h>
#include <Renderer/Renderer.h>
#include <Renderer/RenderSettings.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Renderers/Vulkan/RendererVK.h>
#include <Window/Window.h>
#include <InputManager.h>
#include <GLFW/glfw3.h>
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include "imgui/imgui_impl_glfw.h"
#include "imgui/implot.h"

#include "Renderer/Renderers/Vulkan/RendererVK.h"
#include "CullUtils.h"

AutoCVar_Int CVAR_LightLockEnabled("lights.lock", "lock the light", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_LightUseDefaultEnabled("lights.useDefault", "Use the map's default light", 0, CVarFlags::EditCheckbox);

const size_t FRAME_ALLOCATOR_SIZE = 16 * 1024 * 1024; // 16 MB
u32 MAIN_RENDER_LAYER = "MainLayer"_h; // _h will compiletime hash the string into a u32
u32 DEPTH_PREPASS_RENDER_LAYER = "DepthPrepass"_h; // _h will compiletime hash the string into a u32

void KeyCallback(GLFWwindow* window, i32 key, i32 scancode, i32 action, i32 modifiers)
{
    ServiceLocator::GetInputManager()->KeyboardInputHandler(key, scancode, action, modifiers);
}

void CharCallback(GLFWwindow* window, u32 unicodeKey)
{
    ServiceLocator::GetInputManager()->CharInputHandler(unicodeKey);
}

void MouseCallback(GLFWwindow* window, i32 button, i32 action, i32 modifiers)
{
    ServiceLocator::GetInputManager()->MouseInputHandler(button, action, modifiers);
}

void CursorPositionCallback(GLFWwindow* window, f64 x, f64 y)
{
    ServiceLocator::GetInputManager()->MousePositionHandler(static_cast<f32>(x), static_cast<f32>(y));
}

void ScrollCallback(GLFWwindow* window, f64 x, f64 y)
{
    ServiceLocator::GetInputManager()->MouseScrollHandler(static_cast<f32>(x), static_cast<f32>(y));
}

void WindowIconifyCallback(GLFWwindow* window, int iconified)
{
    Window* userWindow = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    userWindow->SetIsMinimized(iconified == 1);
}

ClientRenderer::ClientRenderer()
{
    _window = new Window();
    _window->Init(Renderer::Settings::SCREEN_WIDTH, Renderer::Settings::SCREEN_HEIGHT);
    ServiceLocator::SetWindow(_window);

    _inputManager = new InputManager();
    ServiceLocator::SetInputManager(_inputManager);

    glfwSetKeyCallback(_window->GetWindow(), KeyCallback);
    glfwSetCharCallback(_window->GetWindow(), CharCallback);
    glfwSetMouseButtonCallback(_window->GetWindow(), MouseCallback);
    glfwSetCursorPosCallback(_window->GetWindow(), CursorPositionCallback);
    glfwSetScrollCallback(_window->GetWindow(), ScrollCallback);
    glfwSetWindowIconifyCallback(_window->GetWindow(), WindowIconifyCallback);
    
    _renderer = new Renderer::RendererVK();
    _renderer->InitWindow(_window);

    InitImgui();

    ServiceLocator::SetRenderer(_renderer);
    ServiceLocator::SetClientRenderer(this);

    CreatePermanentResources();

    _debugRenderer = new DebugRenderer(_renderer, _resources);
    _uiRenderer = new UIRenderer(_renderer, _debugRenderer);
    _cModelRenderer = new CModelRenderer(_renderer, _debugRenderer);
    _skyboxRenderer = new SkyboxRenderer(_renderer, _debugRenderer);
    _postProcessRenderer = new PostProcessRenderer(_renderer);
    _rendertargetVisualizer = new RendertargetVisualizer(_renderer);
    _terrainRenderer = new TerrainRenderer(_renderer, _debugRenderer, _cModelRenderer);
    _pixelQuery = new PixelQuery(_renderer);
}

bool ClientRenderer::UpdateWindow(f32 deltaTime)
{
    return _window->Update(deltaTime);
}

void ClientRenderer::Update(f32 deltaTime)
{
    // Reset the memory in the frameAllocator
    _frameAllocator->Reset();

    _terrainRenderer->Update(deltaTime);
    _cModelRenderer->Update(deltaTime);
    _postProcessRenderer->Update(deltaTime);
    _rendertargetVisualizer->Update(deltaTime);
    _pixelQuery->Update(deltaTime);
    _uiRenderer->Update(deltaTime);

    _debugRenderer->DrawLine3D(vec3(0.0f, 0.0f, 0.0f), vec3(100.0f, 0.0f, 0.0f), 0xff0000ff);
    _debugRenderer->DrawLine3D(vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 100.0f, 0.0f), 0xff00ff00);
    _debugRenderer->DrawLine3D(vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, 100.0f), 0xffff0000);
}

void ClientRenderer::Render()
{
    ZoneScopedNC("ClientRenderer::Render", tracy::Color::Red2);

    // If the window is minimized we want to pause rendering
    if (_window->IsMinimized())
        return;

    Camera* camera = ServiceLocator::GetCamera();

    // Create rendergraph
    Renderer::RenderGraphDesc renderGraphDesc;
    renderGraphDesc.allocator = _frameAllocator; // We need to give our rendergraph an allocator to use
    Renderer::RenderGraph renderGraph = _renderer->CreateRenderGraph(renderGraphDesc);

    _renderer->FlipFrame(_frameIndex);

    // Get lastAO and set it in resources so we can use it later
    _resources.ambientObscurance = _postProcessRenderer->GetAOImage(_frameIndex);

    // Update the view matrix to match the new camera position
    _resources.viewConstantBuffer->resource.lastViewProjectionMatrix = _resources.viewConstantBuffer->resource.viewProjectionMatrix;
    _resources.viewConstantBuffer->resource.viewProjectionMatrix = camera->GetViewProjectionMatrix();
    _resources.viewConstantBuffer->resource.viewMatrix = camera->GetViewMatrix();
    _resources.viewConstantBuffer->resource.eyePosition = vec4(camera->GetPosition(), 0.0f);
    _resources.viewConstantBuffer->resource.eyeRotation = vec4(camera->GetRotation(), 0.0f);
    _resources.viewConstantBuffer->Apply(_frameIndex);

    entt::registry* registry = ServiceLocator::GetGameRegistry();
    MapSingleton& mapSingleton = registry->ctx<MapSingleton>();

    if (!CVAR_LightLockEnabled.Get())
    {
        AreaUpdateLightColorData lightColor = mapSingleton.GetLightColorData();
        _resources.lightConstantBuffer->resource.ambientColor = vec4(lightColor.ambientColor, 1.0f);
        _resources.lightConstantBuffer->resource.lightColor = vec4(lightColor.diffuseColor, 1.0f);
        _resources.lightConstantBuffer->resource.lightDir = vec4(mapSingleton.GetLightDirection(), 1.0f);
        _resources.lightConstantBuffer->Apply(_frameIndex);
    }

    _resources.globalDescriptorSet.Bind("_viewData"_h, _resources.viewConstantBuffer->GetBuffer(_frameIndex));
    _resources.globalDescriptorSet.Bind("_lightData"_h, _resources.lightConstantBuffer->GetBuffer(_frameIndex));

    _debugRenderer->AddUploadPass(&renderGraph);

    // StartFrame Pass
    {
        struct StartFramePassData
        {
            Renderer::RenderPassMutableResource color;
            Renderer::RenderPassMutableResource objectIDs;
            Renderer::RenderPassMutableResource depth;
        };

        renderGraph.AddPass<StartFramePassData>("StartFramePass",
            [=](StartFramePassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.color = builder.Write(_resources.color, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::CLEAR);
            data.objectIDs = builder.Write(_resources.objectIDs, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::CLEAR);
            data.depth = builder.Write(_resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::CLEAR);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
            [&](StartFramePassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, MainPass);
            commandList.MarkFrameStart(_frameIndex);

            // Set viewport
            commandList.SetViewport(0, 0, static_cast<f32>(Renderer::Settings::SCREEN_WIDTH), static_cast<f32>(Renderer::Settings::SCREEN_HEIGHT), 0.0f, 1.0f);
            commandList.SetScissorRect(0, Renderer::Settings::SCREEN_WIDTH, 0, Renderer::Settings::SCREEN_HEIGHT);
        });
    }

    // Depth Prepass
    _terrainRenderer->AddTerrainDepthPrepass(&renderGraph, _resources, _frameIndex);
    _cModelRenderer->AddComplexModelDepthPrepass(&renderGraph, _resources, _frameIndex);

    // Calculate SAO
    _postProcessRenderer->AddCalculateSAOPass(&renderGraph, _resources, _frameIndex);

    // Color pass
    _terrainRenderer->AddTerrainPass(&renderGraph, _resources, _frameIndex);
    _cModelRenderer->AddComplexModelPass(&renderGraph, _resources, _frameIndex);

    // Skybox
    _skyboxRenderer->AddSkyboxPass(&renderGraph, _resources, _frameIndex);

    // Postprocessing
    _postProcessRenderer->AddPostProcessPass(&renderGraph, _resources, _frameIndex);
    _rendertargetVisualizer->AddVisualizerPass(&renderGraph, _resources, _frameIndex);

    // UI Pass
    struct PyramidPassData
    {
        Renderer::RenderPassResource depth;
    };

    renderGraph.AddPass<PyramidPassData>("PyramidPass",
        [=](PyramidPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.depth = builder.Read(_resources.depth, Renderer::RenderGraphBuilder::ShaderStage::PIXEL);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](PyramidPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, BuildPyramid);

            DepthPyramidUtils::BuildPyramid(_renderer, graphResources, commandList, _resources, _frameIndex);
        });

    _pixelQuery->AddPixelQueryPass(&renderGraph, _resources, _frameIndex);

    _debugRenderer->AddDrawArgumentPass(&renderGraph, _frameIndex);
    _debugRenderer->Add3DPass(&renderGraph, _resources, _frameIndex);

    _uiRenderer->AddUIPass(&renderGraph, _resources, _frameIndex);

    _debugRenderer->Add2DPass(&renderGraph, _resources, _frameIndex);

    _uiRenderer->AddImguiPass(&renderGraph, _resources, _frameIndex);

    renderGraph.AddSignalSemaphore(_sceneRenderedSemaphore); // Signal that we are ready to present
    renderGraph.AddSignalSemaphore(_frameSyncSemaphores.Get(_frameIndex)); // Signal that this frame has finished, for next frames sake

    static bool firstFrame = true;
    if (firstFrame)
    {
        firstFrame = false;
    }
    else
    {
        renderGraph.AddWaitSemaphore(_frameSyncSemaphores.Get(!_frameIndex)); // Wait for previous frame to finish
    }

    if (_renderer->ShouldWaitForUpload())
    {
        renderGraph.AddWaitSemaphore(_renderer->GetUploadFinishedSemaphore());
        _renderer->SetHasWaitedForUpload();
    }

    renderGraph.Setup();
    renderGraph.Execute();
    
    {
        ZoneScopedNC("Present", tracy::Color::Red2);
        _renderer->Present(_window, _resources.color, _sceneRenderedSemaphore);
    }

    // Flip the frameIndex between 0 and 1
    _frameIndex = !_frameIndex;
}

uvec2 ClientRenderer::GetRenderResolution()
{
    return _renderer->GetImageDimension(_resources.color, 0);
}

void ClientRenderer::InitImgui()
{
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(_window->GetWindow(),true);

    _renderer->InitImgui();
}

void ClientRenderer::ReloadShaders(bool forceRecompileAll)
{
    _renderer->ReloadShaders(forceRecompileAll);
}

const std::string& ClientRenderer::GetGPUName()
{
    return _renderer->GetGPUName();
}

size_t ClientRenderer::GetVRAMUsage()
{
    return _renderer->GetVRAMUsage();
}

size_t ClientRenderer::GetVRAMBudget()
{
    return _renderer->GetVRAMBudget();
}

void ClientRenderer::CreatePermanentResources()
{
    // Main color rendertarget
    Renderer::ImageDesc mainColorDesc;
    mainColorDesc.debugName = "MainColor";
    mainColorDesc.dimensions = vec2(1.0f, 1.0f);
    mainColorDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE;
    mainColorDesc.format = Renderer::ImageFormat::R16G16B16A16_FLOAT;
    mainColorDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    mainColorDesc.clearColor = Color::Clear;

    _resources.color = _renderer->CreateImage(mainColorDesc);

    // Object ID rendertarget
    Renderer::ImageDesc objectIDsDesc;
    objectIDsDesc.debugName = "ObjectIDs";
    objectIDsDesc.dimensions = vec2(1.0f, 1.0f);
    objectIDsDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE;
    objectIDsDesc.format = Renderer::ImageFormat::R32_UINT;
    objectIDsDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    objectIDsDesc.clearColor = Color::Clear;

    _resources.objectIDs = _renderer->CreateImage(objectIDsDesc);

    // depth pyramid ID rendertarget
    Renderer::ImageDesc pyramidDesc;
    pyramidDesc.debugName = "DepthPyramid";
    pyramidDesc.dimensions = vec2(1.0f, 1.0f);
    pyramidDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_PYRAMID;
    pyramidDesc.format = Renderer::ImageFormat::R32_FLOAT;
    pyramidDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    _resources.depthPyramid = _renderer->CreateImage(pyramidDesc);

    // Main depth rendertarget
    Renderer::DepthImageDesc mainDepthDesc;
    mainDepthDesc.debugName = "MainDepth";
    mainDepthDesc.dimensions = vec2(1.0f, 1.0f);
    mainDepthDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE;
    mainDepthDesc.format = Renderer::DepthImageFormat::D32_FLOAT;
    mainDepthDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    mainDepthDesc.depthClearValue = 0.0f;

    _resources.depth = _renderer->CreateDepthImage(mainDepthDesc);

    // View Constant Buffer (for camera data)
    _resources.viewConstantBuffer = new Renderer::Buffer<ViewConstantBuffer>(_renderer, "ViewConstantBuffer", Renderer::BufferUsage::UNIFORM_BUFFER, Renderer::BufferCPUAccess::WriteOnly);

    // Light Constant Buffer
    _resources.lightConstantBuffer = new Renderer::Buffer<LightConstantBuffer>(_renderer, "LightConstantBufffer", Renderer::BufferUsage::UNIFORM_BUFFER, Renderer::BufferCPUAccess::WriteOnly);

    // Frame allocator, this is a fast allocator for data that is only needed this frame
    _frameAllocator = new Memory::StackAllocator();
    _frameAllocator->Init(FRAME_ALLOCATOR_SIZE);

    _sceneRenderedSemaphore = _renderer->CreateNSemaphore();
    for (u32 i = 0; i < _frameSyncSemaphores.Num; i++)
    {
        _frameSyncSemaphores.Get(i) = _renderer->CreateNSemaphore();
    }
}
