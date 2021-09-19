#pragma once
#include <NovusTypes.h>
#include <entity/fwd.hpp>
#include <enkiTS/TaskScheduler.h>
#include <Nai/Compiler/Compiler.h>
#include "../Utils/ServiceLocator.h"

class ScriptLoader
{
public:
    bool Init(entt::registry& registry);
    bool Reload();

    bool LoadScriptDirectory(std::string& scriptFolder);

    bool LoadScriptPipeline1(std::string& scriptName, std::string& scriptPath);
    bool LoadScriptPipeline2(Module* module);
    bool LoadScriptPipeline3(Module* module);
    bool LoadScriptPipeline4(Module* module, bool didFail);

    Compiler* GetCompiler() { return &_compiler; }
    enki::TaskScheduler* GetTaskScheduler() { return &_taskScheduler; }

private:
    Compiler _compiler;
    enki::TaskScheduler _taskScheduler;
};