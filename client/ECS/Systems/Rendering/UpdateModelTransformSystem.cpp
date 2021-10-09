#include "UpdateModelTransformSystem.h"
#include <entt.hpp>
#include <tracy/Tracy.hpp>
#include <Renderer/Renderer.h>
#include "../../../Utils/ServiceLocator.h"
#include "../../../Rendering/ClientRenderer.h"
#include "../../../Rendering/CModelRenderer.h"
#include "../../Components/Transform.h"
#include "../../Components/Rendering/ModelDisplayInfo.h"
#include "../../Components/Rendering/VisibleModel.h"

void UpdateModelTransformSystem::Update(entt::registry& registry)
{
    Renderer::Renderer* renderer = ServiceLocator::GetRenderer();
    ClientRenderer* clientRenderer = ServiceLocator::GetClientRenderer();

    CModelRenderer* cModelRenderer = clientRenderer->GetCModelRenderer();
    Renderer::GPUVector<mat4x4>& modelInstanceMatrices = cModelRenderer->GetModelInstanceMatrices();

    auto modelView = registry.view<Transform, TransformIsDirty, ModelDisplayInfo, VisibleModel>();

    if (modelView.size_hint() == 0)
        return;

    modelInstanceMatrices.WriteLock([&](std::vector<mat4x4>& instanceMatrices)
    {
        modelView.each([&](const auto entity, Transform& transform, ModelDisplayInfo& modelDisplayInfo)
        {
            mat4x4& instanceMatrix = instanceMatrices[modelDisplayInfo.instanceID];

            // Update the instance
            instanceMatrix = transform.GetMatrix();

            modelInstanceMatrices.SetDirtyElement(modelDisplayInfo.instanceID);
        });
    });

    registry.clear<TransformIsDirty>();
}