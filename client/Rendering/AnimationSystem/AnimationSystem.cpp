#include "AnimationSystem.h"

#include "../ClientRenderer.h"
#include "../../Utils/ServiceLocator.h"

i32 AnimationModelInfo::GetBoneAnimation(i32 boneKeyId)
{
    u32 boneIndex = GetBoneIndexFromBoneKeyId(boneKeyId);
    return boneInfos[boneIndex].activeSequence.id;
}
bool AnimationModelInfo::SetBoneAnimation(i32 boneKeyId, i32 animationId)
{
    if (animationId == -1)
        return UnsetBoneAnimation(boneKeyId);

    u32 boneIndex = GetBoneIndexFromBoneKeyId(boneKeyId);
    BoneInfo& boneInfo = boneInfos[boneIndex];

    // Handle Repeats, Variations, Transition and other such features Here
    {

    }

    // Get sequenceId from animationId
    i32 sequenceId = GetSequenceIdFromAnimationId(animationId);
    if (sequenceId == -1)
        return false;

    boneInfo.activeSequence.id = sequenceId;

    CModelRenderer::AnimationRequest animationRequest;
    {
        animationRequest.instanceId = instanceId;
        animationRequest.boneIndex = boneIndex;
        animationRequest.sequenceIndex = sequenceId;

        animationRequest.flags.isPlaying = true;
        animationRequest.flags.isLooping = false;
    }

    CModelRenderer* cmodelRenderer = ServiceLocator::GetClientRenderer()->GetCModelRenderer();
    cmodelRenderer->AddAnimationRequest(animationRequest);

    return true;
}
bool AnimationModelInfo::UnsetBoneAnimation(i32 boneKeyId)
{
    u32 boneIndex = GetBoneIndexFromBoneKeyId(boneKeyId);
    BoneInfo& boneInfo = boneInfos[boneIndex];

    u32 activeSequenceId = boneInfo.activeSequence.id;
    boneInfo.activeSequence.id = std::numeric_limits<u32>().max();
    boneInfo.transitionSequence.id = std::numeric_limits<u32>().max();

    CModelRenderer::AnimationRequest animationRequest;
    {
        animationRequest.instanceId = instanceId;
        animationRequest.boneIndex = boneIndex;
        animationRequest.sequenceIndex = activeSequenceId;

        animationRequest.flags.isPlaying = false;
        animationRequest.flags.isLooping = false;
    }

    CModelRenderer* cmodelRenderer = ServiceLocator::GetClientRenderer()->GetCModelRenderer();
    cmodelRenderer->AddAnimationRequest(animationRequest);

    return activeSequenceId != std::numeric_limits<u32>().max();
}

u32 AnimationModelInfo::GetBoneIndexFromBoneKeyId(i32 boneKeyId)
{
    u32 boneIndex = 0;
    if (boneKeyId != -1)
    {
        auto itr = boneKeyIdToBoneIndex.find(boneKeyId);
        if (itr == boneKeyIdToBoneIndex.end())
        {
            boneIndex = std::numeric_limits<u32>().max();
        }
        else
        {
            boneIndex = itr->second;
        }
    }    
    
    if (boneIndex >= boneInfos.size())
    {
        DebugHandler::PrintFatal("[AnimationSystem] Attempted to call GetBoneAnimation with BoneKeyId(%u) not present", boneKeyId);
    }

    return boneIndex;
}

i32 AnimationModelInfo::GetSequenceIdFromAnimationId(i32 animationId)
{
    CModelRenderer* cmodelRenderer = ServiceLocator::GetClientRenderer()->GetCModelRenderer();

    auto complexModelsFileDataReadLock = SafeVectorScopedReadLock(cmodelRenderer->GetLoadedComplexModelsFileData());
    const std::vector<CModel::ComplexModel>& complexModelsFileData = complexModelsFileDataReadLock.Get();
    const CModel::ComplexModel& complexModelFileData = complexModelsFileData[modelId];

    i32 sequenceIndex = -1;
    for (u32 i = 0; i < complexModelFileData.sequences.size(); i++)
    {
        const CModel::ComplexAnimationSequence& sequence = complexModelFileData.sequences[i];
        if (sequence.flags.isAlwaysPlaying || sequence.flags.isAlias)
            continue;

        if (sequence.id == animationId && sequence.subId == 0)
        {
            sequenceIndex = i;
            break;
        }
    }

    // Handle Fallback here
    {

    }

    return sequenceIndex;
}

