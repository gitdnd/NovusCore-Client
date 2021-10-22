#include "DebugAPI.h"

#include "../../../ScriptAPI.h"
#include "../../../../Utils/ServiceLocator.h"
#include "../../../../ECS/Components/Singletons/DayNightSingleton.h"
#include "../../../../ECS/Components/Singletons/TimeSingleton.h"

#include <entt.hpp>
#include <Nai/Compiler/Compiler.h>

namespace ScriptingAPI
{
    namespace TimeAPI
    {
        bool GetTimeCallback(Interpreter* interpreter)
        {
            entt::registry* registry = ServiceLocator::GetGameRegistry();
            TimeSingleton& timeSingleton = registry->ctx<TimeSingleton>();

            f64 timeInSecond = timeSingleton.lifeTimeInS;
            interpreter->SetReturnValue<f64>(&timeInSecond);

            return true;
        }
        bool GetTimeInMSCallback(Interpreter* interpreter)
        {
            entt::registry* registry = ServiceLocator::GetGameRegistry();
            TimeSingleton& timeSingleton = registry->ctx<TimeSingleton>();

            f64 timeInSecond = timeSingleton.lifeTimeInMS;
            interpreter->SetReturnValue<f64>(&timeInSecond);

            return true;
        }
        bool GetDeltaTimeCallback(Interpreter* interpreter)
        {
            entt::registry* registry = ServiceLocator::GetGameRegistry();
            TimeSingleton& timeSingleton = registry->ctx<TimeSingleton>();

            f64 timeInSecond = timeSingleton.deltaTime;
            interpreter->SetReturnValue<f64>(&timeInSecond);

            return true;
        }
        bool GetTimeOfDayCallback(Interpreter* interpreter)
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
            NativeFunction nfGetTimeInMS(cc, module, "GetTimeInMS", GetTimeInMSCallback);
            NativeFunction nfGetDeltaTime(cc, module, "GetDeltaTime", GetDeltaTimeCallback);
            NativeFunction nfGetTimeOfDay(cc, module, "GetTimeOfDay", GetTimeOfDayCallback);
        }

        void Init(ScriptAPI* scriptAPI)
        {
            scriptAPI->AddCallback(Register);
        }
    }
}