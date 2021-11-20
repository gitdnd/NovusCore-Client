#include "ClientRenderer.h"
#include "UIRenderer.h"
#include "TerrainRenderer.h"
#include "MapObjectRenderer.h"
#include "CModelRenderer.h"
#include "MaterialRenderer.h"
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
    _mapObjectRenderer = new MapObjectRenderer(_renderer, _debugRenderer);
    _terrainRenderer = new TerrainRenderer(_renderer, _debugRenderer, _mapObjectRenderer, _cModelRenderer);
    _materialRenderer = new MaterialRenderer(_renderer, _terrainRenderer, _mapObjectRenderer, _cModelRenderer);
    _pixelQuery = new PixelQuery(_renderer);

    DepthPyramidUtils::InitBuffers(_renderer);
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
    _mapObjectRenderer->Update(deltaTime);
    _cModelRenderer->Update(deltaTime);
    _postProcessRenderer->Update(deltaTime);
    _rendertargetVisualizer->Update(deltaTime);
    _pixelQuery->Update(deltaTime);
    _uiRenderer->Update(deltaTime);

    _debugRenderer->DrawLine3D(vec3(0.0f, 0.0f, 0.0f), vec3(100.0f, 0.0f, 0.0f), 0xff0000ff);
    _debugRenderer->DrawLine3D(vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 100.0f, 0.0f), 0xff00ff00);
    _debugRenderer->DrawLine3D(vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, 100.0f), 0xffff0000);

    _debugRenderer->Update(deltaTime);
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

    // Flip Y & Z + Negate new Y
    mat4x4 axisFlipMatrix = mat4x4(1, 0, 0, 0,
                                   0, 0, -1, 0,
                                   0, 1, 0, 0,
                                   0, 0, 0, 1);

    axisFlipMatrix = glm::rotate(axisFlipMatrix, glm::radians(90.0f), glm::vec3(0, 0, 1));

    // Update the view matrix to match the new camera position
    _resources.viewConstantBuffer->resource.lastViewProjectionMatrix = _resources.viewConstantBuffer->resource.viewProjectionMatrix;
    _resources.viewConstantBuffer->resource.viewProjectionMatrix = camera->GetProjectionMatrix() * (axisFlipMatrix * camera->GetViewMatrix());
    _resources.viewConstantBuffer->resource.viewMatrix = axisFlipMatrix * camera->GetViewMatrix();
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

    // StartFrame Pass
    {
        struct StartFramePassData
        {
            Renderer::RenderPassMutableResource visibilityBuffer;
            Renderer::RenderPassMutableResource resolvedColor;
            Renderer::RenderPassMutableResource depth;
        };

        renderGraph.AddPass<StartFramePassData>("StartFramePass",
            [=](StartFramePassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.visibilityBuffer = builder.Write(_resources.visibilityBuffer, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::CLEAR);
            data.resolvedColor = builder.Write(_resources.resolvedColor, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::CLEAR);
            data.depth = builder.Write(_resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::CLEAR);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
            [&](StartFramePassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, StartFramePass);
            commandList.MarkFrameStart(_frameIndex);

            // Set viewport
            commandList.SetViewport(0, 0, static_cast<f32>(Renderer::Settings::SCREEN_WIDTH), static_cast<f32>(Renderer::Settings::SCREEN_HEIGHT), 0.0f, 1.0f);
            commandList.SetScissorRect(0, Renderer::Settings::SCREEN_WIDTH, 0, Renderer::Settings::SCREEN_HEIGHT);
        });
    }

    // Animation Pass
    _cModelRenderer->AddAnimationPass(&renderGraph, _resources, _frameIndex);

    // Occluder Pass
    _terrainRenderer->AddOccluderPass(&renderGraph, _resources, _frameIndex);
    _mapObjectRenderer->AddOccluderPass(&renderGraph, _resources, _frameIndex);
    _cModelRenderer->AddOccluderPass(&renderGraph, _resources, _frameIndex);

    // Depth Pyramid Pass
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

            DepthPyramidUtils::BuildPyramid2(_renderer, graphResources, commandList, _resources, _frameIndex);
        });

    // Culling Pass
    _terrainRenderer->AddCullingPass(&renderGraph, _resources, _frameIndex);
    _mapObjectRenderer->AddCullingPass(&renderGraph, _resources, _frameIndex);
    _cModelRenderer->AddCullingPass(&renderGraph, _resources, _frameIndex);

    // Geometry Pass
    _terrainRenderer->AddGeometryPass(&renderGraph, _resources, _frameIndex);
    _mapObjectRenderer->AddGeometryPass(&renderGraph, _resources, _frameIndex);
    _cModelRenderer->AddGeometryPass(&renderGraph, _resources, _frameIndex);

    // Skybox
    _skyboxRenderer->AddSkyboxPass(&renderGraph, _resources, _frameIndex);

    // Transparency pass
    _cModelRenderer->AddTransparencyPass(&renderGraph, _resources, _frameIndex);

    // Calculate SAO
    _postProcessRenderer->AddCalculateSAOPass(&renderGraph, _resources, _frameIndex);

    // Visibility Buffer Material pass
    _materialRenderer->AddMaterialPass(&renderGraph, _resources, _frameIndex);

    // Editor Pass
    _terrainRenderer->AddEditorPass(&renderGraph, _resources, _frameIndex);
    _mapObjectRenderer->AddEditorPass(&renderGraph, _resources, _frameIndex);
    _cModelRenderer->AddEditorPass(&renderGraph, _resources, _frameIndex);

    // Postprocessing
    _postProcessRenderer->AddPostProcessPass(&renderGraph, _resources, _frameIndex);
    _rendertargetVisualizer->AddVisualizerPass(&renderGraph, _resources, _frameIndex);

    _pixelQuery->AddPixelQueryPass(&renderGraph, _resources, _frameIndex);

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
        _renderer->Present(_window, _resources.resolvedColor, _sceneRenderedSemaphore);
    }

    // Flip the frameIndex between 0 and 1
    _frameIndex = !_frameIndex;
}

