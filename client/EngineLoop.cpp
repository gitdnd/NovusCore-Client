#include "EngineLoop.h"

#ifdef WIN32
#include "Winsock.h"
#endif

#include <entt.hpp>
#include <Networking/NetClient.h>
#include <Networking/NetPacketHandler.h>
#include <Memory/MemoryTracker.h>
#include <Utils/CPUInfo.h>

#include <SceneManager.h>
#include <Renderer/Renderer.h>
#include "Rendering/ClientRenderer.h"
#include "Rendering/TerrainRenderer.h"
#include "Rendering/MapObjectRenderer.h"
#include "Rendering/CModelRenderer.h"
#include "Rendering/RendertargetVisualizer.h"
#include "Rendering/CameraFreelook.h"
#include "Rendering/CameraOrbital.h"
#include "Rendering/AnimationSystem/AnimationSystem.h"
#include "Editor/Editor.h"
#include "Window/Window.h"

#include "Scripting/ScriptEngine.h"
#include "Scripting/ScriptLoader.h"
#include "Scripting/ScriptAPI.h"

// Loaders
#include "Loaders/LoaderSystem.h"

// Component Singletons
#include "ECS/Components/Singletons/TimeSingleton.h"
#include "ECS/Components/Singletons/StatsSingleton.h"
#include "ECS/Components/Singletons/ScriptSingleton.h"
#include "ECS/Components/Singletons/ConfigSingleton.h"
#include "ECS/Components/Singletons/LocalplayerSingleton.h"
#include "ECS/Components/Network/ConnectionSingleton.h"

// Components
#include "ECS/Components/Physics/Rigidbody.h"
#include "ECS/Components/Rendering/DebugBox.h"
#include "ECS/Components/Rendering/CModelInfo.h"

#include "UI/ECS/Components/Transform.h"
#include "UI/ECS/Components/NotCulled.h"

// Systems
#include "ECS/Systems/Network/ConnectionSystems.h"
#include "ECS/Systems/Rendering/UpdateModelTransformSystem.h"
#include "ECS/Systems/Rendering/UpdateCModelInfoSystem.h"
#include "ECS/Systems/Physics/SimulateDebugCubeSystem.h"
#include "ECS/Systems/MovementSystem.h"
#include "ECS/Systems/AreaUpdateSystem.h"
#include "ECS/Systems/DayNightSystem.h"

#include "UI/ECS/Systems/DeleteElementsSystem.h"
#include "UI/ECS/Systems/UpdateRenderingSystem.h"
#include "UI/ECS/Systems/UpdateBoundsSystem.h"
#include "UI/ECS/Systems/UpdateCullingSystem.h"
#include "UI/ECS/Systems/BuildSortKeySystem.h"
#include "UI/ECS/Systems/FinalCleanUpSystem.h"

// Utils
#include <Utils/Timer.h>
#include "Utils/ServiceLocator.h"
#include "Utils/MapUtils.h"
#include "Utils/NetworkUtils.h"
#include "Utils/ConfigUtils.h"
#include "UI/Utils/ElementUtils.h"

// Handlers
#include "Network/Handlers/AuthSocket/AuthHandlers.h"
#include "Network/Handlers/GameSocket/GameHandlers.h"

#include <InputManager.h>
#include <GLFW/glfw3.h>
#include <tracy/Tracy.hpp>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/misc/cpp/imgui_stdlib.h"
#include "imgui/implot.h"

#include "CVar/CVarSystem.h"

