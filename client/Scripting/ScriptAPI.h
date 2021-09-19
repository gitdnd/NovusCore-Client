#pragma once
#include <NovusTypes.h>
#include <vector>
#include <robin_hood.h>

struct Compiler;
struct Module;
class ScriptAPI;
typedef void ScriptAPICallbackFunc(Compiler* cc, ScriptAPI* scriptAPI);

class ScriptAPI
{
public:
    ScriptAPI() { }

    void Init();
    void RegisterAPI(Compiler* cc);

    bool RegisterModule(Module* module);
    Module* GetModule(u32 moduleNameHash);

    void AddCallback(ScriptAPICallbackFunc* callback);

private:
    std::vector<ScriptAPICallbackFunc*> _registerAPICallbacks;
    std::vector<Module*> _modules;

    robin_hood::unordered_map<u32, u32> _moduleNameHashToModuleIndex;
};