uvec2 ClientRenderer::GetRenderResolution()
{
    return _renderer->GetImageDimension(_resources.visibilityBuffer, 0);
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
    // Visibility Buffer rendertarget
    Renderer::ImageDesc visibilityBufferDesc;
    visibilityBufferDesc.debugName = "VisibilityBuffer";
    visibilityBufferDesc.dimensions = vec2(1.0f, 1.0f);
    visibilityBufferDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE;
    visibilityBufferDesc.format = Renderer::ImageFormat::R32G32B32A32_UINT;
    visibilityBufferDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    visibilityBufferDesc.clearUInts = uvec4(0,0,0,0);

    _resources.visibilityBuffer = _renderer->CreateImage(visibilityBufferDesc);

    // ResolvedColor rendertarget
    Renderer::ImageDesc resolvedColorDesc;
    resolvedColorDesc.debugName = "ResolvedColor";
    resolvedColorDesc.dimensions = vec2(1.0f, 1.0f);
    resolvedColorDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE;
    resolvedColorDesc.format = Renderer::ImageFormat::R16G16B16A16_FLOAT;
    resolvedColorDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    resolvedColorDesc.clearColor = Color::Clear;

    _resources.resolvedColor = _renderer->CreateImage(resolvedColorDesc);

    // Transparency rendertarget
    Renderer::ImageDesc transparencyDesc;
    transparencyDesc.debugName = "Transparency";
    transparencyDesc.dimensions = vec2(1.0f, 1.0f);
    transparencyDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE;
    transparencyDesc.format = Renderer::ImageFormat::R16G16B16A16_FLOAT;
    transparencyDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    transparencyDesc.clearColor = Color::Clear;

    _resources.transparency = _renderer->CreateImage(transparencyDesc);

    // Transparency Weights rendertarget
    Renderer::ImageDesc transparencyWeightsDesc;
    transparencyWeightsDesc.debugName = "TransparencyWeights";
    transparencyWeightsDesc.dimensions = vec2(1.0f, 1.0f);
    transparencyWeightsDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE;
    transparencyWeightsDesc.format = Renderer::ImageFormat::R8_UNORM;
    transparencyWeightsDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
    transparencyWeightsDesc.clearColor = Color::Red;

    _resources.transparencyWeights = _renderer->CreateImage(transparencyWeightsDesc);

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
