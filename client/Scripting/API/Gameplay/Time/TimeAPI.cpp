#include "DebugAPI.h"

#include "../../../ScriptAPI.h"
#include "../../../../Utils/ServiceLocator.h"
#include "../../../../ECS/Components/Singletons/DayNightSingleton.h"

#include <entt.hpp>
#include <Nai/Compiler/Compiler.h>

namespace ScriptingAPI
{
    namespace TimeAPI
    {
        bool GetTimeCallback(Interpreter* interpreter)
        {
            entt::registry* registry = ServiceLocator::GetGameRegistry();

            DayNightSingleton& dayNightSingleton = registry->ctx<DayNightSingleton>();

            f64 timeInSecond = dayNightSingleton.seconds;
            interpreter->SetReturnValue<f64>(&timeInSecond);

            return true;
        }

        void Register(Compiler* cc, ScriptAPI* scriptAPI)
        {
            Module* module = cc->CreateNativeModule("Time");
            scriptAPI->RegisterModule(module);

            NativeFunction nfGetTime(cc, module, "GetTime", GetTimeCallback);
        }

        void Init(ScriptAPI* scriptAPI)
        {
            scriptAPI->AddCallback(Register);
        }
    }
}