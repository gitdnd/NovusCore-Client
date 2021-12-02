#include "UpdateCModelInfoSystem.h"
#include <entt.hpp>
#include <tracy/Tracy.hpp>
#include <Renderer/Renderer.h>

#include "../../../Utils/ServiceLocator.h"
#include "../../../Utils/MapUtils.h"

#include "../../../Rendering/ClientRenderer.h"
#include "../../../Rendering/CModelRenderer.h"

#include <Gameplay/ECS/Components/Transform.h>
#include "../../Components/Rendering/CModelInfo.h"
#include "../../Components/Rendering/Collidable.h"
#include "../../Components/Singletons/MapSingleton.h"

void UpdateCModelInfoSystem::Update(entt::registry& registry)
{
    Renderer::Renderer* renderer = ServiceLocator::GetRenderer();
    ClientRenderer* clientRenderer = ServiceLocator::GetClientRenderer();
    CModelRenderer* cModelRenderer = clientRenderer->GetCModelRenderer();

    auto modelView = registry.view<Transform, CModelInfo, TransformIsDirty>();
    if (modelView.size_hint() == 0)
        return;

    MapSingleton& mapSingleton = registry.ctx<MapSingleton>();
    Terrain::Map& currentMap = mapSingleton.GetCurrentMap();

    modelView.each([&](const auto entity, Transform& transform, CModelInfo& cmodelInfo)
    {
        vec2 adtPos = Terrain::MapUtils::WorldPositionToADTCoordinates(transform.position);
        vec2 chunkPos = Terrain::MapUtils::GetChunkFromAdtPosition(adtPos);
        vec2 chunkRemainder = chunkPos - glm::floor(chunkPos);
        u32 chunkID = Terrain::MapUtils::GetChunkIdFromChunkPos(chunkPos);

        if (chunkID == cmodelInfo.currentChunkID)
            return;

        bool isCollidable = registry.all_of<Collidable>(entity);

        // Check if we need to remove from oldChunk
        {
            if (!cmodelInfo.isStaticModel)
            {
                SafeVector<entt::entity>* entityList = currentMap.GetEntityListByChunkID(cmodelInfo.currentChunkID);
                if (entityList)
                {
                    entityList->WriteLock([&](std::vector<entt::entity>& entityList)
                    {
                        auto itr = std::find(entityList.begin(), entityList.end(), entity);
                        entityList.erase(itr);
                    });
                }
            }

            SafeVector<entt::entity>* collidableEntityList = currentMap.GetCollidableEntityListByChunkID(cmodelInfo.currentChunkID);
            if (isCollidable)
            {
                if (collidableEntityList)
                {
                    collidableEntityList->WriteLock([&](std::vector<entt::entity>& entityList)
                    {
                        auto itr = std::find(entityList.begin(), entityList.end(), entity);
                        entityList.erase(itr);
                    });
                }
            }
        }

        // Check if we need to add to newChunk
        {
            if (!cmodelInfo.isStaticModel)
            {
                SafeVector<entt::entity>* entityList = currentMap.GetEntityListByChunkID(chunkID);
                if (entityList)
                {
                    entityList->PushBack(entity);
                }
            }

            SafeVector<entt::entity>* collidableEntityList = currentMap.GetCollidableEntityListByChunkID(chunkID);
            if (isCollidable)
            {
                if (collidableEntityList)
                {
                    collidableEntityList->PushBack(entity);
                }
            }
        }

        // Update currentChunkID
        cmodelInfo.currentChunkID = chunkID;
    });
}