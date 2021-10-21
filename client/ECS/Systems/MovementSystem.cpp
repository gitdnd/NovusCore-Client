#include "MovementSystem.h"
#include <Math/Geometry.h>
#include <Utils/ByteBuffer.h>
#include <Utils/StringUtils.h>
#include <Networking/Opcode.h>
#include <Window/Window.h>
#include <InputManager.h>
#include "../../Utils/ServiceLocator.h"
#include "../../Utils/MapUtils.h"
#include "../../Rendering/CameraOrbital.h"
#include "../../Rendering/ClientRenderer.h"
#include "../../Rendering/DebugRenderer.h"
#include "../../Rendering/AnimationSystem/AnimationSystem.h"
#include "../Components/Singletons/TimeSingleton.h"
#include "../Components/Singletons/LocalplayerSingleton.h"
#include "../Components/Network/ConnectionSingleton.h"
#include "../Components/Rendering/DebugBox.h"
#include "../Components/Rendering/ModelDisplayInfo.h"
#include "../Components/Transform.h"

#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/norm.hpp>
#include <GLFW/glfw3.h>

void MovementSystem::Init(entt::registry& registry)
{
    LocalplayerSingleton& localplayerSingleton = registry.set<LocalplayerSingleton>();
    localplayerSingleton.movement.flags.canJump = true;
    localplayerSingleton.movement.flags.canChangeDirection = true;

    InputManager* inputManager = ServiceLocator::GetInputManager();
    KeybindGroup* keybindGroup = inputManager->CreateKeybindGroup("Movement", 0);
    keybindGroup->SetActive(true);

    keybindGroup->AddKeyboardCallback("Increase Speed", GLFW_KEY_PAGE_UP, KeybindAction::Press, KeybindModifier::None, [&](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        CameraOrbital* camera = ServiceLocator::GetCameraOrbital();
        if (!camera->IsActive() || localplayerSingleton.entity == entt::null)
            return false;
    
        Transform& transform = registry.get<Transform>(localplayerSingleton.entity);
        transform.moveSpeed += 7.1111f;
    
        return true;
    });
    keybindGroup->AddKeyboardCallback("Decrease Speed", GLFW_KEY_PAGE_DOWN, KeybindAction::Press, KeybindModifier::None, [&](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        CameraOrbital* camera = ServiceLocator::GetCameraOrbital();
        if (!camera->IsActive() || localplayerSingleton.entity == entt::null)
            return false;
    
        Transform& transform = registry.get<Transform>(localplayerSingleton.entity);
        transform.moveSpeed = glm::max(transform.moveSpeed - 7.1111f, 7.1111f);
        return true;
    });
    keybindGroup->AddKeyboardCallback("Auto Run", GLFW_KEY_HOME, KeybindAction::Press, KeybindModifier::None, [&](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        CameraOrbital* camera = ServiceLocator::GetCameraOrbital();
        if (!camera->IsActive() || localplayerSingleton.entity == entt::null)
            return false;
    
        localplayerSingleton.movement.flags.autoRun = !localplayerSingleton.movement.flags.autoRun;
        return true;
    });
    
    keybindGroup->AddKeyboardCallback("Forward", GLFW_KEY_W, KeybindAction::Press, KeybindModifier::None, nullptr);
    keybindGroup->AddKeyboardCallback("Backward", GLFW_KEY_S, KeybindAction::Press, KeybindModifier::None, nullptr);
    keybindGroup->AddKeyboardCallback("Left", GLFW_KEY_A, KeybindAction::Press, KeybindModifier::None, nullptr);
    keybindGroup->AddKeyboardCallback("Right", GLFW_KEY_D, KeybindAction::Press, KeybindModifier::None, nullptr);
    keybindGroup->AddKeyboardCallback("Jump", GLFW_KEY_SPACE, KeybindAction::Press, KeybindModifier::None, nullptr);
}

