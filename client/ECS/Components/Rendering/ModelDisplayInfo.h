#pragma once
#include <NovusTypes.h>

enum ModelType
{
    GameObject,
    Creature
};

struct ModelDisplayInfo
{
    ModelDisplayInfo(ModelType inModelType, u32 inDisplayID)
    {
        modelType = inModelType;
        displayID = inDisplayID;
    }

    ModelType modelType;
    u32 displayID;
    u32 instanceID;
};