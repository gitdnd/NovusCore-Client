#include "ScriptEngine.h"
#include <Nai/Compiler/Module.h>
#include <Nai/Compiler/Compiler.h>
#include <Nai/Compiler/Backend/Bytecode/Interpreter.h>

#include "CVar/CVarSystem.h"

#include <execution>

AutoCVar_Int CVAR_ScriptEngineExecutionThreads("scriptEngine.executionThreads", "number of threads used to execute scripts", 4);
AutoCVar_Int CVAR_ScriptEngineExecutionThreadsMin("scriptEngine.executionThreadsMin", "number of minimum threads used to execute scripts", 1);
AutoCVar_Int CVAR_ScriptEngineExecutionThreadsMax("scriptEngine.executionThreadsMax", "number of maximum threads used to execute scripts", 16);
AutoCVar_Int CVAR_ScriptEngineStackSize("scriptEngine.stackSizeMB", "stack size for each thread when executing scripts", 1);
AutoCVar_Int CVAR_ScriptEngineHeapSize("scriptEngine.heapSizeMB", "heap size for each thread when executing scripts", 4);

bool ScriptEngine::Init(Compiler* cc)
{
    _isInitialized = false;

    for (u32 i = 0; i < _interpreters.size(); i++)
    {
        Interpreter* interpreter = _interpreters[i];
        delete interpreter;
    }

    _interpreters.clear();

    i32 numScriptThreads = CVAR_ScriptEngineExecutionThreads.Get();
    i32 numMinScriptThreads = CVAR_ScriptEngineExecutionThreadsMin.Get();
    i32 numMaxScriptThreads = CVAR_ScriptEngineExecutionThreadsMax.Get();

    if (numScriptThreads < numMinScriptThreads)
    {
        DebugHandler::PrintError("ScriptEngine : Failed to initialize, numInterpreters(%i) is less than the specified minimum %i", numScriptThreads, numMinScriptThreads);
        _isInitialized = false;
        return false;
    }
    else if (numScriptThreads > numMaxScriptThreads)
    {
        DebugHandler::PrintError("ScriptEngine : Failed to initialize, numInterpreters(%i) is greater than the specified maximum %i", numScriptThreads, numMaxScriptThreads);
        _isInitialized = false;
        return false;
    }

    _interpreters.resize(numScriptThreads);
    _taskScheduler.Initialize(numScriptThreads);

    i32 stackSize = CVAR_ScriptEngineStackSize.Get() * 1024 * 1024;
    i32 heapSize = CVAR_ScriptEngineHeapSize.Get() * 1024 * 1024;

    if (stackSize <= 0)
    {
        DebugHandler::PrintError("ScriptEngine : Failed to initialize, stackSize(%i) is less than the minimum value 1 MB", stackSize);
        _isInitialized = false;
        return false;
    }

    if (heapSize <= 0)
    {
        DebugHandler::PrintError("ScriptEngine : Failed to initialize, heapSize(%i) is less than the minimum value 1 MB", heapSize);
        _isInitialized = false;
        return false;
    }

    for (i32 i = 0; i < numScriptThreads; i++)
    {
        // Setup Interpret
        Interpreter* interpreter = new Interpreter();
        interpreter->Init(cc, stackSize, heapSize);

        _interpreters[i] = interpreter;
    }

    // Empty Previous ExecutionInfo Queue
    {
        _numTasks = 0;

        ScriptExecutionInfo scriptExecutionInfo;
        while (_executionInfos.try_dequeue(scriptExecutionInfo)) {}
    }

    _isInitialized = true;
    return true;
}

void ScriptEngine::Execute()
{
    i32 numTasks = _numTasks;

    if (numTasks)
    {
        _executionInfosBulk.resize(numTasks);
        if (_executionInfos.try_dequeue_bulk(_executionInfosBulk.begin(), numTasks))
        {
            enki::TaskSet task(numTasks, [this](enki::TaskSetPartition range, uint32_t threadNum)
            {
                Interpreter* interpreter = _interpreters[threadNum];

                for (u32 i = range.start; i < range.end; i++)
                {
                    ScriptExecutionInfo& scriptExecutionInfo = _executionInfosBulk[i];
                    interpreter->Prepare();
                    interpreter->Interpret(scriptExecutionInfo.module, scriptExecutionInfo.fnHash);
                }
            });

            _taskScheduler.AddTaskSetToPipe(&task);
            _taskScheduler.WaitforTask(&task);

            _numTasks -= numTasks;

            DebugHandler::PrintSuccess("ScriptEngine Ran %u Tasks", numTasks);
        }
    }
}

void ScriptEngine::AddExecution(const ScriptExecutionInfo& executionInfo)
{
    if (!_isInitialized)
        return;

    _executionInfos.enqueue(executionInfo);
    _numTasks++;
}