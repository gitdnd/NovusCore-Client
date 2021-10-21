#include "AnimationSystem.h"

#include "../ClientRenderer.h"
#include "../CModelRenderer.h"
#include "../../Utils/ServiceLocator.h"

#include <shared_mutex>

bool AnimationSystem::AddInstance(u32 instanceID, const AnimationInstanceData& animationInstanceData)
{
    bool didAdd = false;

    _instanceIDToAnimationInstanceData.WriteLock([&](robin_hood::unordered_map<u32, AnimationInstanceData>& animationInstanceDatas)
    {
        auto itr = animationInstanceDatas.find(instanceID);
        if (itr != animationInstanceDatas.end())
            return;

        animationInstanceDatas[instanceID] = animationInstanceData;
        didAdd = true;
    });

    return didAdd;
}

bool AnimationSystem::RemoveInstance(u32 instanceID)
{
    bool didRemove = false;

    _instanceIDToAnimationInstanceData.WriteLock([&](robin_hood::unordered_map<u32, AnimationInstanceData>& animationInstanceDatas)
    {
        auto itr = animationInstanceDatas.find(instanceID);
        if (itr == animationInstanceDatas.end())
            return;

        animationInstanceDatas.erase(itr);
        didRemove = true;
    });

    return didRemove;
}

bool AnimationSystem::GetAnimationInstanceData(u32 instanceID, AnimationInstanceData*& out)
{
    bool didFind = false;

    _instanceIDToAnimationInstanceData.WriteLock([&](robin_hood::unordered_map<u32, AnimationInstanceData>& animationInstanceDatas)
    {
        auto itr = animationInstanceDatas.find(instanceID);
        if (itr == animationInstanceDatas.end())
            return;

        out = &itr->second;
        didFind = true;
    });

    return didFind;
}

bool AnimationSystem::TryPlayAnimationID(u32 instanceID, u16 animationID, bool play, bool loop /* = false */)
{
    ClientRenderer* clientRenderer = ServiceLocator::GetClientRenderer();
    CModelRenderer* cModelRenderer = clientRenderer->GetCModelRenderer();

    const CModelRenderer::ModelInstanceData& modelInstanceData = cModelRenderer->GetModelInstanceData(instanceID);
    const CModelRenderer::AnimationModelInfo& animationModelInfo = cModelRenderer->GetAnimationModelInfo(modelInstanceData.modelID);

    if (animationModelInfo.numSequences == 0)
        return false;

    AnimationSystem::AnimationInstanceData* animationInstanceData = nullptr;
    if (!GetAnimationInstanceData(instanceID, animationInstanceData))
        return false;

    u32 sequenceID = std::numeric_limits<u32>().max();

    cModelRenderer->GetAnimationSequences().ReadLock([&](const std::vector<CModelRenderer::AnimationSequence>& animationSequences)
    {
        for (u32 i = 0; i < animationModelInfo.numSequences; i++)
        {
            const CModelRenderer::AnimationSequence& animationSequence = animationSequences[animationModelInfo.sequenceOffset + i];

            if (animationSequence.flags.isAlwaysPlaying || animationSequence.flags.isAlias)
                continue;

            if (animationSequence.animationId == animationID)
            {
                sequenceID = i;
                break;
            }
        }
    });

    bool canPlayAnimation = sequenceID != std::numeric_limits<u32>().max();
    if (canPlayAnimation)
    {
        CModelRenderer::AnimationRequest request;
        request.instanceId = instanceID;
        request.sequenceId = sequenceID;
        request.flags.isPlaying = play;
        request.flags.isLooping = loop;
        request.flags.stopAll = false;

        cModelRenderer->AddAnimationRequest(request);

        auto activeAnimationIDsitr = std::find(animationInstanceData->activeAnimationIDs.begin(), animationInstanceData->activeAnimationIDs.end(), animationID);

        if (play && activeAnimationIDsitr == animationInstanceData->activeAnimationIDs.end())
        {
            animationInstanceData->activeAnimationIDs.push_back(animationID);
        }
        else if (!play && activeAnimationIDsitr != animationInstanceData->activeAnimationIDs.end())
        {
            animationInstanceData->activeAnimationIDs.erase(activeAnimationIDsitr);
        }
    }

    return canPlayAnimation;
}

void AnimationSystem::TryStopAllAnimations(u32 instanceID)
{
    ClientRenderer* clientRenderer = ServiceLocator::GetClientRenderer();
    CModelRenderer* cModelRenderer = clientRenderer->GetCModelRenderer();

    const CModelRenderer::ModelInstanceData& modelInstanceData = cModelRenderer->GetModelInstanceData(instanceID);
    const CModelRenderer::AnimationModelInfo& animationModelInfo = cModelRenderer->GetAnimationModelInfo(modelInstanceData.modelID);

    if (animationModelInfo.numSequences == 0)
        return;

    AnimationSystem::AnimationInstanceData* animationInstanceData = nullptr;
    if (!GetAnimationInstanceData(instanceID, animationInstanceData))
        return;

    CModelRenderer::AnimationRequest request;
    request.instanceId = instanceID;
    request.sequenceId = 0;
    request.flags.isPlaying = false;
    request.flags.isLooping = false;
    request.flags.stopAll = true;

    cModelRenderer->AddAnimationRequest(request);

    animationInstanceData->activeAnimationIDs.clear();
}

bool AnimationSystem::AnimationInstanceData::IsAnimationIDPlaying(u16 animationID)
{
    auto itr = std::find(activeAnimationIDs.begin(), activeAnimationIDs.end(), animationID);
    return itr != activeAnimationIDs.end();
}
