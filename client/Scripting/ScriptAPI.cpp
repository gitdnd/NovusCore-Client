#include "ScriptAPI.h"

#include "API/Debug/DebugAPI.h"
#include "API/Gameplay/Time/TimeAPI.h"

#include <Nai/Compiler/Module.h>
#include <Nai/Compiler/Compiler.h>

void ScriptAPI::Init()
{
    // This Init function is responsible for adding API Callbacks
    ScriptingAPI::DebugAPI::Init(this);
    ScriptingAPI::TimeAPI::Init(this);
}

void ScriptAPI::RegisterAPI(Compiler* cc)
{
    for (i32 i = 0; i < _registerAPICallbacks.size(); i++)
    {
        _registerAPICallbacks[i](cc, this);
    }
}

bool ScriptAPI::RegisterModule(Module* module)
{
    u32 moduleHash = module->debugNameHash.hash;

    for (i32 i = 0; i < _modules.size(); i++)
    {
        Module* existingModule = _modules[i];
        if (existingModule->debugNameHash.hash == moduleHash)
            return false;
    }

    _modules.push_back(module);
    _moduleNameHashToModuleIndex[moduleHash] = static_cast<u32>(_modules.size()) - 1u;
    return true;
}

Module* ScriptAPI::GetModule(u32 moduleNameHash)
{
    auto itr = _moduleNameHashToModuleIndex.find(moduleNameHash);
    if (itr == _moduleNameHashToModuleIndex.end())
    {
        DebugHandler::PrintFatal("ScriptAPI : Tried to get a module that has not been registered");
    }

    u32 registeredModuleIndex = itr->second;
    return _modules[registeredModuleIndex];
}

void ScriptAPI::AddCallback(ScriptAPICallbackFunc* callback)
{
    _registerAPICallbacks.push_back(callback);
}