AutoCVar_Int CVAR_FramerateLock("framerate.lock", "enable locking framerate", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_FramerateTarget("framerate.target", "target framerate", 60);


EngineLoop::EngineLoop() : 
    _isRunning(false), _inputQueue(256), _outputQueue(256)
{
#ifdef WIN32
    WSADATA data;
    i32 code = WSAStartup(MAKEWORD(2, 2), &data);
    if (code != 0)
    {
        DebugHandler::PrintFatal("[Network] Failed to initialize WinSock");
    }
#endif
}

EngineLoop::~EngineLoop()
{
    delete _clientRenderer;
    delete _editor;
}

void EngineLoop::Start()
{
    if (_isRunning)
        return;

    ServiceLocator::SetMainInputQueue(&_inputQueue);

    std::thread threadRun = std::thread(&EngineLoop::Run, this);
    threadRun.detach();
}

void EngineLoop::Stop()
{
    if (!_isRunning)
        return;

    Message message;
    message.code = MSG_IN_EXIT;
    PassMessage(message);
}

void EngineLoop::Abort()
{
    Cleanup();

    Message exitMessage;
    exitMessage.code = MSG_OUT_EXIT_CONFIRM;
    _outputQueue.enqueue(exitMessage);
}

void EngineLoop::PassMessage(Message& message)
{
    _inputQueue.enqueue(message);
}

bool EngineLoop::TryGetMessage(Message& message)
{
    return _outputQueue.try_dequeue(message);
}

bool EngineLoop::Init()
{
    assert(_isInitialized == false);

    CPUInfo cpuInfo = CPUInfo::Get();
    cpuInfo.Print();

    SetupUpdateFramework();

    LoaderSystem* loaderSystem = LoaderSystem::Get();
    loaderSystem->Init();

    bool failedToLoad = false;
    for (Loader* loader : loaderSystem->GetLoaders())
    {
        failedToLoad |= !loader->Init();

        if (failedToLoad)
            break;
    }

    if (failedToLoad)
        return false;

    // Create Cameras (Must happen before ClientRenderer is created)
    CameraFreeLook* cameraFreeLook = new CameraFreeLook();
    CameraOrbital* cameraOrbital = new CameraOrbital();
    ServiceLocator::SetCameraFreeLook(cameraFreeLook);
    ServiceLocator::SetCameraOrbital(cameraOrbital);

    _clientRenderer = new ClientRenderer();
    _editor = new Editor::Editor();

    ServiceLocator::SetEditor(_editor);
    ServiceLocator::SetAnimationSystem(new AnimationSystem());

    InputManager* inputManager = ServiceLocator::GetInputManager();
    KeybindGroup* keybindGroup = inputManager->CreateKeybindGroup("Debug", 0);
    keybindGroup->SetActive(true);

    // Initialize Cameras (Must happen after ClientRenderer is created)
    {
        Window* mainWindow = ServiceLocator::GetWindow();
        cameraFreeLook->SetWindow(mainWindow);
        cameraOrbital->SetWindow(mainWindow);

        cameraFreeLook->Init();
        cameraOrbital->Init();

        // Camera Orbital is default active
        //cameraOrbital->SetActive(true);
        cameraFreeLook->SetActive(true);

        // Bind Switch Camera Key
        keybindGroup->AddKeyboardCallback("Switch Camera Mode", GLFW_KEY_C, KeybindAction::Press, KeybindModifier::Any, [this, cameraFreeLook, cameraOrbital](i32 key, KeybindAction action, KeybindModifier modifier)
        {
            if (cameraFreeLook->IsActive())
            {
                cameraFreeLook->SetActive(false);
                cameraFreeLook->Disabled();
        
                cameraOrbital->SetActive(true);
                cameraOrbital->Enabled();
            }
            else if (cameraOrbital->IsActive())
            {
                cameraOrbital->SetActive(false);
                cameraOrbital->Disabled();
        
                cameraFreeLook->SetActive(true);
                cameraFreeLook->Enabled();
            }
        
            return true;
        });
    }

    // Initialize Networking
    NetworkUtils::InitNetwork(&_updateFramework.gameRegistry);

    ConnectionSingleton& connectionSingleton = _updateFramework.gameRegistry.ctx<ConnectionSingleton>();
    bool didConnect = connectionSingleton.gameConnection->Connect("127.0.0.1", 4500);
    ConnectionUpdateSystem::GameSocket_HandleConnect(connectionSingleton.gameConnection, didConnect);

    // Setup SceneManager (Must happen before ScriptLoader::Init)
    SceneManager* sceneManager = new SceneManager();
    ServiceLocator::SetSceneManager(sceneManager);
    sceneManager->SetAvailableScenes({ "LoginScreen"_h, "CharacterSelection"_h, "CharacterCreation"_h });

    // Initialize Script Engine / Loader
    {
        ScriptEngine* scriptEngine = new ScriptEngine();
        ServiceLocator::SetScriptEngine(scriptEngine);

        ScriptLoader* scriptLoader = new ScriptLoader();
        ServiceLocator::SetScriptLoader(scriptLoader);

        ScriptAPI* scriptAPI = new ScriptAPI();
        ServiceLocator::SetScriptAPI(scriptAPI);

        scriptEngine->Init(scriptLoader->GetCompiler());
        scriptAPI->Init();
        scriptLoader->Init(_updateFramework.gameRegistry);
    }
    
    // Invoke LoadScene (Must happen after ScriptLoader::Init)
    sceneManager->LoadScene("LoginScreen"_h);

    // Initialize DayNightSystem & AreaUpdateSystem
    {
        DayNightSystem::Init(_updateFramework.gameRegistry);
        AreaUpdateSystem::Init(_updateFramework.gameRegistry);
    }

    // Initialize MovementSystem & SimulateDebugCubeSystem (Must happen after ClientRenderer is created)
    {
        MovementSystem::Init(_updateFramework.gameRegistry);
        SimulateDebugCubeSystem::Init(_updateFramework.gameRegistry);
    }

    _isInitialized = true;
    return true;
}

void EngineLoop::Run()
{
    tracy::SetThreadName("GameThread");

    if (!Init())
    {
        Abort();
        return;
    }

    _isRunning = true;

    TimeSingleton& timeSingleton = _updateFramework.gameRegistry.set<TimeSingleton>();
    EngineStatsSingleton& statsSingleton = _updateFramework.gameRegistry.set<EngineStatsSingleton>();

    Timer timer;
    Timer updateTimer;
    Timer renderTimer;
    
    EngineStatsSingleton::Frame timings;
    while (true)
    {
        f32 deltaTime = timer.GetDeltaTime();
        timer.Tick();

        timings.deltaTime = deltaTime;

        timeSingleton.lifeTimeInS = timer.GetLifeTime();
        timeSingleton.lifeTimeInMS = timeSingleton.lifeTimeInS * 1000;
        timeSingleton.deltaTime = deltaTime;

        updateTimer.Reset();
        
        if (!Update(deltaTime))
            break;
        
        DrawEngineStats(&statsSingleton);
        DrawImguiMenuBar();
        RendertargetVisualizer* rendertargetVisualizer = _clientRenderer->GetRendertargetVisualizer();
        rendertargetVisualizer->DrawImgui();

        timings.simulationFrameTime = updateTimer.GetLifeTime();
        
        renderTimer.Reset();
        
        Render();
        
        timings.renderFrameTime = renderTimer.GetLifeTime();
        
        statsSingleton.AddTimings(timings.deltaTime, timings.simulationFrameTime, timings.renderFrameTime);

        bool lockFrameRate = CVAR_FramerateLock.Get() == 1;
        if (lockFrameRate)
        {
            f32 targetFramerate = static_cast<f32>(CVAR_FramerateTarget.Get());
            targetFramerate = Math::Max(targetFramerate, 10.0f);
            f32 targetDelta = 1.0f / targetFramerate;

            // Wait for tick rate, this might be an overkill implementation but it has the most even tickrate I've seen - MPursche
            /*for (deltaTime = timer.GetDeltaTime(); deltaTime < targetDelta - 0.0025f; deltaTime = timer.GetDeltaTime())
            {
                ZoneScopedNC("WaitForTickRate::Sleep", tracy::Color::AntiqueWhite1)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }*/

            for (deltaTime = timer.GetDeltaTime(); deltaTime < targetDelta; deltaTime = timer.GetDeltaTime())
            {
                ZoneScopedNC("WaitForTickRate::Yield", tracy::Color::AntiqueWhite1);
                std::this_thread::yield();
            }
        }

        FrameMark;
    }

    // Clean up stuff here
    Message exitMessage;
    exitMessage.code = MSG_OUT_EXIT_CONFIRM;
    _outputQueue.enqueue(exitMessage);
}
void EngineLoop::Cleanup()
{
    // Cleanup Network
    NetworkUtils::DeInitNetwork(&_updateFramework.gameRegistry);
}

bool EngineLoop::Update(f32 deltaTime)
{
    bool shouldExit = _clientRenderer->UpdateWindow(deltaTime) == false;
    if (shouldExit)
        return false;

    ImguiNewFrame();

    Message message;
    while (_inputQueue.try_dequeue(message))
    {
        if (message.code == -1)
            assert(false);

        if (message.code == MSG_IN_EXIT)
        {
            Cleanup();
            return false;
        }
        else if (message.code == MSG_IN_PRINT)
        {
            _outputQueue.enqueue(message);
        }
        else if (message.code == MSG_IN_PING)
        {
            Message pongMessage;
            pongMessage.code = MSG_OUT_PRINT;
            pongMessage.message = new std::string("PONG!");
            _outputQueue.enqueue(pongMessage);
        }
        else if (message.code == MSG_IN_RELOAD)
        {
            UIUtils::ClearAllElements();

            ServiceLocator::GetScriptLoader()->Reload();

            // Resend "LoadScene" to trigger UI events
            SceneManager* sceneManager = ServiceLocator::GetSceneManager();
            sceneManager->LoadScene(sceneManager->GetScene());
        }
    }

    // Update Systems will modify the Camera, so we wait with updating the Camera 
    // until we are sure it is static for the rest of the frame
    UpdateSystems();

    uvec2 renderResolution = _clientRenderer->GetRenderResolution();

    Camera* camera = ServiceLocator::GetCamera();
    camera->Update(deltaTime, 75.0f, static_cast<f32>(renderResolution.x) / static_cast<f32>(renderResolution.y));

    i32* editorEnabledCVAR = CVarSystem::Get()->GetIntCVar("editor.Enable"_h);
    if (*editorEnabledCVAR)
    {
        _editor->Update(deltaTime);
    }

    _clientRenderer->Update(deltaTime);

    ConfigSingleton& configSingleton = _updateFramework.gameRegistry.ctx<ConfigSingleton>();
    {
        if (CVarSystem::Get()->IsDirty())
        {
            ConfigUtils::SaveConfig(ConfigSaveType::CVAR);
            CVarSystem::Get()->ClearDirty();
        }

        if (configSingleton.uiConfig.IsDirty())
        {
            ConfigUtils::SaveConfig(ConfigSaveType::UI);
            configSingleton.uiConfig.ClearDirty();
        }
    }
    
    return true;
}
void EngineLoop::UpdateSystems()
{
    ZoneScopedNC("UpdateSystems", tracy::Color::DarkBlue)
    {
        ZoneScopedNC("Taskflow::Run", tracy::Color::DarkBlue)
            _updateFramework.taskflow.run(_updateFramework.framework);
    }
    {
        ZoneScopedNC("Taskflow::WaitForAll", tracy::Color::DarkBlue)
            _updateFramework.taskflow.wait_for_all();
    }
}

void EngineLoop::Render()
{
    ZoneScopedNC("EngineLoop::Render", tracy::Color::Red2)

    ImGui::Render();
    _clientRenderer->Render();
}

void EngineLoop::SetupUpdateFramework()
{
    tf::Framework& framework = _updateFramework.framework;
    entt::registry& gameRegistry = _updateFramework.gameRegistry;
    entt::registry& uiRegistry = _updateFramework.uiRegistry;

    ServiceLocator::SetGameRegistry(&gameRegistry);
    ServiceLocator::SetUIRegistry(&uiRegistry);
    SetupMessageHandler();

    // ConnectionUpdateSystem
    tf::Task connectionUpdateSystemTask = framework.emplace([&gameRegistry]()
    {
        ZoneScopedNC("ConnectionUpdateSystem::Update", tracy::Color::Blue2);
        ConnectionUpdateSystem::Update(gameRegistry);
        //gameRegistry.ctx<ScriptSingleton>().CompleteSystem();
    });

    /* UI SYSTEMS */
    // DeleteElementsSystem
    /*tf::Task uiDeleteElementSystem = framework.emplace([&uiRegistry, &gameRegistry]()
    {
        ZoneScopedNC("DeleteElementsSystem::Update", tracy::Color::Gainsboro);
        UISystem::DeleteElementsSystem::Update(uiRegistry);
        //gameRegistry.ctx<ScriptSingleton>().CompleteSystem();
    });

    // UpdateRenderingSystem
    tf::Task uiUpdateRenderingSystem = framework.emplace([&uiRegistry, &gameRegistry]()
    {
        ZoneScopedNC("UpdateRenderingSystem::Update", tracy::Color::Gainsboro);
        UISystem::UpdateRenderingSystem::Update(uiRegistry);
        //gameRegistry.ctx<ScriptSingleton>().CompleteSystem();
    });
    uiUpdateRenderingSystem.gather(uiDeleteElementSystem);

    // UpdateBoundsSystem
    tf::Task uiUpdateBoundsSystemTask = framework.emplace([&uiRegistry, &gameRegistry]()
    {
        ZoneScopedNC("UpdateBoundsSystem::Update", tracy::Color::Gainsboro);
        UISystem::UpdateBoundsSystem::Update(uiRegistry);
        //gameRegistry.ctx<ScriptSingleton>().CompleteSystem();
    });
    uiUpdateRenderingSystem.gather(uiDeleteElementSystem);

    // UpdateCullingSystem
    tf::Task uiUpdateCullingSystemTask = framework.emplace([&uiRegistry, &gameRegistry]()
    {
        ZoneScopedNC("UpdateCullingSystem::Update", tracy::Color::Gainsboro);
        UISystem::UpdateCullingSystem::Update(uiRegistry);
        //gameRegistry.ctx<ScriptSingleton>().CompleteSystem();
    });
    uiUpdateRenderingSystem.gather(uiDeleteElementSystem);
    
    // BuildSortKeySystem
    tf::Task uiBuildSortKeySystemTask = framework.emplace([&uiRegistry, &gameRegistry]()
    {
        ZoneScopedNC("BuildSortKeySystem::Update", tracy::Color::Gainsboro);
        UISystem::BuildSortKeySystem::Update(uiRegistry);
        //gameRegistry.ctx<ScriptSingleton>().CompleteSystem();
    });
    uiUpdateRenderingSystem.gather(uiDeleteElementSystem);

    // FinalCleanUpSystem
    tf::Task uiFinalCleanUpSystemTask = framework.emplace([&uiRegistry, &gameRegistry]()
    {
        ZoneScopedNC("UpdateRenderingSystem::Update", tracy::Color::Gainsboro);
        UISystem::FinalCleanUpSystem::Update(uiRegistry);
        //gameRegistry.ctx<ScriptSingleton>().CompleteSystem();
    });
    uiFinalCleanUpSystemTask.gather(uiUpdateRenderingSystem);
    uiFinalCleanUpSystemTask.gather(uiUpdateCullingSystemTask);
    uiFinalCleanUpSystemTask.gather(uiBuildSortKeySystemTask);*/
    /* END UI SYSTEMS */

    // MovementSystem
    tf::Task movementSystemTask = framework.emplace([&gameRegistry]()
    {
        ZoneScopedNC("MovementSystem::Update", tracy::Color::Blue2);
        MovementSystem::Update(gameRegistry);
        //gameRegistry.ctx<ScriptSingleton>().CompleteSystem();
    });
    movementSystemTask.gather(connectionUpdateSystemTask);

    // DayNightSystem
    tf::Task dayNightSystemTask = framework.emplace([&gameRegistry]()
    {
        ZoneScopedNC("DayNightSystem::Update", tracy::Color::Blue2);
        DayNightSystem::Update(gameRegistry);
        //gameRegistry.ctx<ScriptSingleton>().CompleteSystem();
    });
    dayNightSystemTask.gather(movementSystemTask);

    // AreaUpdateSystem
    tf::Task areaUpdateSystemTask = framework.emplace([&gameRegistry]()
    {
        ZoneScopedNC("AreaUpdateSystem::Update", tracy::Color::Blue2);
        AreaUpdateSystem::Update(gameRegistry);
        //gameRegistry.ctx<ScriptSingleton>().CompleteSystem();
    });
    areaUpdateSystemTask.gather(dayNightSystemTask);

    // SimulateDebugCubeSystem
    tf::Task simulateDebugCubeSystemTask = framework.emplace([this, &gameRegistry]()
    {
        ZoneScopedNC("SimulateDebugCubeSystem::Update", tracy::Color::Blue2);
        SimulateDebugCubeSystem::Update(gameRegistry, _clientRenderer->GetDebugRenderer());
        //gameRegistry.ctx<ScriptSingleton>().CompleteSystem();
    });
    simulateDebugCubeSystemTask.gather(areaUpdateSystemTask);

    // UpdateCModelInfoSystem
    tf::Task updateCModelInfoSystemTask = framework.emplace([this, &gameRegistry]()
    {
        ZoneScopedNC("UpdateCModelInfoSystem::Update", tracy::Color::Blue2);
        UpdateCModelInfoSystem::Update(gameRegistry);
        //gameRegistry.ctx<ScriptSingleton>().CompleteSystem();
    });
    updateCModelInfoSystemTask.gather(simulateDebugCubeSystemTask);

    // UpdateModelTransformSystem
    tf::Task updateModelTransformSystemTask = framework.emplace([this, &gameRegistry]()
    {
        ZoneScopedNC("UpdateModelTransformSystem::Update", tracy::Color::Blue2);
        UpdateModelTransformSystem::Update(gameRegistry);
        //gameRegistry.ctx<ScriptSingleton>().CompleteSystem();
    });
    updateModelTransformSystemTask.gather(updateCModelInfoSystemTask);

    // ScriptSingletonTask
    tf::Task scriptSingletonTask = framework.emplace([&uiRegistry, &gameRegistry]()
    {
        ZoneScopedNC("ScriptSingletonTask::Update", tracy::Color::Blue2);

        ServiceLocator::GetScriptEngine()->Execute();
        //gameRegistry.ctx<ScriptSingleton>().ExecuteTransactions();
        //gameRegistry.ctx<ScriptSingleton>().ResetCompletedSystems();
    });
    //scriptSingletonTask.gather(uiFinalCleanUpSystemTask);
    scriptSingletonTask.gather(updateModelTransformSystemTask);
}
void EngineLoop::SetupMessageHandler()
{
    // Setup Auth Message Handler
    NetPacketHandler* authNetPacketHandler = new NetPacketHandler();
    ServiceLocator::SetAuthNetPacketHandler(authNetPacketHandler);
    AuthSocket::AuthHandlers::Setup(authNetPacketHandler);

    // Setup Game Message Handler
    NetPacketHandler* gameNetPacketHandler = new NetPacketHandler();
    ServiceLocator::SetGameNetPacketHandler(gameNetPacketHandler);
    GameSocket::GameHandlers::Setup(gameNetPacketHandler);
}

void EngineLoop::ImguiNewFrame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}
void EngineLoop::DrawEngineStats(EngineStatsSingleton* stats)
{
    if (ImGui::Begin("Engine Info"))
    {
        EngineStatsSingleton::Frame average = stats->AverageFrame(240);

        ImGui::Text("Frames Per Second : %f ", 1.f / average.deltaTime);
        ImGui::Text("Global Frame Time (ms) : %f", average.deltaTime * 1000);

        if (ImGui::BeginTabBar("Information"))
        {
            if (ImGui::BeginTabItem("Map"))
            {
                ImGui::Spacing();
                DrawMapStats();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Light Info"))
            {
                entt::registry* registry = ServiceLocator::GetGameRegistry();
                MapSingleton& mapSingleton = registry->ctx<MapSingleton>();
                AreaUpdateSingleton& areaUpdateSingleton = registry->ctx<AreaUpdateSingleton>();
                
                size_t numLights = areaUpdateSingleton.totalLightDatas.size();
                ImGui::Spacing();
                ImGui::Text("Lights (Total: %u)", numLights);
                ImGui::Separator();

                AreaUpdateLightColorData lightColorData = mapSingleton.GetLightColorData();
                ImGui::Text("Ambient Color (R: %f, G: %f, B: %f)", lightColorData.ambientColor.r, lightColorData.ambientColor.g, lightColorData.ambientColor.b);
                ImGui::Text("Diffuse Color (R: %f, G: %f, B: %f)", lightColorData.diffuseColor.r, lightColorData.diffuseColor.g, lightColorData.diffuseColor.b);
                ImGui::Text("Skyband Top Color (R: %f, G: %f, B: %f)", lightColorData.skybandTopColor.r, lightColorData.skybandTopColor.g, lightColorData.skybandTopColor.b);
                ImGui::Text("Skyband Middle Color (R: %f, G: %f, B: %f)", lightColorData.skybandMiddleColor.r, lightColorData.skybandMiddleColor.g, lightColorData.skybandMiddleColor.b);
                ImGui::Text("Skyband Bottom Color (R: %f, G: %f, B: %f)", lightColorData.skybandBottomColor.r, lightColorData.skybandBottomColor.g, lightColorData.skybandBottomColor.b);
                ImGui::Text("Skyband Above Horizon Color (R: %f, G: %f, B: %f)", lightColorData.skybandAboveHorizonColor.r, lightColorData.skybandAboveHorizonColor.g, lightColorData.skybandAboveHorizonColor.b);
                ImGui::Text("Skyband Horizon Color (R: %f, G: %f, B: %f)", lightColorData.skybandHorizonColor.r, lightColorData.skybandHorizonColor.g, lightColorData.skybandHorizonColor.b);

                ImGui::Separator();

                ImGui::Text("-- Lights --");
                for (int i = 0; i < numLights; i++)
                {
                    AreaUpdateLightData& lightData = areaUpdateSingleton.totalLightDatas[i];

                    f32 fallOffRange = lightData.fallOff.y - lightData.fallOff.x;
                    f32 impact = (lightData.fallOff.y - lightData.distanceToCenter) / fallOffRange;

                    if (lightData.distanceToCenter < lightData.fallOff.x)
                        impact = 1.0f;

                    ImGui::Text("#%u - (Id: %u, Impact: %f, Ambient Color(R: %f, G: %f, B: %f))", i + 1, lightData.lightId, impact, lightData.colorData.ambientColor.r, lightData.colorData.ambientColor.g, lightData.colorData.ambientColor.b);
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Input Info"))
            {
                ImGui::Spacing();

                InputManager* inputManager = ServiceLocator::GetInputManager();

                const std::vector<KeybindGroup*> keybindGroups = inputManager->GetKeybindGroups();

                u32 numKeybindGroups = static_cast<u32>(keybindGroups.size());
                u32 numActiveKeybindGroups = 0;
                u32 numInactiveKeybindGroups = 0;

                for (u32 i = 0; i < numKeybindGroups; i++)
                {
                    KeybindGroup* keybindGroup = keybindGroups[i];
                    if (!keybindGroup->IsActive())
                        continue;

                    numActiveKeybindGroups++;
                }

                numInactiveKeybindGroups = numKeybindGroups - numActiveKeybindGroups;
                ImGui::Text("Keybind Groups (Total: %u, Active: %u, Inactive: %u)", numKeybindGroups, numActiveKeybindGroups, numInactiveKeybindGroups);
                ImGui::Separator();

                ImGui::Text("Input Consumed Information");

                auto& mouseInputConsumeInfo = inputManager->GetMouseInputConsumeInfo();
                auto& mousePositionConsumeInfo = inputManager->GetMousePositionConsumeInfo();
                auto& mouseScrollConsumeInfo = inputManager->GetMouseScrollConsumeInfo();
                auto& keyboardInputConsumeInfo = inputManager->GetKeyboardInputConsumeInfo();
                auto& unicodeInputConsumeInfo = inputManager->GetUnicodeInputConsumeInfo();

                ImGui::Text("- Mouse Input: (Group: %s, Keybind: %s)", mouseInputConsumeInfo.keybindGroupName->c_str(), mouseInputConsumeInfo.keybindName->c_str());
                ImGui::Text("- Mouse Position: (Group: %s, Keybind: %s)", mousePositionConsumeInfo.keybindGroupName->c_str(), mousePositionConsumeInfo.keybindName->c_str());
                ImGui::Text("- Mouse Scroll: (Group: %s, Keybind: %s)", mouseScrollConsumeInfo.keybindGroupName->c_str(), mouseScrollConsumeInfo.keybindName->c_str());
                ImGui::Text("- Keyboard Input: (Group: %s, Keybind: %s)", keyboardInputConsumeInfo.keybindGroupName->c_str(), keyboardInputConsumeInfo.keybindName->c_str());
                ImGui::Text("- Unicode Input: (Group: %s, Keybind: %s)", unicodeInputConsumeInfo.keybindGroupName->c_str(), unicodeInputConsumeInfo.keybindName->c_str());

                ImGui::Separator();

                if (numActiveKeybindGroups)
                {
                    ImGui::Text("Active Keybind Groups");

                    for (u32 i = 0; i < numKeybindGroups; i++)
                    {
                        KeybindGroup* keybindGroup = keybindGroups[i];
                        if (!keybindGroup->IsActive())
                            continue;

                        const std::vector<KeybindGroup::Keybind*>& keybinds = keybindGroup->GetKeybinds();
                        u32 numKeybinds = static_cast<u32>(keybinds.size());

                        ImGui::Text("- %s (Priority: %u, Keybinds: %u)", keybindGroup->GetDebugName().c_str(), keybindGroup->GetPriority(), numKeybinds);
                    }
                    
                    ImGui::Separator();
                }

                if (numInactiveKeybindGroups)
                {
                    ImGui::Text("Inactive Keybind Groups");

                    for (u32 i = 0; i < numKeybindGroups; i++)
                    {
                        KeybindGroup* keybindGroup = keybindGroups[i];
                        if (keybindGroup->IsActive())
                            continue;

                        const std::vector<KeybindGroup::Keybind*>& keybinds = keybindGroup->GetKeybinds();
                        u32 numKeybinds = static_cast<u32>(keybinds.size());

                        ImGui::Text("- %s (Priority: %u, Keybinds: %u)", keybindGroup->GetDebugName().c_str(), keybindGroup->GetPriority(), numKeybinds);
                    }
                    
                    ImGui::Separator();
                }

                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Position"))
            {
                ImGui::Spacing();
                DrawPositionStats();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("UI"))
            {
                ImGui::Spacing();
                DrawUIStats();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Memory"))
            {
                ImGui::Spacing();
                DrawMemoryStats();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Performance"))
            {
                ImGui::Spacing();
                DrawPerformance(stats);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    ImGui::End();
}
void EngineLoop::DrawMapStats()
{
    entt::registry* registry = ServiceLocator::GetGameRegistry();
    MapSingleton& mapSingleton = registry->ctx<MapSingleton>();
    NDBCSingleton& ndbcSingleton = registry->ctx<NDBCSingleton>();

    static const std::string* selectedMap = nullptr;
    static std::string selectedMapToLower;

    const std::vector<const std::string*>& mapNames = mapSingleton.GetMapNames();

    if (selectedMap == nullptr && mapNames.size() > 0)
        selectedMap = mapNames[0];

    selectedMapToLower.resize(selectedMap->length());
    std::transform(selectedMap->begin(), selectedMap->end(), selectedMapToLower.begin(), [](char c) { return std::tolower((int)c); });

    // Map Selection
    {
        ImGui::Text("Select a map");

        static std::string searchText = "";
        static std::string searchTextToLower = "";
        static std::string mapNameCopy = "";
        ImGui::InputText("Filter", &searchText);

        searchTextToLower.resize(searchText.length());
        std::transform(searchText.begin(), searchText.end(), searchTextToLower.begin(), [](char c) { return std::tolower((int)c); });

        bool hasFilter = searchText.length() != 0;

        static const char* preview = nullptr;
        if (!hasFilter)
            preview = selectedMap->c_str();

        if (ImGui::BeginCombo("##", preview)) // The second parameter is the label previewed before opening the combo.
        {
            for (const std::string* mapName : mapNames)
            {
                mapNameCopy.resize(mapName->length());
                std::transform(mapName->begin(), mapName->end(), mapNameCopy.begin(), [](char c) { return std::tolower((int)c); });

                if (mapNameCopy.find(searchTextToLower) == std::string::npos)
                    continue;

                bool isSelected = selectedMap == mapName;

                if (ImGui::Selectable(mapName->c_str(), &isSelected))
                {
                    selectedMap = mapName;
                    preview = selectedMap->c_str();
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }
        else
        {
            if (hasFilter)
            {
                if (selectedMapToLower.find(searchTextToLower) != std::string::npos)
                {
                    preview = selectedMap->c_str();
                }
                else
                {
                    for (const std::string* mapName : mapNames)
                    {
                        mapNameCopy.resize(mapName->length());
                        std::transform(mapName->begin(), mapName->end(), mapNameCopy.begin(), [](char c) { return std::tolower((int)c); });

                        if (mapNameCopy.find(searchTextToLower) == std::string::npos)
                            continue;

                        preview = mapName->c_str();
                        break;
                    }
                }
            }
        }

        if (ImGui::Button("Load"))
        {
            u32 mapNameHash = StringUtils::fnv1a_32(preview, strlen(preview));
            mapSingleton.SetMapToBeLoaded(mapNameHash);
        }

        ImGui::SameLine();

        if (ImGui::Button("Set Default"))
        {
            ConfigSingleton& configSingleton = registry->ctx<ConfigSingleton>();
            configSingleton.uiConfig.SetDefaultMap(preview);
        }

        ImGui::SameLine();

        if (ImGui::Button("Clear Default"))
        {
            ConfigSingleton& configSingleton = registry->ctx<ConfigSingleton>();
            configSingleton.uiConfig.SetDefaultMap("");
        }

        ImGui::Spacing();
    }

    if (ImGui::BeginTabBar("Map Information"))
    {
        Terrain::Map& currentMap = mapSingleton.GetCurrentMap();
        bool mapIsLoaded = currentMap.IsLoadedMap();

        if (ImGui::BeginTabItem("Basic Info"))
        {
            if (!mapIsLoaded)
            {
                ImGui::Text("No Map Loaded");
            }
            else
            {
                NDBC::File* ndbcFile = ndbcSingleton.GetNDBCFile("Maps"_h);
                const NDBC::Map* map = ndbcFile->GetRowById<NDBC::Map>(currentMap.id);

                const std::string& publicMapName = ndbcFile->GetStringTable()->GetString(map->name);
                const std::string& internalMapName = ndbcFile->GetStringTable()->GetString(map->internalName);

                static std::string instanceType = "............."; // Default to 13 Characters (Max that can be set to force default size to not need reallocation)
                {
                    if (map->instanceType == 0 || map->instanceType >= 5)
                        instanceType = "Open World";
                    else
                    {
                        if (map->instanceType == 1)
                            instanceType = "Dungeon";
                        else if (map->instanceType == 2)
                            instanceType = "Raid";
                        else if (map->instanceType == 3)
                            instanceType = "Battleground";
                        else if (map->instanceType == 4)
                            instanceType = "Arena";
                    }
                }

                ImGui::Text("Map Id:            %u", map->id);
                ImGui::Text("Public Name:       %s", publicMapName.c_str());
                ImGui::Text("Internal name:     %s", internalMapName.c_str());
                ImGui::Text("Instance Type:     %s", instanceType.c_str());
                ImGui::Text("Max Players:       %u", map->maxPlayers);
                ImGui::Text("Expansion:         %u", map->expansion);
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Placement Info"))
        {
            if (!mapIsLoaded)
            {
                ImGui::Text("No Map Loaded");
            }
            else
            {
                if (currentMap.header.flags.UseMapObjectInsteadOfTerrain)
                {
                    ImGui::Text("Loaded World Object:           %s", currentMap.header.mapObjectName.c_str());
                }
                else
                {
                    ImGui::Text("Loaded Chunks:                 %u", currentMap.chunks.size());
                }

                TerrainRenderer* terrainRenderer = _clientRenderer->GetTerrainRenderer();
                MapObjectRenderer* mapObjectRenderer = _clientRenderer->GetMapObjectRenderer();
                CModelRenderer* cModelRenderer = _clientRenderer->GetCModelRenderer();

                ImGui::Text("Loaded Map Objects:            %u", mapObjectRenderer->GetNumLoadedMapObjects());
                ImGui::Text("Loaded Complex Models:         %u", cModelRenderer->GetNumLoadedCModels());

                ImGui::Separator();

                ImGui::Text("Map Object Placements:         %u", mapObjectRenderer->GetNumMapObjectPlacements());
                ImGui::Text("Complex Models Placements:     %u", cModelRenderer->GetNumCModelPlacements());
            }
            
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}
void EngineLoop::DrawPositionStats()
{
    entt::registry* registry = ServiceLocator::GetGameRegistry();
    Camera* camera = ServiceLocator::GetCamera();
    vec3 cameraLocation = camera->GetPosition();
    vec3 cameraRotation = camera->GetRotation();

    ImGui::Text("Camera Location : (%f, %f, %f)", cameraLocation.x, cameraLocation.y, cameraLocation.z);
    ImGui::Text("Camera Rotation : (%f, %f, %f)", cameraRotation.x, cameraRotation.y, cameraRotation.z);

    ImGui::Spacing();
    ImGui::Spacing();

    vec2 adtPos = Terrain::MapUtils::WorldPositionToADTCoordinates(cameraLocation);

    vec2 chunkPos = Terrain::MapUtils::GetChunkFromAdtPosition(adtPos);
    vec2 chunkRemainder = chunkPos - glm::floor(chunkPos);

    vec2 cellLocalPos = (chunkRemainder * Terrain::MAP_CHUNK_SIZE);
    vec2 cellPos = cellLocalPos / Terrain::MAP_CELL_SIZE;
    vec2 cellRemainder = cellPos - glm::floor(cellPos);

    vec2 patchLocalPos = (cellRemainder * Terrain::MAP_CELL_SIZE);
    vec2 patchPos = patchLocalPos / Terrain::MAP_PATCH_SIZE;
    vec2 patchRemainder = patchPos - glm::floor(patchPos);

    u32 currentChunkID = -1;
    u32 numCollidableCModels = 0;

    LocalplayerSingleton& localplayerSingleton = registry->ctx<LocalplayerSingleton>();
    if (localplayerSingleton.entity != entt::null)
    {
        if (CModelInfo* cmodelInfo = registry->try_get<CModelInfo>(localplayerSingleton.entity))
        {
            currentChunkID = cmodelInfo->currentChunkID;

            MapSingleton& mapSingleton = registry->ctx<MapSingleton>();
            Terrain::Map& currentMap = mapSingleton.GetCurrentMap();
            
            if (SafeVector<entt::entity>* collidableEntityList = currentMap.GetCollidableEntityListByChunkID(currentChunkID))
            {
                numCollidableCModels = static_cast<u32>(collidableEntityList->Size());
            }
        }
    }

    ImGui::Text("ChunkID : (%u)", currentChunkID);
    ImGui::Text("Collidable CModels : (%u)", numCollidableCModels);
    ImGui::Text("Chunk : (%f, %f)", chunkPos.x, chunkPos.y);
    ImGui::Text("cellPos : (%f, %f)", cellLocalPos.x, cellLocalPos.y);
    ImGui::Text("patchPos : (%f, %f)", patchLocalPos.x, patchLocalPos.y);

    ImGui::Spacing();
    ImGui::Text("Chunk Remainder : (%f, %f)", chunkRemainder.x, chunkRemainder.y);
    ImGui::Text("Cell  Remainder : (%f, %f)", cellRemainder.x, cellRemainder.y);
    ImGui::Text("Patch Remainder : (%f, %f)", patchRemainder.x, patchRemainder.y);
}
void EngineLoop::DrawUIStats()
{
    entt::registry* registry = ServiceLocator::GetUIRegistry();
    size_t count = registry->size<UIComponent::Transform>();
    size_t notCulled = registry->size<UIComponent::NotCulled>();
    bool* drawCollisionBounds = reinterpret_cast<bool*>(CVarSystem::Get()->GetIntCVar("ui.drawCollisionBounds"));

    ImGui::Text("Total Elements : %d", count);
    ImGui::Text("Culled elements : %d", (count-notCulled));
    
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Checkbox("Show Collision Bounds", drawCollisionBounds);
}
void EngineLoop::DrawMemoryStats()
{
    // RAM
    size_t ramUsage = Memory::MemoryTracker::GetMemoryUsage() / 1000000;
    size_t ramBudget = Memory::MemoryTracker::GetMemoryBudget() / 1000000;
    f32 ramPercent = (static_cast<f32>(ramUsage) / static_cast<f32>(ramBudget)) * 100;

    ImGui::Text("RAM Usage: %luMB / %luMB (%.2f%%)", ramUsage, ramBudget, ramPercent);

    size_t ramMinBudget = 3500;
    f32 ramMinPercent = (static_cast<f32>(ramUsage) / static_cast<f32>(ramMinBudget)) * 100;
    ImGui::Text("RAM Usage (Min specs): %luMB / %luMB (%.2f%%)", ramUsage, ramMinBudget, ramMinPercent);

    size_t ramUsagePeak = Memory::MemoryTracker::GetMemoryUsagePeak() / 1000000;
    f32 ramPeakPercent = (static_cast<f32>(ramUsagePeak) / static_cast<f32>(ramBudget)) * 100;

    ImGui::Text("RAM Usage (Peak): %luMB / %luMB (%.2f%%)", ramUsagePeak, ramBudget, ramPeakPercent);

    f32 ramMinPeakPercent = (static_cast<f32>(ramUsagePeak) / static_cast<f32>(ramMinBudget)) * 100;
    ImGui::Text("RAM Usage (Peak, Min specs): %luMB / %luMB (%.2f%%)", ramUsagePeak, ramMinBudget, ramMinPeakPercent);

    // VRAM
    ImGui::Spacing();

    size_t vramUsage = _clientRenderer->GetVRAMUsage() / 1000000;
    
    size_t vramBudget = _clientRenderer->GetVRAMBudget() / 1000000;
    f32 vramPercent = (static_cast<f32>(vramUsage) / static_cast<f32>(vramBudget)) * 100;

    ImGui::Text("VRAM Usage: %luMB / %luMB (%.2f%%)", vramUsage, vramBudget, vramPercent);

    size_t vramMinBudget = 1500;
    f32 vramMinPercent = (static_cast<f32>(vramUsage) / static_cast<f32>(vramMinBudget)) * 100;

    ImGui::Text("VRAM Usage (Min specs): %luMB / %luMB (%.2f%%)", vramUsage, vramMinBudget, vramMinPercent);
}

void EngineLoop::DrawImguiMenuBar()
{
    if (ImGui::BeginMainMenuBar())
    {
        _editor->DrawImguiMenuBar();

        if (ImGui::BeginMenu("Panels"))
        {
            if (ImGui::Button("Rendertarget Visualizer"))
            {
                RendertargetVisualizer* rendertargetVisualizer = _clientRenderer->GetRendertargetVisualizer();
                rendertargetVisualizer->SetVisible(true);
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Debug"))
        {
            if (ImGui::BeginMenu("CVAR"))
            {
                CVarSystem::Get()->DrawImguiEditor();
                ImGui::EndMenu();
            }

            // Reload shaders button
            if (ImGui::Button("Reload Shaders"))
            {
                _clientRenderer->ReloadShaders(false);
            }
            if (ImGui::Button("Reload Shaders (FORCE)"))
            {
                _clientRenderer->ReloadShaders(true);
            }

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void EngineLoop::DrawPerformance(EngineStatsSingleton* stats)
{
    EngineStatsSingleton::Frame average = stats->AverageFrame(240);

    TerrainRenderer* terrainRenderer = _clientRenderer->GetTerrainRenderer();
    MapObjectRenderer* mapObjectRenderer = _clientRenderer->GetMapObjectRenderer();
    CModelRenderer* cModelRenderer = _clientRenderer->GetCModelRenderer();

    // Draw hardware info
    CPUInfo cpuInfo = CPUInfo::Get();

    const std::string& cpuName = cpuInfo.GetPrettyName();
    const std::string& gpuName = _clientRenderer->GetGPUName();

    ImGui::Text("CPU: %s", cpuName.c_str());
    ImGui::Text("GPU: %s", gpuName.c_str());

    const std::string rightHeaderText = "Survived / Total (%%)";

    f32 textWidth = ImGui::CalcTextSize(rightHeaderText.c_str()).x;
    f32 windowWidth = ImGui::GetWindowContentRegionWidth();

    f32 textPos = windowWidth - textWidth;

    // Surviving Drawcalls
    {
        ImGui::Spacing();
        bool showDrawCalls = ImGui::CollapsingHeader("Surviving Drawcalls");

        // If we are not collapsed, add a header that explains the values
        if (showDrawCalls)
        {
            ImGui::SameLine(textPos);
            ImGui::Text(rightHeaderText.c_str());
            ImGui::Separator();
        }

        u32 totalDrawCalls = 0;
        u32 totalDrawCallsSurvived = 0;

        // Terrain
        {
            u32 drawCalls = terrainRenderer->GetNumDrawCalls();
            totalDrawCalls += drawCalls;

            // Occluders
            {
                u32 drawCallsSurvived = terrainRenderer->GetNumOccluderDrawCalls();

                if (showDrawCalls)
                {
                    DrawCullingStatsEntry("Terrain Occluders", drawCalls, drawCallsSurvived, !showDrawCalls);
                }
                totalDrawCallsSurvived += drawCallsSurvived;
            }

            // Geometry
            {
                u32 drawCallsSurvived = terrainRenderer->GetNumSurvivingDrawCalls();

                if (showDrawCalls)
                {
                    DrawCullingStatsEntry("Terrain Geometry", drawCalls, drawCallsSurvived, !showDrawCalls);
                }
                totalDrawCallsSurvived += drawCallsSurvived;
            };
        }

        // MapObjects
        {
            u32 drawCalls = mapObjectRenderer->GetNumDrawCalls();
            totalDrawCalls += drawCalls;

            // Occluders
            {
                u32 drawCallsSurvived = mapObjectRenderer->GetNumSurvivingOccluderDrawCalls();

                if (showDrawCalls)
                {
                    DrawCullingStatsEntry("MapObjects Occluders", drawCalls, drawCallsSurvived, !showDrawCalls);
                }
                totalDrawCallsSurvived += drawCallsSurvived;
            }
            
            // Geometry
            {
                u32 drawCallsSurvived = mapObjectRenderer->GetNumSurvivingGeometryDrawCalls();

                if (showDrawCalls)
                {
                    DrawCullingStatsEntry("MapObjects Geometry", drawCalls, drawCallsSurvived, !showDrawCalls);
                }
                totalDrawCallsSurvived += drawCallsSurvived;
            }

        }

        // Opaque CModels
        {
            u32 drawCalls = cModelRenderer->GetNumOpaqueDrawCalls();
            totalDrawCalls += drawCalls;

            // Occluders
            {
                u32 drawCallsSurvived = cModelRenderer->GetNumOccluderSurvivingDrawCalls();

                if (showDrawCalls)
                {
                    DrawCullingStatsEntry("CModels (Occluders)", drawCalls, drawCallsSurvived, !showDrawCalls);
                }
                totalDrawCallsSurvived += drawCallsSurvived;
            }

            // Geometry
            {
                u32 drawCallsSurvived = cModelRenderer->GetNumOpaqueSurvivingDrawCalls();

                if (showDrawCalls)
                {
                    DrawCullingStatsEntry("CModels (Opaque)", drawCalls, drawCallsSurvived, !showDrawCalls);
                }
                totalDrawCallsSurvived += drawCallsSurvived;
            }
        }

        // Transparent CModels
        {
            u32 drawCalls = cModelRenderer->GetNumTransparentDrawCalls();
            u32 drawCallsSurvived = cModelRenderer->GetNumTransparentSurvivingDrawCalls();

            if (showDrawCalls)
            {
                DrawCullingStatsEntry("CModels (Transparent)", drawCalls, drawCallsSurvived, !showDrawCalls);
            }

            totalDrawCalls += drawCalls;
            totalDrawCallsSurvived += drawCallsSurvived;
        }

        // Always draw Total, if we are collapsed it will go on the collapsable header
        DrawCullingStatsEntry("Total", totalDrawCalls, totalDrawCallsSurvived, !showDrawCalls);
    }

    // Surviving Triangles
    {
        ImGui::Spacing();
        bool showTriangles = ImGui::CollapsingHeader("Surviving Triangles");

        if (showTriangles)
        {
            ImGui::SameLine(textPos);
            ImGui::Text(rightHeaderText.c_str());
            ImGui::Separator();
        }

        u32 totalTriangles = 0;
        u32 totalTrianglesSurvived = 0;

        // Terrain
        {
            u32 triangles = terrainRenderer->GetNumTriangles();
            totalTriangles += triangles;

            // Occluders
            {
                u32 trianglesSurvived = terrainRenderer->GetNumOccluderTriangles();

                if (showTriangles)
                {
                    DrawCullingStatsEntry("Terrain Occluders", triangles, trianglesSurvived, !showTriangles);
                }
                totalTrianglesSurvived += trianglesSurvived;
            }

            // Geometry
            {
                u32 trianglesSurvived = terrainRenderer->GetNumSurvivingGeometryTriangles();

                if (showTriangles)
                {
                    DrawCullingStatsEntry("Terrain Geometry", triangles, trianglesSurvived, !showTriangles);
                }
                totalTrianglesSurvived += trianglesSurvived;
            }
        }

        // MapObjects
        {
            u32 triangles = mapObjectRenderer->GetNumTriangles();
            totalTriangles += triangles;
            
            // Occluders
            {
                u32 trianglesSurvived = mapObjectRenderer->GetNumSurvivingOccluderTriangles();

                if (showTriangles)
                {
                    DrawCullingStatsEntry("MapObjects Occluders", triangles, trianglesSurvived, !showTriangles);
                }
                totalTrianglesSurvived += trianglesSurvived;
            }

            // Geometry
            {
                u32 trianglesSurvived = mapObjectRenderer->GetNumSurvivingGeometryTriangles();

                if (showTriangles)
                {
                    DrawCullingStatsEntry("MapObjects Geometry", triangles, trianglesSurvived, !showTriangles);
                }
                totalTrianglesSurvived += trianglesSurvived;
            }
        }

        // Opaque CModels
        {
            u32 triangles = cModelRenderer->GetNumOpaqueTriangles();
            totalTriangles += triangles;
            // Occluders
            {
                u32 trianglesSurvived = cModelRenderer->GetNumOccluderSurvivingTriangles();

                if (showTriangles)
                {
                    DrawCullingStatsEntry("CModels (Occluders)", triangles, trianglesSurvived, !showTriangles);
                }
                totalTrianglesSurvived += trianglesSurvived;
            }

            // Geometry
            {
                u32 trianglesSurvived = cModelRenderer->GetNumOpaqueSurvivingTriangles();

                if (showTriangles)
                {
                    DrawCullingStatsEntry("CModels (Opaque)", triangles, trianglesSurvived, !showTriangles);
                }
                totalTrianglesSurvived += trianglesSurvived;
            }
        }

        // Transparent CModels
        {
            u32 triangles = cModelRenderer->GetNumTransparentTriangles();
            u32 trianglesSurvived = cModelRenderer->GetNumTransparentSurvivingTriangles();

            if (showTriangles)
            {
                DrawCullingStatsEntry("CModels (Transparent)", triangles, trianglesSurvived, !showTriangles);
            }

            totalTriangles += triangles;
            totalTrianglesSurvived += trianglesSurvived;
        }

        DrawCullingStatsEntry("Total", totalTriangles, totalTrianglesSurvived, !showTriangles);
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Text("Frametimes");
    ImGui::Separator();

    // Draw Timing Graph
    {
        ImGui::Text("Update Time (ms) : %f", average.simulationFrameTime * 1000);
        ImGui::Text("Render Time CPU (ms): %f", average.renderFrameTime * 1000);

        //read the frame buffer to gather timings for the histograms
        std::vector<float> updateTimes;
        updateTimes.reserve(stats->frameStats.size());

        std::vector<float> renderTimes;
        renderTimes.reserve(stats->frameStats.size());

        for (int i = 0; i < stats->frameStats.size(); i++)
        {
            updateTimes.push_back(stats->frameStats[i].simulationFrameTime * 1000);
            renderTimes.push_back(stats->frameStats[i].renderFrameTime * 1000);
        }

        ImPlot::SetNextPlotLimits(0.0, 120.0, 0, 33.0);

        //lock minimum Y to 0 (cant have negative ms)
        //lock X completely as its fixed 120 frames
        if (ImPlot::BeginPlot("Timing", "frame", "ms", ImVec2(400, 300), 0, ImPlotAxisFlags_Lock, ImPlotAxisFlags_LockMin))
        {
            ImPlot::PlotLine("Update Time", updateTimes.data(), (int)updateTimes.size());
            ImPlot::PlotLine("Render Time", renderTimes.data(), (int)renderTimes.size());
            ImPlot::EndPlot();
        }
    }
}

void EngineLoop::DrawCullingStatsEntry(std::string_view name, u32 drawCalls, u32 survivedDrawCalls, bool isCollapsed)
{
    f32 percent = static_cast<f32>(survivedDrawCalls) / static_cast<f32>(drawCalls) * 100.0f;

    char str[50];
    i32 strLength = StringUtils::FormatString(str, sizeof(str), "%s / %s (%.0f%%)", StringUtils::FormatThousandSeparator(survivedDrawCalls).c_str(), StringUtils::FormatThousandSeparator(drawCalls).c_str(), percent);

    f32 textWidth = ImGui::CalcTextSize(str).x;
    f32 windowWidth = ImGui::GetWindowContentRegionWidth();

    f32 textPos = windowWidth - textWidth;

    if (isCollapsed)
    {
        ImGui::SameLine(textPos);
        ImGui::Text("%s", str);
    }
    else
    {
        ImGui::Separator();
        ImGui::Text("%.*s:", name.length(), name.data());
        ImGui::SameLine(textPos);
        ImGui::Text("%s", str);
    }
}
