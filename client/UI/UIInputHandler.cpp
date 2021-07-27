#include "UIInputHandler.h"
#include <InputManager.h>
#include <GLFW/glfw3.h>
#include <tracy/Tracy.hpp>
#include <entity/entity.hpp>
#include <entity/registry.hpp>

#include "../Utils/ServiceLocator.h"

#include "ECS/Components/Singletons/UIDataSingleton.h"
#include "ECS/Components/ElementInfo.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/TransformEvents.h"
#include "ECS/Components/SortKey.h"
#include "ECS/Components/Collision.h"
#include "ECS/Components/Collidable.h"
#include "ECS/Components/Visible.h"
#include "ECS/Components/NotCulled.h"

#include "Utils/ElementUtils.h"
#include "Utils/TransformUtils.h"
#include "Utils/ColllisionUtils.h"
#include "Utils/EventUtils.h"

#include "angelscript/Inputfield.h"
#include "angelscript/Checkbox.h"
#include "angelscript/Slider.h"
#include "angelscript/SliderHandle.h"

namespace UIInput
{
    bool OnMouseClick(i32 key, KeybindAction action, KeybindModifier modifier)
    {
        ZoneScoped;
        const hvec2 mouse = UIUtils::Transform::WindowPositionToUIPosition(ServiceLocator::GetInputManager()->GetMousePosition());
        entt::registry* registry = ServiceLocator::GetUIRegistry();
        UISingleton::UIDataSingleton& dataSingleton = registry->ctx<UISingleton::UIDataSingleton>();

        //Unfocus last focused widget.
        entt::entity lastFocusedWidget = dataSingleton.focusedWidget;
        if (dataSingleton.focusedWidget != entt::null)
        {
            auto [elementInfo, events] = registry->get<UIComponent::ElementInfo, UIComponent::TransformEvents>(dataSingleton.focusedWidget);
            UIUtils::ExecuteEvent(elementInfo.scriptingObject, events.onFocusLostCallback);
            dataSingleton.focusedWidget = entt::null;
        }

        if (dataSingleton.draggedWidget != entt::null && action == KeybindAction::Release)
        {
            auto [elementInfo, events] = registry->get<UIComponent::ElementInfo, UIComponent::TransformEvents>(dataSingleton.draggedWidget);
            UIUtils::ExecuteEvent(elementInfo.scriptingObject, events.onDragEndedCallback);
            dataSingleton.draggedWidget = entt::null;

            return true;
        }

        auto eventGroup = registry->group<>(entt::get<UIComponent::TransformEvents, UIComponent::ElementInfo, UIComponent::SortKey, UIComponent::Collision, UIComponent::Collidable, UIComponent::Visible, UIComponent::NotCulled>);
        eventGroup.sort<UIComponent::SortKey>([](const UIComponent::SortKey& first, const UIComponent::SortKey& second) { return first.key > second.key; });
        for (auto entity : eventGroup)
        {
            UIComponent::TransformEvents& events = eventGroup.get<UIComponent::TransformEvents>(entity);
            const UIComponent::ElementInfo& elementInfo = eventGroup.get<UIComponent::ElementInfo>(entity);
            const UIComponent::Collision& collision = eventGroup.get<UIComponent::Collision>(entity);

            // Check so mouse if within widget bounds.
            if (mouse.x < collision.minBound.x || mouse.x > collision.maxBound.x || mouse.y < collision.minBound.y || mouse.y > collision.maxBound.y)
                continue;

            // Don't interact with the last focused widget directly. Reserving first click for unfocusing it but still block clicking through it.
            // Also check if we have any events we can actually call else exit out early.
            if (lastFocusedWidget == entity || !events.flags)
                return true;

            if (action == KeybindAction::Press)
            {
                if (events.IsDraggable())
                {
                    const UIComponent::Transform& transform = registry->get<UIComponent::Transform>(entity);
                    dataSingleton.draggedWidget = entity;
                    dataSingleton.dragOffset = mouse - (transform.anchorPosition + transform.position);
                    
                    UIUtils::ExecuteEvent(elementInfo.scriptingObject, events.onDragStartedCallback);
                }
            }
            else if (action == KeybindAction::Release)
            {
                if (events.IsFocusable())
                {
                    dataSingleton.focusedWidget = entity;
            
                    UIUtils::ExecuteEvent(elementInfo.scriptingObject, events.onFocusGainedCallback);
                }
            
                if (events.IsClickable())
                {
                    if (elementInfo.type == UI::ElementType::UITYPE_CHECKBOX)
                    {
                        UIScripting::Checkbox* checkBox = reinterpret_cast<UIScripting::Checkbox*>(elementInfo.scriptingObject);
                        checkBox->ToggleChecked();
                    } 
                    else if(elementInfo.type == UI::ElementType::UITYPE_SLIDER)
                    {
                        UIScripting::Slider* slider = reinterpret_cast<UIScripting::Slider*>(elementInfo.scriptingObject);
                        slider->OnClicked(mouse);
                    }
                    UIUtils::ExecuteEvent(elementInfo.scriptingObject, events.onClickCallback);
                }
            }

            return true;
        }

        return false;
    }

