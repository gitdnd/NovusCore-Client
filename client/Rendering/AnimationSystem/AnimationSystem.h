#pragma once
#include <NovusTypes.h>
#include <robin_hood.h>
#include <Utils/SafeUnorderedMap.h>

#include "../CModelRenderer.h"

struct AnimationEditorInstanceData
{
public:
    u16 selectedAnimationID = 0;
    bool shouldAnimationLoop = 0;
};

struct AnimationSequenceInfo
{
    u32 id;
};

struct AnimationModelInfo
{
public:
    struct BoneInfo
    {
        i16 parentBoneId = -1;
        AnimationSequenceInfo activeSequence;
        AnimationSequenceInfo transitionSequence;
    };

public:
    AnimationModelInfo() { }

    i32 GetBoneAnimation(i32 boneKeyId);
    bool SetBoneAnimation(i32 boneKeyId, i32 animationId);
    bool UnsetBoneAnimation(i32 boneKeyId);

private:
    u32 GetBoneIndexFromBoneKeyId(i32 boneKeyId);
    i32 GetSequenceIdFromAnimationId(i32 animationId);

public:
    u32 modelId = std::numeric_limits<u32>().max();
    u32 instanceId = std::numeric_limits<u32>().max();
    AnimationEditorInstanceData editorInstanceData;

    std::vector<BoneInfo> boneInfos;
    robin_hood::unordered_map<i32, u32> boneKeyIdToBoneIndex;
};

class AnimationSystem
{
public:

public:
    bool AddInstance(u32 instanceID, const CModelRenderer::LoadedComplexModel& loadedComplexModel);
    bool RemoveInstance(u32 instanceID);

    bool SetBoneAnimation(u32 instanceID, i32 boneKeyId, i32 animationId);

public:
    SafeUnorderedMap<u32, AnimationModelInfo>& GetInstanceToAnimationModelInfo() { return _instanceIdToAnimationModelInfo; }

private:
    SafeUnorderedMap<u32, AnimationModelInfo> _instanceIdToAnimationModelInfo;
};