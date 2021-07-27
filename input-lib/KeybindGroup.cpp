#include "KeybindGroup.h"
#include "GLFW/glfw3.h"
#include <Utils/StringUtils.h>

KeybindGroup::KeybindGroup(const std::string& debugName, u32 priority) : _debugName(debugName), _priority(priority)
{
    _debugNameHash = StringUtils::fnv1a_32(debugName.c_str(), debugName.length());
}

const std::string& KeybindGroup::GetDebugName()
{
    return _debugName;
}

const u32& KeybindGroup::GetPriority()
{
    return _priority;
}

bool KeybindGroup::IsActive()
{
    return _isActive;
}

void KeybindGroup::SetActive(bool state)
{
    if (state == _isActive)
        return;

    _isActive = state;
    if (_isActive == false)
    {
        u32 numKeybinds = static_cast<u32>(_keybinds.size());
        for (u32 i = 0; i < numKeybinds; i++)
        {
            Keybind& keybind = _keybinds[i];
            if (keybind.isPressed && keybind.callback)
                keybind.callback(keybind.glfwKey, KeybindAction::Release, KeybindModifier::None);
        }
    }
}

bool KeybindGroup::AddKeyboardCallback(const std::string& keybindName, i32 glfwKey, KeybindAction actionMask, KeybindModifier modifierMask, std::function<KeyboardInputCallbackFunc> callback)
{
    u32 numKeybinds = static_cast<u32>(_keybinds.size());
    for (u32 i = 0; i < numKeybinds; i++)
    {
        Keybind& keybind = _keybinds[i];

        if (keybind.glfwKey         == glfwKey &&
            keybind.actionMask      == actionMask &&
            keybind.modifierMask    == modifierMask)
            return false;
    }

    Keybind& keybind = _keybinds.emplace_back();
    {
        keybind.keybindName = keybindName;
        keybind.keybindNameHash = StringUtils::fnv1a_32(keybindName.c_str(), keybindName.length());
        keybind.glfwKey = glfwKey;
        keybind.actionMask = actionMask;
        keybind.modifierMask = modifierMask;
        keybind.isPressed = false;
        keybind.callback = callback;
    }

    return true;
}

void KeybindGroup::AddAnyKeyboardCallback(const std::string& keybindName, std::function<KeyboardInputCallbackFunc> callback)
{
    if (_anyKeyboardInputKeybind)
        delete _anyKeyboardInputKeybind;

    _anyKeyboardInputKeybind = new Keybind();
    {
        _anyKeyboardInputKeybind->keybindName = keybindName;
        _anyKeyboardInputKeybind->keybindNameHash = StringUtils::fnv1a_32(keybindName.c_str(), keybindName.length());
        _anyKeyboardInputKeybind->glfwKey = 0;
        _anyKeyboardInputKeybind->actionMask = KeybindAction::Press;
        _anyKeyboardInputKeybind->modifierMask = KeybindModifier::None;
        _anyKeyboardInputKeybind->isPressed = false;
        _anyKeyboardInputKeybind->callback = callback;
    }
}

bool KeybindGroup::AddUnicodeCallback(u32 unicode, std::function<CharInputCallbackFunc> callback)
{
    auto itr = _unicodeToCallback.find(unicode);
    if (itr == _unicodeToCallback.end())
        return false;

    _unicodeToCallback[unicode] = callback;
    return true;
}

void KeybindGroup::AddAnyUnicodeCallback(std::function<CharInputCallbackFunc> callback)
{
    _anyUnicodeInputCallback = callback;
}

void KeybindGroup::AddMousePositionCallback(std::function<MousePositionUpdateFunc> callback)
{
    _mousePositionUpdateCallback = callback;
}

void KeybindGroup::AddMouseScrollCallback(std::function<MouseScrollUpdateFunc> callback)
{
    _mouseScrollUpdateCallback = callback;
}

bool KeybindGroup::IsKeybindPressed(u32 keybindHash)
{
    if (IsActive())
    {
        u32 numKeybinds = static_cast<u32>(_keybinds.size());
        for (u32 i = 0; i < numKeybinds; i++)
        {
            Keybind& keybind = _keybinds[i];

            if (keybind.keybindNameHash == keybindHash)
                return keybind.isPressed;
        }
    }

    return false;
}

