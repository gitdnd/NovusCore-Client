#include "ScriptLoader.h"
#include "ScriptEngine.h"
#include "ScriptAPI.h"
#include "../Rendering/Camera.h"

#include <Utils/Timer.h>
#include <Utils/FileReader.h>
#include "../Utils/ServiceLocator.h"

#include <entt.hpp>
#include <CVar/CVarSystem.h>

#include "../Gameplay/GameConsole/GameConsole.h"
#include "../ECS/Components/Singletons/ScriptSingleton.h"
#include "../ECS/Components/Singletons/DataStorageSingleton.h"
#include "../ECS/Components/Singletons/SceneManagerSingleton.h"

#include <execution>
#include <filesystem>
namespace fs = std::filesystem;

AutoCVar_String CVAR_ScriptPath("script.path", "path to the scripting folder", "./Data/scripts");

bool ScriptLoader::Init(entt::registry& registry)
{
    registry.set<DataStorageSingleton>();
    registry.set<SceneManagerSingleton>();
    registry.set<ScriptSingleton>();

    _taskScheduler.Initialize(4);
    
    return Reload();
}

bool ScriptLoader::Reload()
{
    _compiler.Init();

    std::string scriptPath = CVAR_ScriptPath.Get();
    return LoadScriptDirectory(scriptPath);
}

bool ScriptLoader::LoadScriptDirectory(std::string& scriptFolder)
{
    GameConsole* gameConsole = ServiceLocator::GetGameConsole();
    if (scriptFolder == "")
    {
        gameConsole->PrintError("ScriptLoader : No ScriptFolder was specified");
        return false;
    }

    fs::path absolutePath = fs::absolute(scriptFolder);
    if (!fs::exists(absolutePath))
    {
        fs::create_directory(absolutePath);
    }

    Timer timer;
    bool didFail = false;

    std::vector<fs::path> paths;
    std::filesystem::recursive_directory_iterator dirpos{ absolutePath };
    std::copy(begin(dirpos), end(dirpos), std::back_inserter(paths));
    u32 numPaths = static_cast<u32>(paths.size());

    /*
        Compiler Pipepline

        # Pipeline 1
        - Lexer
        - ExportPass (All modules must pass here before continuing)

        # Pipeline 2
        - Importing
        - Parser (All modules must pass here before continuing)

        # Pipeline 3
        - Typer
        - Bytecode (All modules must pass here before continuing)

        # Pipeline 4
        - If no errors, add "Main" functions to execution queue
        - Buffer Cleanup
    */

    // LoadScriptPipeline1
    {
        enki::TaskSet pipeline1(numPaths, [this, &paths, &didFail](enki::TaskSetPartition range, uint32_t threadNum)
        {
            for (u32 i = range.start; i < range.end; i++)
            {
                const fs::path& path = paths[i];

                if (path.extension() != ".nai")
                    continue;

                didFail |= !LoadScriptPipeline1(path.filename().string(), path.string());
            }
        });

        _taskScheduler.AddTaskSetToPipe(&pipeline1);
        _taskScheduler.WaitforTask(&pipeline1);
    }

    u32 numModulesFromNai = _compiler.GetModuleCount();

    // Script API is registered here
    {
        ServiceLocator::GetScriptAPI()->RegisterAPI(&_compiler);
    }

    u32 numModulesTotal = _compiler.GetModuleCount();

    // LoadScriptPipeline2
    if (!didFail)
    {
        enki::TaskSet pipeline2(numModulesFromNai, [this, &didFail](enki::TaskSetPartition range, uint32_t threadNum)
        {
            for (u32 i = range.start; i < range.end; i++)
            {
                Module* module = _compiler.GetModuleByIndex(i);
                didFail |= !LoadScriptPipeline2(module);
            }
        });

        _taskScheduler.AddTaskSetToPipe(&pipeline2);
        _taskScheduler.WaitforTask(&pipeline2);
    }

    // LoadScriptPipeline3
    if (!didFail)
    {
        enki::TaskSet pipeline3(numModulesTotal, [this](enki::TaskSetPartition range, uint32_t threadNum)
        {
            for (u32 i = range.start; i < range.end; i++)
            {
                Module* module = _compiler.GetModuleByIndex(i);
                LoadScriptPipeline3(module);
            }
        });

        _taskScheduler.AddTaskSetToPipe(&pipeline3);
        _taskScheduler.WaitforTask(&pipeline3);
    }

    // LoadScriptPipeline4
    if (!didFail)
    {
        enki::TaskSet pipeline4(numModulesFromNai, [this, &didFail](enki::TaskSetPartition range, uint32_t threadNum)
        {
            for (u32 i = range.start; i < range.end; i++)
            {
                Module* module = _compiler.GetModuleByIndex(i);
                LoadScriptPipeline4(module);
            }
        });

        _taskScheduler.AddTaskSetToPipe(&pipeline4);
        _taskScheduler.WaitforTask(&pipeline4);
    }

    if (didFail)
    {
        gameConsole->PrintError("ScriptLoader : Please correct the errors above");
    }

    f32 msTimeTaken = timer.GetLifeTime() * 1000;
    gameConsole->PrintSuccess("ScriptLoader : Loaded %u scripts in %.4f ms", numModulesFromNai, msTimeTaken);

    return !didFail;
}

bool ScriptLoader::LoadScriptPipeline1(std::string& scriptName, std::string& scriptPath)
{
    GameConsole* gameConsole = ServiceLocator::GetGameConsole();

    FileReader reader(scriptPath, scriptPath);

    if (!reader.Open())
    {
        gameConsole->PrintError("ScriptLoader : Failed to read script (%s)", scriptPath.c_str());
        return false;
    }

    Bytebuffer* buffer = new Bytebuffer(nullptr, reader.Length());
    reader.Read(buffer, buffer->size);

    Module* module = _compiler.CreateModule(scriptName, buffer);

    if (!Lexer::Process(module))
        return false;

    if (!ExportPass::Process(&_compiler, module))
        return false;

    return true;
}

bool ScriptLoader::LoadScriptPipeline2(Module* module)
{
    if (!ImportPass::Process(&_compiler, module))
        return false;

    if (!Parser::Process(&_compiler, module))
        return false;

    return true;
}

bool ScriptLoader::LoadScriptPipeline3(Module* module)
{
    if (!Typer::Process(&_compiler, module))
        return false;

    return true;
}

bool ScriptLoader::LoadScriptPipeline4(Module* module)
{
    if (!Bytecode::Process(&_compiler, module))
        return false;

    u32 mainHash = "main"_djb2;

    auto itr = module->bytecodeInfo.functionHashToDeclaration.find(mainHash);
    if (itr != module->bytecodeInfo.functionHashToDeclaration.end())
    {
        // Add Main to be executed later
        {
            ScriptEngine* scriptEngine = ServiceLocator::GetScriptEngine();

            ScriptExecutionInfo scriptExecutionInfo(module, mainHash);
            scriptEngine->AddExecution(scriptExecutionInfo);
        }
    }

    //delete module->buffer;
    return true;
}