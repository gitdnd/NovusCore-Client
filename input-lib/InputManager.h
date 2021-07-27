#pragma once
#include <NovusTypes.h>
#include <robin_hood.h>
#include <GLFW/glfw3.h>
#include <memory>
#include "KeybindGroup.h"

class Window;
class InputManager
{
public:
    InputManager();

    KeybindGroup* CreateKeybindGroup(const std::string& debugName, u32 priority);
    KeybindGroup* GetKeybindGroupByHash(const u32 hash);

    void KeyboardInputHandler(i32 key, i32 scancode, i32 actionMask, i32 modifierMask);
    void CharInputHandler(u32 unicodeKey);
    void MouseInputHandler(i32 button, i32 actionMask, i32 modifierMask);
    void MousePositionHandler(f32 x, f32 y);
    void MouseScrollHandler(f32 x, f32 y);

    bool IsKeyPressed(i32 glfwKey);
    bool IsMousePressed(i32 glfwKey);

    vec2 GetMousePosition() { return vec2(_mousePositionX, _mousePositionY); }
    f32 GetMousePositionX() { return _mousePositionX; }
    f32 GetMousePositionY() { return _mousePositionY; }

private:
    std::vector<KeybindGroup*> _keybindGroups;
    f32 _mousePositionX = 0;
    f32 _mousePositionY = 0;
    bool _mouseState = false;
};