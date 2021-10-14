#include "SimulateDebugCubeSystem.h"
#include <entt.hpp>
#include <InputManager.h>
#include <GLFW/glfw3.h>
#include <tracy/Tracy.hpp>
#include <Renderer/Renderer.h>
#include <InputManager.h>
#include <Math/Geometry.h>
#include "../../../Utils/ServiceLocator.h"
#include "../../../Utils/MapUtils.h"
#include "../../../Rendering/DebugRenderer.h"
#include "../../../Rendering/Camera.h"

#include "../../Components/Singletons/TimeSingleton.h"
#include "../../Components/Singletons/NDBCSingleton.h"
#include "../../Components/Transform.h"
#include "../../Components/Physics/Rigidbody.h"
#include "../../Components/Rendering/DebugBox.h"
#include "../../Components/Rendering/ModelDisplayInfo.h"
#include "../../Components/Rendering/VisibleModel.h"

#include <random>

void SimulateDebugCubeSystem::Init(entt::registry& registry)
{
    InputManager* inputManager = ServiceLocator::GetInputManager();
    KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("Debug"_h);

    keybindGroup->AddKeyboardCallback("SpawnDebugBox", GLFW_KEY_B, KeybindAction::Press, KeybindModifier::Any, [&registry](i32 key, KeybindAction action, KeybindModifier modifier)
    {
        Camera* camera = ServiceLocator::GetCamera();

        NDBCSingleton& ndbcSingleton = registry.ctx<NDBCSingleton>();
        NDBC::File* creatureDisplayInfoFile = ndbcSingleton.GetNDBCFile("CreatureDisplayInfo");
        NDBC::File* creatureModelDataFile = ndbcSingleton.GetNDBCFile("CreatureModelData");

        // Seed with a real random value, if available
        std::random_device r;
        std::mt19937 mt(r());
        std::uniform_int_distribution<int> uniform_dist(75, 30000);

        for (u32 i = 0; i < 100; i++)
        {
            // Create ENTT entity
            entt::entity entity = registry.create();

            Transform& transform = registry.emplace<Transform>(entity);
            transform.position = camera->GetPosition();
            transform.position += vec3((i % 10) * 10.0f, (i / 10) * 10.0f, 0);

            //transform.scale = vec3(0.5f, 0.5f, 2.f); // "Ish" scale for humans
            transform.UpdateRotationMatrix();

            registry.emplace<TransformIsDirty>(entity);
            registry.emplace<Rigidbody>(entity);
            //registry.emplace<DebugBox>(entity);

            registry.emplace<VisibleModel>(entity);

            u32 displayID = 0;
            while (true)
            {
                displayID = uniform_dist(mt);

                NDBC::CreatureDisplayInfo* creatureDisplayInfo = creatureDisplayInfoFile->GetRowById<NDBC::CreatureDisplayInfo>(displayID);
                if (creatureDisplayInfo == nullptr)
                {
                    continue;
                }

                NDBC::CreatureModelData* creatureModelData = creatureModelDataFile->GetRowById<NDBC::CreatureModelData>(creatureDisplayInfo->modelId);
                if (creatureDisplayInfo == nullptr)
                {
                    continue;
                }

                u32 modelPathStringID = creatureModelData->modelPath;
                StringTable*& stringTable = creatureModelDataFile->GetStringTable();

                std::string modelPath = stringTable->GetString(modelPathStringID);
                std::string modelPathToLower = "";
                modelPathToLower.resize(modelPath.length());

                std::transform(modelPath.begin(), modelPath.end(), modelPathToLower.begin(), [](char c) { return std::tolower((int)c); });

                if (!StringUtils::BeginsWith(modelPathToLower, "character"))
                {
                    break;
                }
            }

            ModelDisplayInfo& modelDisplayInfo = registry.emplace<ModelDisplayInfo>(entity, ModelType::Creature, displayID); // 65 horse
        }

        return true;
    });
}

void SimulateDebugCubeSystem::Update(entt::registry& registry, DebugRenderer* debugRenderer)
{
    TimeSingleton& timeSingleton = registry.ctx<TimeSingleton>();

    auto rigidbodyView = registry.view<Transform, Rigidbody>();
    rigidbodyView.each([&](const auto entity, Transform& transform)
    {
        // Make all rigidbodies "fall"
        f32 dist = GRAVITY_SCALE * timeSingleton.deltaTime;

        Geometry::AABoundingBox box;
        box.min = transform.position;
        box.min.x -= transform.scale.x;
        box.min.y -= transform.scale.y;

        box.max = transform.position + transform.scale;

        Geometry::Triangle triangle;
        f32 height = 0;

        vec3 distToCollision;
        if (Terrain::MapUtils::Intersect_AABB_TERRAIN_SWEEP(box, triangle, vec3(0, 0, -1), height, dist, distToCollision))
        {
            transform.position.z += distToCollision.z;
            registry.remove<Rigidbody>(entity);
        }
        else
        {
            transform.position.z -= dist;
        }

        registry.emplace_or_replace<TransformIsDirty>(entity);
    });

    auto debugCubeView = registry.view<Transform, DebugBox>();
    debugCubeView.each([&](const auto entity, Transform& transform)
    {
        vec3 min = transform.position;
        min.x -= transform.scale.x;
        min.y -= transform.scale.y;
        vec3 max = transform.position + transform.scale;

        u32 color = 0xff0000ff; // Red if it doesn't have a rigidbody
        if (registry.has<Rigidbody>(entity))
        {
            color = 0xff00ff00; // Green if it does
        }

        // This registers the model to be rendered THIS frame.
        debugRenderer->DrawAABB3D(min, max, color);
    });
}