#pragma once
#include <NovusTypes.h>

struct CModelInfo
{
    CModelInfo() : instanceID(-1), currentChunkID(-1), isStaticModel(false) { }
    CModelInfo(u32 inInstanceID, bool inIsStaticModel) : instanceID(inInstanceID), currentChunkID(-1), isStaticModel(inIsStaticModel) { }

    u32 instanceID;
    u32 currentChunkID;
    bool isStaticModel;
};