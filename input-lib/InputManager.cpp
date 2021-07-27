#include "InputManager.h"
#include <Utils/StringUtils.h>
#include <GLFW/glfw3.h>
#include "imgui/imgui.h"

InputManager::InputManager()
{
}

KeybindGroup* InputManager::CreateKeybindGroup(const std::string& debugName, u32 priority)
{
    KeybindGroup* keybindGroup = new KeybindGroup(debugName, priority);
    _keybindGroups.push_back(keybindGroup);

    // Ensure we sort the vector as such that KeybindGroups with a higher priority comes first
    std::sort(_keybindGroups.begin(), _keybindGroups.end(), [&](KeybindGroup* a, KeybindGroup* b) { return a->GetPriority() > b->GetPriority(); });

    return keybindGroup;
}

KeybindGroup* InputManager::GetKeybindGroupByHash(const u32 hash)
{
    u32 numKeybinds = static_cast<u32>(_keybindGroups.size());
    for (u32 i = 0; i < numKeybinds; i++)
    {
        KeybindGroup* keybindGroup = _keybindGroups[i];

        if (keybindGroup->_debugNameHash == hash)
            return keybindGroup;
    }

    return nullptr;
}

void InputManager::KeyboardInputHandler(i32 key, i32 /*scanCode*/, i32 actionMask, i32 modifierMask)
{
    auto& io = ImGui::GetIO();
    bool wasAbsorbed = io.WantCaptureKeyboard;
    if (wasAbsorbed && actionMask == GLFW_PRESS)
        return;

    u32 numKeybinds = static_cast<u32>(_keybindGroups.size());

    for (u32 i = 0; i < numKeybinds; i++)
    {
        KeybindGroup* keybindGroup = _keybindGroups[i];
        if (!keybindGroup->IsActive())
            continue;

        wasAbsorbed |= keybindGroup->KeyboardInputCallback(key, actionMask, modifierMask, wasAbsorbed);
    }
}
void InputManager::CharInputHandler(u32 unicode)
{
    auto& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard)
        return;

    u32 numKeybinds = static_cast<u32>(_keybindGroups.size());
    for (u32 i = 0; i < numKeybinds; i++)
    {
        KeybindGroup* keybindGroup = _keybindGroups[i];

        if (keybindGroup->CharInputCallback(unicode))
            return;
    }
}
void InputManager::MouseInputHandler(i32 button, i32 actionMask, i32 modifierMask)
{
    auto& io = ImGui::GetIO();
    bool wasAbsorbed = io.WantCaptureMouse;
    if (wasAbsorbed && actionMask == GLFW_PRESS)
        return;

    _mouseState = actionMask == GLFW_RELEASE ? 0 : 1;

    u32 numKeybinds = static_cast<u32>(_keybindGroups.size());

    for (u32 i = 0; i < numKeybinds; i++)
    {
        KeybindGroup* keybindGroup = _keybindGroups[i];
        if (!keybindGroup->IsActive())
            continue;

        wasAbsorbed |= keybindGroup->MouseInputHandler(button, actionMask, modifierMask, wasAbsorbed);
    }
}
void InputManager::MousePositionHandler(f32 x, f32 y)
{
    _mousePositionX = x;
    _mousePositionY = y;

    auto& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;

    u32 numKeybinds = static_cast<u32>(_keybindGroups.size());
    bool wasAbsorbed = false;

    for (u32 i = 0; i < numKeybinds; i++)
    {
        KeybindGroup* keybindGroup = _keybindGroups[i];
        if (!keybindGroup->IsActive())
            continue;

        if (keybindGroup->_mousePositionUpdateCallback)
        {
            // If the callback returns true, we consume the input
            wasAbsorbed |= keybindGroup->MousePositionUpdate(x, y, wasAbsorbed);
        }
    }
}
void InputManager::MouseScrollHandler(f32 x, f32 y)
{
    auto& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;

    u32 numKeybinds = static_cast<u32>(_keybindGroups.size());
    bool wasAbsorbed = false;

    for (u32 i = 0; i < numKeybinds; i++)
    {
        KeybindGroup* keybindGroup = _keybindGroups[i];
        if (!keybindGroup->IsActive())
            continue;

        if (keybindGroup->_mouseScrollUpdateCallback)
        {
            // If the callback returns true, we consume the input
            wasAbsorbed |= keybindGroup->MouseScrollUpdate(x, y, wasAbsorbed);
        }
    }
}