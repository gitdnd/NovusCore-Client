#pragma once
#include <Utils/ConcurrentQueue.h>
#include <Utils/Message.h>
#include <entity/registry.hpp>
#include <cassert>

class NetPacketHandler;
class Window;
class InputManager;
class ClientRenderer;
class Camera;
class CameraFreeLook;
class CameraOrbital;
class SceneManager;
class ScriptLoader;
class ScriptEngine;
class ScriptAPI;
class AnimationSystem;

namespace Editor
{
    class Editor;
}
namespace Renderer
{
    class Renderer;
}
class ServiceLocator
{
public:
    static entt::registry* GetGameRegistry() 
    {
        assert(_gameRegistry != nullptr);
        return _gameRegistry; 
    }
    static void SetGameRegistry(entt::registry* registry);
    static entt::registry* GetUIRegistry() 
    {
        assert(_uiRegistry != nullptr);
        return _uiRegistry; 
    }
    static void SetUIRegistry(entt::registry* registry);
    static NetPacketHandler* GetAuthNetPacketHandler()
    {
        assert(_authNetPacketHandler != nullptr);
        return _authNetPacketHandler;
    }
    static void SetAuthNetPacketHandler(NetPacketHandler* netPacketHandler);
    static NetPacketHandler* GetGameNetPacketHandler()
    {
        assert(_gameNetPacketHandler != nullptr);
        return _gameNetPacketHandler; 
    }
    static void SetGameNetPacketHandler(NetPacketHandler* netPacketHandler);
    static Window* GetWindow() 
    {
        assert(_window != nullptr);
        return _window; 
    }
    static void SetWindow(Window* window);
    static InputManager* GetInputManager() 
    {
        assert(_inputManager != nullptr);
        return _inputManager; 
    }
    static void SetInputManager(InputManager* inputManager);
    static ClientRenderer* GetClientRenderer()
    {
        assert(_clientRenderer != nullptr);
        return _clientRenderer;
    }
    static void SetClientRenderer(ClientRenderer* clientRenderer);
    static Camera* GetCamera();
    static CameraFreeLook* GetCameraFreeLook()
    {
        assert(_cameraFreeLook != nullptr);
        return _cameraFreeLook;
    }
    static void SetCameraFreeLook(CameraFreeLook* camera);
    static CameraOrbital* GetCameraOrbital()
    {
        assert(_cameraOrbital != nullptr);
        return _cameraOrbital;
    }
    static void SetCameraOrbital(CameraOrbital* camera);
    static moodycamel::ConcurrentQueue<Message>* GetMainInputQueue() 
    {
        assert(_mainInputQueue != nullptr);
        return _mainInputQueue; 
    }
    static void SetMainInputQueue(moodycamel::ConcurrentQueue<Message>* mainInputQueue);
    static Renderer::Renderer* GetRenderer() 
    {
        assert(_renderer != nullptr);
        return _renderer;
    }
    static void SetRenderer(Renderer::Renderer* renderer);
    static SceneManager* GetSceneManager()
    {
        assert(_sceneManager != nullptr);
        return _sceneManager;
    }
    static void SetSceneManager(SceneManager* sceneManager);
    static Editor::Editor* GetEditor()
    {
        assert(_editor != nullptr);
        return _editor;
    }
    static void SetEditor(Editor::Editor* editor);
    static ScriptEngine* GetScriptEngine()
    {
        assert(_scriptEngine != nullptr);
        return _scriptEngine;
    }
    static void SetScriptEngine(ScriptEngine* scriptEngine);
    static ScriptLoader* GetScriptLoader()
    {
        assert(_scriptLoader != nullptr);
        return _scriptLoader;
    }
    static void SetScriptLoader(ScriptLoader* scriptLoader);
    static ScriptAPI* GetScriptAPI()
    {
        assert(_scriptAPI != nullptr);
        return _scriptAPI;
    }
    static void SetScriptAPI(ScriptAPI* scriptAPI);
    static AnimationSystem* GetAnimationSystem()
    {
        assert(_animationSystem != nullptr);
        return _animationSystem;
    }
    static void SetAnimationSystem(AnimationSystem* animationSystem);

private:
    ServiceLocator() { }
    static entt::registry* _gameRegistry;
    static entt::registry* _uiRegistry;
    static NetPacketHandler* _authNetPacketHandler;
    static NetPacketHandler* _gameNetPacketHandler;
    static Window* _window;
    static InputManager* _inputManager;
    static ClientRenderer* _clientRenderer;
    static CameraFreeLook* _cameraFreeLook;
    static CameraOrbital* _cameraOrbital;
    static moodycamel::ConcurrentQueue<Message>* _mainInputQueue;
    static Renderer::Renderer* _renderer;
    static SceneManager* _sceneManager;
    static Editor::Editor* _editor;
    static ScriptEngine* _scriptEngine;
    static ScriptLoader* _scriptLoader;
    static ScriptAPI* _scriptAPI;
    static AnimationSystem* _animationSystem;
};