void MovementSystem::Update(entt::registry& registry)
{
    CameraOrbital* camera = ServiceLocator::GetCameraOrbital();
    LocalplayerSingleton& localplayerSingleton = registry.ctx<LocalplayerSingleton>();

    if (!camera->IsActive() || localplayerSingleton.entity == entt::null)
        return;

    InputManager* inputManager = ServiceLocator::GetInputManager();
    TimeSingleton& timeSingleton = registry.ctx<TimeSingleton>();

    KeybindGroup* movementKeybindGroup = inputManager->GetKeybindGroupByHash("Movement"_h);
    KeybindGroup* cameraOrbitalKeybindGroup = inputManager->GetKeybindGroupByHash("CameraOrbital"_h);
    Transform& transform = registry.get<Transform>(localplayerSingleton.entity);
    ModelDisplayInfo& modelDisplayInfo = registry.get<ModelDisplayInfo>(localplayerSingleton.entity);

    // Here we save our original movement flags to know if we have "changed" direction, and have to update the server, otherwise we can continue sending heartbeats
    Transform::MovementFlags& movementFlags = transform.movementFlags;
    Transform::MovementFlags originalFlags = transform.movementFlags;
    movementFlags.value = 0;

    vec3 originalPosition = transform.position;
    f32 originalPitch = transform.pitch;
    f32 originalYaw = transform.yaw;
    f32 originalYawOffset = 0.0f;// transform.yawOffset;

    Geometry::Triangle triangle;
    f32 terrainHeight = 0.f;
    Terrain::MapUtils::GetTriangleFromWorldPosition(transform.position, triangle, terrainHeight);

    bool isGrounded = transform.position.z <= terrainHeight;
    bool isRightClickDown = cameraOrbitalKeybindGroup->IsKeybindPressed("Right Mouse"_h);
    if (isRightClickDown)
    {
        // Update Direction Vectors to properly align with movement (Does not include yawOffset)
        transform.yawBase = camera->GetYaw();
        transform.yaw = transform.yawBase + transform.yawOffset;
        transform.UpdateDirectionVectors();

        // Only set Pitch if we are flying
        //transform.pitch = camera->GetPitch();
    }
    else
    {
        transform.UpdateDirectionVectors();
    }

    bool isJumping = false;

    // Handle Input
    {
        vec3 moveDirection = vec3(0.f, 0.f, 0.f);
        i8 moveAlongHorizontalAxis = 0;
        i8 moveAlongVerticalAxis = 0;
        bool isMoving = false;

        moveAlongHorizontalAxis += movementKeybindGroup->IsKeybindPressed("Left"_h);
        moveAlongHorizontalAxis -= movementKeybindGroup->IsKeybindPressed("Right"_h);
        moveAlongVerticalAxis += movementKeybindGroup->IsKeybindPressed("Forward"_h);
        moveAlongVerticalAxis -= movementKeybindGroup->IsKeybindPressed("Backward"_h);
        isMoving = moveAlongHorizontalAxis + moveAlongVerticalAxis;

        // Handle Move Direction
        {
            // FORWARD
            if (moveAlongVerticalAxis == 1)
            {
                moveDirection += transform.front;
            }
            // BACKWARD
            else if (moveAlongVerticalAxis == -1)
            {
                moveDirection -= transform.front;
            }

            // LEFT
            if (moveAlongHorizontalAxis == 1)
            {
                moveDirection += transform.left;
            }
            // RIGHT
            else if (moveAlongHorizontalAxis == -1)
            {
                moveDirection -= transform.left;
            }

            // JUMP
            bool isPressingJump = movementKeybindGroup->IsKeybindPressed("Jump"_h);;
            isJumping = isPressingJump && localplayerSingleton.movement.flags.canJump;

            // This ensures we have to stop pressing "Move Jump" and press it again to jump
            localplayerSingleton.movement.flags.canJump = !isPressingJump;

            f32 moveDirectionLength = glm::length2(moveDirection);
            if (moveDirectionLength != 0)
            {
                moveDirection = glm::normalize(moveDirection);
            }
        }

        if (isGrounded)
        {
            localplayerSingleton.movement.flags.canChangeDirection = true;
            transform.velocity = vec3(0.f, 0.f, 0.f);
            transform.fallSpeed = 19.5f;

            // Clip to Terrain
            {
                transform.position.z = terrainHeight;
                movementFlags.GROUNDED = !isJumping;
            }

            movementFlags.FORWARD = moveAlongVerticalAxis == 1;
            movementFlags.BACKWARD = moveAlongVerticalAxis == -1;
            movementFlags.LEFT = moveAlongHorizontalAxis == 1;
            movementFlags.RIGHT = moveAlongHorizontalAxis == -1;
            transform.velocity = moveDirection * transform.moveSpeed;

            if (isJumping)
            {
                transform.velocity += transform.up * 8.f;

                // Ensure we can only manipulate direction of the jump if we did not produce a direction when we initially jumped
                localplayerSingleton.movement.flags.canChangeDirection = moveAlongVerticalAxis == 0 && moveAlongHorizontalAxis == 0;
            }
        }
        else
        {
            movementFlags.GROUNDED = false;

            // Check if we can change direction
            if (isMoving && localplayerSingleton.movement.flags.canChangeDirection)
            {
                localplayerSingleton.movement.flags.canChangeDirection = false;

                f32 moveSpeed = transform.moveSpeed;

                // If we were previously standing still reduce our moveSpeed by half
                if (originalFlags.value == 0 || originalFlags.value == 0x16)
                    moveSpeed *= 0.33f;

                movementFlags.FORWARD = moveAlongVerticalAxis == 1;
                movementFlags.BACKWARD = moveAlongVerticalAxis == -1;
                movementFlags.LEFT = moveAlongHorizontalAxis == 1;
                movementFlags.RIGHT = moveAlongHorizontalAxis == -1;

                vec3 newVelocity = moveDirection * moveSpeed;
                transform.velocity.x = newVelocity.x;
                transform.velocity.y = newVelocity.y;
            }
            else
            {
                // We rebuild our movementFlags every frame to check if we need to send an update to the server, this ensures we keep the correct movement flags
                movementFlags.FORWARD = originalFlags.FORWARD;
                movementFlags.BACKWARD = originalFlags.BACKWARD;
                movementFlags.LEFT = originalFlags.LEFT;
                movementFlags.RIGHT = originalFlags.RIGHT;
            }

            transform.fallSpeed += transform.fallAcceleration * timeSingleton.deltaTime;
            transform.velocity -= transform.up * transform.fallSpeed * timeSingleton.deltaTime;
        }

        bool isMovingForward = (movementFlags.FORWARD && !movementFlags.BACKWARD);
        bool isMovingBackward = (movementFlags.BACKWARD && !movementFlags.FORWARD);
        bool isMovingLeft = (movementFlags.LEFT && !movementFlags.RIGHT);
        bool isMovingRight = (movementFlags.RIGHT && !movementFlags.LEFT);

        if (isMovingForward && isMovingLeft)
        {
            transform.yawOffset = 45.0f;
        }
        else if (isMovingForward && isMovingRight)
        {
            transform.yawOffset = -45.0f;
        }
        else if (isMovingBackward && isMovingLeft)
        {
            transform.yawOffset = -45.0f;
        }
        else if (isMovingBackward && isMovingRight)
        {
            transform.yawOffset = 45.0f;
        }
        else if (isMovingLeft)
        {
            transform.yawOffset = 90.0f;
        }
        else if (isMovingRight)
        {
            transform.yawOffset = -90.0f;
        }
        else
        {
            transform.yawOffset = 0.0f;
        }

        transform.yaw = transform.yawBase + transform.yawOffset;
    }

    f32 sqrVelocity = glm::length2(transform.velocity);
    if (sqrVelocity != 0)
    {
        vec3 newPosition = transform.position + (transform.velocity * timeSingleton.deltaTime);
        Terrain::MapUtils::GetTriangleFromWorldPosition(newPosition, triangle, terrainHeight);

        f32 diff = newPosition.z - terrainHeight;
        if (!isJumping && originalFlags.GROUNDED && diff < 0.25f)
        {
            newPosition.z = terrainHeight;
        }
        else
        {
            newPosition.z = glm::max(newPosition.z, terrainHeight);
        }

        if (isGrounded)
        {
            f32 angle = triangle.GetSteepnessAngle();

            // TODO: Properly Push the player down the slope
            if (angle <= 50 || newPosition.z <= transform.position.z)
            {
                transform.position = newPosition;
            }
        }
        else
        {
            transform.position = newPosition;
        }
    }

    vec3 cameraTransformPos = transform.position + vec3(0.0f, 0.0f, 1.3f);
    camera->SetPosition(cameraTransformPos);

    mat4x4 transformMatrix = transform.GetMatrix();
    DebugRenderer* debugRenderer = ServiceLocator::GetClientRenderer()->GetDebugRenderer();
    debugRenderer->DrawMatrix(transformMatrix, 1.0f);

    // If our movement flags changed, lets see what animations we should play
    if (movementFlags.value != originalFlags.value)
    {
        AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();
        u32 instanceID = modelDisplayInfo.instanceID;

        AnimationSystem::AnimationInstanceData* animationInstanceData = nullptr;
        if (animationSystem->GetAnimationInstanceData(instanceID, animationInstanceData))
        {
            bool playForwardAnimation = (movementFlags.FORWARD || movementFlags.LEFT || movementFlags.RIGHT) && !movementFlags.BACKWARD;
            bool playBackwardAnimation = (movementFlags.BACKWARD) && !movementFlags.FORWARD;

            if (playForwardAnimation)
            {
                // Forward Animation
                if (!animationInstanceData->IsAnimationIDPlaying(5))
                {
                    animationSystem->TryStopAllAnimations(instanceID);
                    animationSystem->TryPlayAnimationID(instanceID, 5, true, true);
                }
            }
            else if (playBackwardAnimation)
            {
                // Backward Animation
                if (!animationInstanceData->IsAnimationIDPlaying(13))
                {
                    animationSystem->TryStopAllAnimations(instanceID);
                    animationSystem->TryPlayAnimationID(instanceID, 13, true, true);
                }
            }
            else
            {
                // Stand
                if (!animationInstanceData->IsAnimationIDPlaying(0))
                {
                    animationSystem->TryStopAllAnimations(instanceID);
                    animationSystem->TryPlayAnimationID(instanceID, 0, true, true);
                }
            }
        }
    }

    if (transform.position  != originalPosition || 
        transform.pitch     != originalPitch    ||
        transform.yaw       != originalYaw      ||
        transform.yawOffset != originalYaw)
    {
        registry.emplace_or_replace<TransformIsDirty>(localplayerSingleton.entity);
    }
}
