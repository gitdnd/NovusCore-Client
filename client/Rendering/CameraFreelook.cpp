#include "CameraFreelook.h"
#include <windows.h>
#include <InputManager.h>
#include <filesystem>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <GLFW/glfw3.h>
#include <Window/Window.h>
#include <Utils/DebugHandler.h>
#include <Utils/FileReader.h>
#include "../Utils/ServiceLocator.h"
#include "../Utils/MapUtils.h"
#include <CVar/CVarSystem.h>
#include <imgui/imgui.h>

namespace fs = std::filesystem;

AutoCVar_Float CVAR_CameraSpeed("camera.speed", "Camera Freelook Speed", 7.1111f);

void CameraFreeLook::Init()
{
    InputManager* inputManager = ServiceLocator::GetInputManager();
    KeybindGroup* keybindGroup = inputManager->CreateKeybindGroup("CameraFreeLook", 10);

    keybindGroup->AddKeyboardCallback("Alt", GLFW_KEY_LEFT_ALT, KeybindAction::Press, KeybindModifier::Any, nullptr);
    keybindGroup->AddKeyboardCallback("Forward", GLFW_KEY_W, KeybindAction::Press, KeybindModifier::Any, nullptr);
    keybindGroup->AddKeyboardCallback("Backward", GLFW_KEY_S, KeybindAction::Press, KeybindModifier::Any, nullptr);
    keybindGroup->AddKeyboardCallback("Left", GLFW_KEY_A, KeybindAction::Press, KeybindModifier::Any, nullptr);
    keybindGroup->AddKeyboardCallback("Right", GLFW_KEY_D, KeybindAction::Press, KeybindModifier::Any, nullptr);
    keybindGroup->AddKeyboardCallback("Upwards", GLFW_KEY_SPACE, KeybindAction::Press, KeybindModifier::Any, nullptr);
    keybindGroup->AddKeyboardCallback("Downwards", GLFW_KEY_LEFT_CONTROL, KeybindAction::Press, KeybindModifier::Any, nullptr);
    keybindGroup->AddKeyboardCallback("ToggleMouseCapture", GLFW_KEY_ESCAPE, KeybindAction::Press, KeybindModifier::Any, [this](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        if (!IsActive())
            return false;
    
        _captureMouse = !_captureMouse;
    
        GLFWwindow* glfwWindow = _window->GetWindow();
        if (_captureMouse)
        {
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
            glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            DebugHandler::Print("Mouse captured because of tab!");
        }
        else
        {
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            DebugHandler::Print("Mouse released because of tab!");
        }
    
        return true;
    });
    keybindGroup->AddKeyboardCallback("Right Mouseclick", GLFW_MOUSE_BUTTON_RIGHT, KeybindAction::Click, KeybindModifier::Any, [this](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        if (!IsActive())
            return false;
    
        if (!_captureMouse)
        {
            _captureMouse = true;

            InputManager* inputManager = ServiceLocator::GetInputManager();
            _prevMousePosition = vec2(inputManager->GetMousePositionX(), inputManager->GetMousePositionY());
    
            glfwSetInputMode(ServiceLocator::GetWindow()->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
            DebugHandler::Print("Mouse captured because of mouseclick!");
        }
        return true;
    });
    keybindGroup->AddMousePositionCallback([this](f32 xPos, f32 yPos)
    {
        if (!IsActive())
            return false;
    
        if (_captureMouse)
        {
            vec2 mousePosition = vec2(xPos, yPos);
            if (_captureMouseHasMoved)
            {
                vec2 deltaPosition = _prevMousePosition - mousePosition;
    
                _yaw += deltaPosition.x * _mouseSensitivity;
    
                if (_yaw > 360)
                    _yaw -= 360;
                else if (_yaw < 0)
                    _yaw += 360;
    
                _pitch = Math::Clamp(_pitch - (deltaPosition.y * _mouseSensitivity), -89.0f, 89.0f);
            }
            else
                _captureMouseHasMoved = true;
    
            _prevMousePosition = mousePosition;
        }

        return _captureMouse;
    });
    keybindGroup->AddMouseScrollCallback([this](f32 x, f32 y) 
    {
        if (!IsActive())
            return false;

        InputManager* inputManager = ServiceLocator::GetInputManager();
        KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("CameraFreeLook"_h);
        if (keybindGroup->IsKeybindPressed("Alt"_h))
        {
            f32 currentSpeed = CVAR_CameraSpeed.GetFloat();
            f32 newSpeed = currentSpeed + ((currentSpeed / 10) * y);
            newSpeed = glm::max(newSpeed, 7.1111f);
            CVAR_CameraSpeed.Set(newSpeed);

            return true;
        }

        return false;
    });
    
    keybindGroup->AddKeyboardCallback("IncreaseCameraSpeed", GLFW_KEY_PAGE_UP, KeybindAction::Press, KeybindModifier::None, [this](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        if (!IsActive())
            return false;
    
        f32 newSpeed = CVAR_CameraSpeed.GetFloat() + 7.1111f;
        CVAR_CameraSpeed.Set(newSpeed);
        return true;
    });
    keybindGroup->AddKeyboardCallback("DecreaseCameraSpeed", GLFW_KEY_PAGE_DOWN, KeybindAction::Press, KeybindModifier::None, [this](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        if (!IsActive())
            return false;
    
        f32 newSpeed = CVAR_CameraSpeed.GetFloat() - 7.1111f;
        newSpeed = glm::max(newSpeed, 7.1111f);
        CVAR_CameraSpeed.Set(newSpeed);
        return true;
    });
    
    keybindGroup->AddKeyboardCallback("SaveCameraDefault", GLFW_KEY_F9, KeybindAction::Press, KeybindModifier::None, [this](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        if (!IsActive())
            return false;
    
        SaveToFile("freelook.cameradata");
        return true;
    });  
    keybindGroup->AddKeyboardCallback("LoadCameraDefault", GLFW_KEY_F10, KeybindAction::Press, KeybindModifier::None, [this](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        if (!IsActive())
            return false;
    
        LoadFromFile("freelook.cameradata");
        return true;
    });
}

void CameraFreeLook::Enabled()
{
    _captureMouseHasMoved = false;
    glfwSetInputMode(_window->GetWindow(), GLFW_CURSOR, _captureMouse ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

    InputManager* inputManager = ServiceLocator::GetInputManager();
    KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("CameraFreeLook"_h);
    keybindGroup->SetActive(true);    
    
    if (_captureMouse)
    {
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
        glfwSetInputMode(ServiceLocator::GetWindow()->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
    else
    {
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        glfwSetInputMode(ServiceLocator::GetWindow()->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

void CameraFreeLook::Disabled()
{
    InputManager* inputManager = ServiceLocator::GetInputManager();
    KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("CameraFreeLook"_h);
    keybindGroup->SetActive(false);    
    
    if (_captureMouse)
    {
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        glfwSetInputMode(ServiceLocator::GetWindow()->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

void CameraFreeLook::Update(f32 deltaTime, f32 fovInDegrees, f32 aspectRatioWH)
{
    if (!IsActive())
        return;

    _fovInDegrees = fovInDegrees;
    _aspectRatio = aspectRatioWH;

    InputManager* inputManager = ServiceLocator::GetInputManager();
    KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("CameraFreeLook"_h);
    f32 speed = CVAR_CameraSpeed.GetFloat();

    // Movement
    if (keybindGroup->IsKeybindPressed("Forward"_h))
    {
        _position += _front * speed * deltaTime;
    }
    if (keybindGroup->IsKeybindPressed("Backward"_h))
    {
        _position -= _front * speed * deltaTime;
    }
    if (keybindGroup->IsKeybindPressed("Left"_h))
    {
        _position += _left * speed * deltaTime;
    }
    if (keybindGroup->IsKeybindPressed("Right"_h))
    {
        _position -= _left * speed * deltaTime;
    }
    if (keybindGroup->IsKeybindPressed("Upwards"_h))
    {
        _position += worldUp * speed * deltaTime;
    }
    if (keybindGroup->IsKeybindPressed("Downwards"_h))
    {
        _position -= worldUp * speed * deltaTime;
    }

    // Compute matrices
    glm::quat rotQuat = glm::quat(glm::vec3(0.0f, glm::radians(_pitch), glm::radians(_yaw)));
    _rotationMatrix = glm::mat4_cast(rotQuat);

    const mat4x4 cameraMatrix = glm::translate(mat4x4(1.0f), _position) * _rotationMatrix;
    _viewMatrix = glm::inverse(cameraMatrix);

    _projectionMatrix = glm::perspective(glm::radians(fovInDegrees), aspectRatioWH, GetFarClip(), GetNearClip());
    _viewProjectionMatrix = _projectionMatrix * _viewMatrix;

    UpdateCameraVectors();
    UpdateFrustumPlanes();
}