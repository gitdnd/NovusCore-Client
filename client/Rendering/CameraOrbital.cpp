#include "CameraOrbital.h"
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
#include "imgui/imgui.h"

namespace fs = std::filesystem;

void CameraOrbital::Init()
{
    InputManager* inputManager = ServiceLocator::GetInputManager();
    KeybindGroup* keybindGroup = inputManager->CreateKeybindGroup("CameraOrbital", 10);

    keybindGroup->AddMouseScrollCallback([this](f32 xPos, f32 yPos)
    {
        if (!IsActive())
            return false;
    
        _distance = glm::clamp(_distance - yPos, 5.f, 30.f);
        return true;
    });
    keybindGroup->AddMousePositionCallback([this, inputManager](f32 xPos, f32 yPos)
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
    
                _pitch = Math::Clamp(_pitch + (deltaPosition.y * _mouseSensitivity), -89.0f, 89.0f);
    
                /* TODO: Add proper collision for the camera so we don't go through the ground
                         the below code will do a quick test for the pitch but not the yaw.
                         We also need to use "distToCollision" to possibly add an offset.
    
                f32 tmpPitch = Math::Clamp(_pitch + (deltaPosition.y * _mouseSensitivity), -89.0f, 89.0f);
                f32 dist = tmpPitch - _pitch;
    
                mat4x4 rotationMatrix = glm::yawPitchRoll(glm::radians(_yaw), glm::radians(_pitch), 0.0f);
                vec3 t = vec3(_rotationMatrix * vec4(vec3(0, 0, _distance), 0.0f));
                vec3 position = _position + t;
    
                Geometry::AABoundingBox box;
                box.min = position - vec3(0.5f, 2.5f, 0.5f);
                box.max = position + vec3(0.5f, 2.5f, 0.5f);
    
                Geometry::Triangle triangle;
                f32 height = 0;
                f32 distToCollision = 0;
    
                if (!Terrain::MapUtils::Intersect_AABB_TERRAIN_SWEEP(box, triangle, height, dist, distToCollision))
                {
                    _pitch = tmpPitch;
                }*/
            }
            else
                _captureMouseHasMoved = true;
    
            _prevMousePosition = mousePosition;
        }

        return _captureMouse;
    });
    keybindGroup->AddKeyboardCallback("Left Mouse", GLFW_MOUSE_BUTTON_LEFT, KeybindAction::Click, KeybindModifier::None, [this, inputManager, keybindGroup](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        if (!IsActive())
            return false;
    
        if (keybindGroup->IsKeybindPressed("Right Mouse"_h))
            return false;
    
        if (action == KeybindAction::Press)
        {
            if (!_captureMouse)
            {
                _captureMouse = true;
                _prevMousePosition = vec2(inputManager->GetMousePositionX(), inputManager->GetMousePositionY());

                ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
                glfwSetInputMode(ServiceLocator::GetWindow()->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }
        }
        else
        {
            if (_captureMouse)
            {
                _captureMouse = false;
                _captureMouseHasMoved = false;

                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
                glfwSetInputMode(ServiceLocator::GetWindow()->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
        }
    
        return true;
    });
    keybindGroup->AddKeyboardCallback("Right Mouse", GLFW_MOUSE_BUTTON_RIGHT, KeybindAction::Click, KeybindModifier::None, [this, inputManager, keybindGroup](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        if (!IsActive())
            return false;

        if (keybindGroup->IsKeybindPressed("Left Mouse"_h))
            return false;
    
        if (action == KeybindAction::Press)
        {
            if (!_captureMouse)
            {
                _captureMouse = true;
                _prevMousePosition = vec2(inputManager->GetMousePositionX(), inputManager->GetMousePositionY());

                ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
                glfwSetInputMode(ServiceLocator::GetWindow()->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }
        }
        else
        {
            if (_captureMouse)
            {
                _captureMouse = false;
                _captureMouseHasMoved = false;

                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
                glfwSetInputMode(ServiceLocator::GetWindow()->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
        }
    
        return true;
    });
}

void CameraOrbital::Enabled()
{
    InputManager* inputManager = ServiceLocator::GetInputManager();
    KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("CameraOrbital"_h);
    keybindGroup->SetActive(true);
}

void CameraOrbital::Disabled()
{
    _captureMouse = false;
    _captureMouseHasMoved = false;
    glfwSetInputMode(_window->GetWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    InputManager* inputManager = ServiceLocator::GetInputManager();
    KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("CameraOrbital"_h);
    keybindGroup->SetActive(false);
}

void CameraOrbital::Update(f32 deltaTime, f32 fovInDegrees, f32 aspectRatioWH)
{
    _fovInDegrees = fovInDegrees;
    _aspectRatio = aspectRatioWH;

    // Compute matrices
    mat4x4 offsetPitchMatrix = glm::yawPitchRoll(0.0f, glm::radians(90.0f), 0.0f);
    mat4x4 offsetYawMatrix = glm::yawPitchRoll(glm::radians(-90.0f), 0.0f, 0.0f);

    _rotationMatrix = offsetPitchMatrix * offsetYawMatrix * glm::yawPitchRoll(glm::radians(_yaw), glm::radians(_pitch), 0.0f);

    vec3 t = vec3(_rotationMatrix * vec4(vec3(0, 0, _distance), 0.0f));
    vec3 position = _position + t;

    _viewMatrix = glm::lookAt(position, _position, worldUp);
    _projectionMatrix = glm::perspective(glm::radians(fovInDegrees), aspectRatioWH, GetFarClip(), GetNearClip());
    _viewProjectionMatrix = _projectionMatrix * _viewMatrix;

    UpdateCameraVectors();
    UpdateFrustumPlanes(glm::transpose(_viewProjectionMatrix));
}