    bool OnMousePositionUpdate(f32 x, f32 y)
    {
        ZoneScoped;

        const hvec2 mouse = UIUtils::Transform::WindowPositionToUIPosition(hvec2(x, y));

        entt::registry* registry = ServiceLocator::GetUIRegistry();
        UISingleton::UIDataSingleton& dataSingleton = registry->ctx<UISingleton::UIDataSingleton>();
        if (dataSingleton.draggedWidget != entt::null)
        {
            const UIComponent::ElementInfo* elementInfo = &registry->get<UIComponent::ElementInfo>(dataSingleton.draggedWidget);
            auto [transform, events] = registry->get<UIComponent::Transform, UIComponent::TransformEvents>(dataSingleton.draggedWidget);

            hvec2 newPos = mouse - transform.anchorPosition - dataSingleton.dragOffset;
            if (events.HasFlag(UI::TransformEventsFlags::UIEVENTS_FLAG_DRAGLOCK_X))
                newPos.x = transform.position.x;
            else if (events.HasFlag(UI::TransformEventsFlags::UIEVENTS_FLAG_DRAGLOCK_Y))
                newPos.y = transform.position.y;

            transform.position = newPos;

            // Handle OnDrag(s)
            if (elementInfo->type == UI::ElementType::UITYPE_SLIDERHANDLE)
            {
                UIScripting::SliderHandle* sliderHandle = reinterpret_cast<UIScripting::SliderHandle*>(elementInfo->scriptingObject);
                sliderHandle->OnDragged();
            }

            UIUtils::MarkDirty(registry, dataSingleton.draggedWidget);
            UIUtils::MarkChildrenDirty(registry, dataSingleton.draggedWidget);
            UIUtils::Transform::UpdateChildPositions(registry, dataSingleton.draggedWidget);
            UIUtils::Collision::MarkBoundsDirty(registry, dataSingleton.draggedWidget);

            return true;
        }

        // Handle hover.
        auto eventGroup = registry->group<>(entt::get<UIComponent::TransformEvents, UIComponent::SortKey, UIComponent::Collision, UIComponent::Collidable, UIComponent::Visible, UIComponent::NotCulled>);
        eventGroup.sort<UIComponent::SortKey>([](const UIComponent::SortKey& first, const UIComponent::SortKey& second) { return first.key > second.key; });
        for (auto entity : eventGroup)
        {
            if (dataSingleton.draggedWidget == entity)
                continue;

            const UIComponent::Collision& collision = eventGroup.get<UIComponent::Collision>(entity);
            // Check so mouse if within widget bounds.
            if (mouse.x < collision.minBound.x || mouse.x > collision.maxBound.x || mouse.y < collision.minBound.y || mouse.y > collision.maxBound.y)
                continue;

            // Hovered widget hasn't changed.
            if (dataSingleton.hoveredWidget == entity)
                break;
            dataSingleton.hoveredWidget = entity;

            // TODO Update EventState.

            break;
        }

        return false;
    }

