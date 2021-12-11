#pragma once
#include <NovusTypes.h>
#include <limits>

enum ModelType
{
    GameObject,
    Creature
};

// TODO : Find a better alternative to using empty structs as components to get around (GPUVector issues) where we create a model the same frame as we potentially mark it as dirty
//        The issue is some models reuse instance data which means it is a little complicated to solve at this time.
struct ModelCreatedThisFrame { };
struct ModelIsReusedInstance { };

struct ModelDisplayInfo
{
    ModelDisplayInfo(ModelType inModelType, u32 inDisplayID)
    {
        modelType = inModelType;
        displayID = inDisplayID;
        instanceID = std::numeric_limits<u32>().max();
    }

    ModelType modelType;
    u32 displayID;
    u32 instanceID;
};