bool KeybindGroup::MousePositionUpdate(f32 x, f32 y, bool wasAbsorbed)
{
    if (_mousePositionUpdateCallback && !wasAbsorbed)
        return _mousePositionUpdateCallback(x, y);

    return false;
}

bool KeybindGroup::MouseScrollUpdate(f32 x, f32 y, bool wasAbsorbed)
{
    if (_mouseScrollUpdateCallback && !wasAbsorbed)
        return _mouseScrollUpdateCallback(x, y);

    return false;
}

bool KeybindGroup::MouseInputHandler(i32 button, i32 actionMask, i32 modifierMask, bool wasAbsorbed)
{
    // We handle Mouse Input like Keyboard Input
    return KeyboardInputCallback(button, actionMask, modifierMask, wasAbsorbed);
}

bool KeybindGroup::KeyboardInputCallback(i32 glfwKey, i32 actionMask, i32 modifierMask, bool wasAbsorbed)
{
    if (_anyKeyboardInputKeybind)
    {
        Keybind* keybind = _anyKeyboardInputKeybind;
        modifierMask &= GLFW_MOD_SHIFT | GLFW_MOD_CONTROL | GLFW_MOD_ALT;
        KeybindModifier modifiers = static_cast<KeybindModifier>(modifierMask << 1);

        if (actionMask == GLFW_RELEASE)
        {
            return keybind->callback(glfwKey, KeybindAction::Release, modifiers);
        }
        else
        {
            return keybind->callback(glfwKey, KeybindAction::Press, modifiers);
        }
    }

    u32 numKeybinds = static_cast<u32>(_keybinds.size());
    for (u32 i = 0; i < numKeybinds; i++)
    {
        Keybind& keybind = _keybinds[i];

        if (keybind.glfwKey == glfwKey)
        {
            if (keybind.isPressed && wasAbsorbed)
            {
                keybind.isPressed = false;

                if (!keybind.callback)
                    return false;

                KeybindModifier modifiers = static_cast<KeybindModifier>(modifierMask << 1);
                keybind.callback(glfwKey, KeybindAction::Release, modifiers);
            }
            else if (!wasAbsorbed)
            {
                keybind.isPressed = actionMask;

                if (!keybind.callback)
                    return true;

                modifierMask &= GLFW_MOD_SHIFT | GLFW_MOD_CONTROL | GLFW_MOD_ALT;
                KeybindModifier modifiers = static_cast<KeybindModifier>(modifierMask << 1);
                KeybindModifier modifierExcludingNone = keybind.modifierMask & ~KeybindModifier::None;

                bool hasRequiredModifiers = keybind.modifierMask == KeybindModifier::Any || 
                                            ((keybind.modifierMask & KeybindModifier::None) != KeybindModifier::Invalid && modifiers == KeybindModifier::Invalid) || 
                                            (modifierExcludingNone & modifiers) == modifierExcludingNone;
                
                if (actionMask == GLFW_RELEASE  &&
                   (keybind.actionMask == KeybindAction::Release || keybind.actionMask == KeybindAction::Click))
                {
                    return keybind.callback(glfwKey, KeybindAction::Release, modifiers);
                }
                else if (actionMask == GLFW_PRESS   &&
                         hasRequiredModifiers       &&
                        (keybind.actionMask == KeybindAction::Press || keybind.actionMask == KeybindAction::Click))
                {
                    return keybind.callback(glfwKey, KeybindAction::Press, modifiers);
                }
            }
        }
    }

    return false;
}

bool KeybindGroup::CharInputCallback(u32 unicode)
{
    if (_anyUnicodeInputCallback)
    {
        if (_anyUnicodeInputCallback(unicode))
            return true;
    }

    auto itr = _unicodeToCallback.find(unicode);
    if (itr == _unicodeToCallback.end())
        return false;

    if (!itr->second)
        return true;

    return itr->second(unicode);
}