    bool OnKeyboardInput(i32 key, KeybindAction action, KeybindModifier modifiers)
    {
        ZoneScoped;
        entt::registry* registry = ServiceLocator::GetUIRegistry();
        UISingleton::UIDataSingleton& dataSingleton = registry->ctx<UISingleton::UIDataSingleton>();

        if (dataSingleton.focusedWidget == entt::null)
            return false;

        if (action == KeybindAction::Release)
            return true;

        const UIComponent::ElementInfo& elementInfo = registry->get<UIComponent::ElementInfo>(dataSingleton.focusedWidget);
        UIComponent::TransformEvents& events = registry->get<UIComponent::TransformEvents>(dataSingleton.focusedWidget);
        if (key == GLFW_KEY_ESCAPE)
        {
            UIUtils::ExecuteEvent(elementInfo.scriptingObject, events.onFocusLostCallback);
            dataSingleton.focusedWidget = entt::null;

            return true;
        }

        switch (elementInfo.type)
        {
        case UI::ElementType::UITYPE_INPUTFIELD:
        {
            UIScripting::InputField* inputFieldAS = reinterpret_cast<UIScripting::InputField*>(elementInfo.scriptingObject);
            inputFieldAS->HandleKeyInput(key);
            break;
        }
        case UI::ElementType::UITYPE_CHECKBOX:
        {
            UIScripting::Checkbox* checkBoxAS = reinterpret_cast<UIScripting::Checkbox*>(elementInfo.scriptingObject);
            checkBoxAS->HandleKeyInput(key);
            break;
        }
        default:
            if (key == GLFW_KEY_ENTER && events.IsClickable())
            {
                UIUtils::ExecuteEvent(elementInfo.scriptingObject, events.onClickCallback);
            }
            break;
        }

        return true;
    }

    bool OnCharInput(u32 unicodeKey)
    {
        ZoneScoped;
        entt::registry* registry = ServiceLocator::GetUIRegistry();
        UISingleton::UIDataSingleton& dataSingleton = registry->ctx<UISingleton::UIDataSingleton>();

        if (dataSingleton.focusedWidget == entt::null)
            return false;

        const UIComponent::ElementInfo& elementInfo = registry->get<UIComponent::ElementInfo>(dataSingleton.focusedWidget);
        const UIComponent::TransformEvents& events = registry->get<UIComponent::TransformEvents>(dataSingleton.focusedWidget);
        if (elementInfo.type == UI::ElementType::UITYPE_INPUTFIELD)
        {
            UIScripting::InputField* inputField = reinterpret_cast<UIScripting::InputField*>(elementInfo.scriptingObject);
            inputField->HandleCharInput((char)unicodeKey);
            inputField->MarkSelfDirty();
        }

        return true;
    }

    void RegisterCallbacks()
    {
        InputManager* inputManager = ServiceLocator::GetInputManager();
        KeybindGroup* keybindGroup = inputManager->CreateKeybindGroup("UI Input", 10);
        keybindGroup->SetActive(true);

        keybindGroup->AddKeyboardCallback("Mouse Left", GLFW_MOUSE_BUTTON_LEFT, KeybindAction::Click, KeybindModifier::Any, std::bind(&OnMouseClick, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        keybindGroup->AddAnyKeyboardCallback("Keyboard Input", std::bind(&OnKeyboardInput, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        keybindGroup->AddAnyUnicodeCallback(std::bind(&OnCharInput, std::placeholders::_1));
        keybindGroup->AddMousePositionCallback(std::bind(&OnMousePositionUpdate, std::placeholders::_1, std::placeholders::_2));

        // Create mouse group upfront. Reduces hitching from first mouse input.
        auto eventGroup = ServiceLocator::GetUIRegistry()->group<>(entt::get<UIComponent::TransformEvents, UIComponent::ElementInfo, UIComponent::SortKey, UIComponent::Collision, UIComponent::Collidable, UIComponent::Visible, UIComponent::NotCulled>);
    }
}