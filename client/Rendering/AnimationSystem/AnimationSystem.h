#pragma once
#include <NovusTypes.h>

#include <robin_hood.h>
#include <Utils/SafeUnorderedMap.h>

class AnimationSystem
{
public:
    struct AnimationInstanceData
    {
        u16 editorSelectedAnimationID = 0;
        bool editorShouldAnimationLoop = 0;

        std::vector<u16> activeAnimationIDs;
    };

public:
    bool AddInstance(u32 instanceID, const AnimationInstanceData& animationInstanceData);
    bool RemoveInstance(u32 instanceID);

    bool GetAnimationInstanceData(u32 instanceID, AnimationInstanceData*& out);

    // Helper Functions
public:
    bool TryPlayAnimationID(u32 instanceID, u16 animationID, bool play, bool loop = false);
    void TryStopAllAnimations(u32 instanceID);

private:
    SafeUnorderedMap<u32, AnimationInstanceData> _instanceIDToAnimationInstanceData;
};