#pragma once
#include <NovusTypes.h>
#include <Utils/ConcurrentQueue.h>
#include <enkiTS/TaskScheduler.h>

struct Compiler;
struct Module;

struct ScriptExecutionInfo
{
    ScriptExecutionInfo() { }
    ScriptExecutionInfo(Module* inModule, u32 inFnHash) : module(inModule), fnHash(inFnHash) { }

    Module* module = nullptr;
    u32 fnHash = 0;
};

class Interpreter;
class ScriptEngine
{
public:
    bool Init(Compiler* cc);
    void Execute();

    void AddExecution(const ScriptExecutionInfo& executionInfo);

private:
    bool _isInitialized = false;
    bool _canExecute = false;
    std::atomic<i32> _numTasks = 0;

    enki::TaskScheduler _taskScheduler;
    std::vector<Interpreter*> _interpreters;
    std::vector<ScriptExecutionInfo> _executionInfosBulk;
    moodycamel::ConcurrentQueue<ScriptExecutionInfo> _executionInfos;
};