bool AnimationSystem::AddInstance(u32 instanceID, const CModelRenderer::LoadedComplexModel& loadedComplexModel)
{
    CModelRenderer* cmodelRenderer = ServiceLocator::GetClientRenderer()->GetCModelRenderer();

    const CModelRenderer::AnimationModelInfo& animationModelInfo = cmodelRenderer->GetAnimationModelInfo(loadedComplexModel.modelID);
    if (animationModelInfo.numBones == 0)
        return false;

    bool result = false;

    _instanceIdToAnimationModelInfo.WriteLock([&](robin_hood::unordered_map<u32, AnimationModelInfo>& instanceIdToAnimationModelInfo)
    {
        auto itr = instanceIdToAnimationModelInfo.find(instanceID);
        if (itr != instanceIdToAnimationModelInfo.end())
            return;

        AnimationModelInfo animationInfo;

        {
            auto boneInfoReadLock = SafeVectorScopedReadLock(cmodelRenderer->GetAnimationBoneInfos());
            auto boneInstanceWriteLock = SafeVectorScopedWriteLock(cmodelRenderer->GetAnimationBoneInstances());
            auto trackInfoReadLock = SafeVectorScopedReadLock(cmodelRenderer->GetAnimationTrackInfos());

            const std::vector<CModelRenderer::AnimationBoneInfo>& animationBoneInfos = boneInfoReadLock.Get();
            std::vector<CModelRenderer::AnimationBoneInstance>& animationBoneInstances = boneInstanceWriteLock.Get();
            const std::vector<CModelRenderer::AnimationTrackInfo>& animationTrackInfos = trackInfoReadLock.Get();

            animationInfo.modelId = loadedComplexModel.modelID;
            animationInfo.instanceId = instanceID;

            animationInfo.boneInfos.resize(animationModelInfo.numBones);
            for (u32 i = 0; i < animationModelInfo.numBones; i++)
            {
                const CModelRenderer::AnimationBoneInfo& animationBoneInfo = animationBoneInfos[animationModelInfo.boneInfoOffset + i];
                const CModelRenderer::AnimationTrackInfo& animationTrackInfo = animationTrackInfos[animationBoneInfo.translationSequenceOffset];

                AnimationModelInfo::BoneInfo boneInfo;
                boneInfo.parentBoneId = animationBoneInfo.parentBoneId;

                i32 boneKeyId = loadedComplexModel.boneKeyId[i];
                if (boneKeyId != -1)
                {
                    auto itr = animationInfo.boneKeyIdToBoneIndex.find(boneKeyId);
                    if (itr != animationInfo.boneKeyIdToBoneIndex.end())
                    {
                        DebugHandler::PrintFatal("[AnimationSystem] Attempted to AddInstance on model(%s) where multiple bones reference the same BoneKeyId", loadedComplexModel.debugName.c_str());
                    }

                    animationInfo.boneKeyIdToBoneIndex[boneKeyId] = i;
                }

                animationInfo.boneInfos[i] = boneInfo;
            }
        }

        instanceIdToAnimationModelInfo[instanceID] = animationInfo;
        result = true;
    });

    return result;
}

bool AnimationSystem::RemoveInstance(u32 instanceID)
{
    bool didRemove = false;

    _instanceIdToAnimationModelInfo.WriteLock([&](robin_hood::unordered_map<u32, AnimationModelInfo>& instanceIdToAnimationModelInfo)
    {
        auto itr = instanceIdToAnimationModelInfo.find(instanceID);
        if (itr == instanceIdToAnimationModelInfo.end())
            return;

        instanceIdToAnimationModelInfo.erase(itr);
        didRemove = true;
    });

    return didRemove;
}

bool AnimationSystem::SetBoneAnimation(u32 instanceID, i32 boneKeyId, i32 animationId)
{
    bool result = false;

    _instanceIdToAnimationModelInfo.WriteLock([&](robin_hood::unordered_map<u32, AnimationModelInfo>& instanceIdToAnimationModelInfo)
    {
        auto itr = instanceIdToAnimationModelInfo.find(instanceID);
        if (itr == instanceIdToAnimationModelInfo.end())
            return;

        AnimationModelInfo& animationModelInfo = itr->second;
        result = animationModelInfo.SetBoneAnimation(boneKeyId, animationId);
    });

    return result;
}