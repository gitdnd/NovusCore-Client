#include "TimeAPI.h"

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

            interpreter->SetReturnValue<f32>(timeSingleton.lifeTimeInS);

            return true;
        }
        bool GetTimeInMSCallback(Interpreter* interpreter)
        {
            entt::registry* registry = ServiceLocator::GetGameRegistry();
            TimeSingleton& timeSingleton = registry->ctx<TimeSingleton>();

            interpreter->SetReturnValue<f32>(timeSingleton.lifeTimeInMS);

            return true;
        }
        bool GetDeltaTimeCallback(Interpreter* interpreter)
        {
            entt::registry* registry = ServiceLocator::GetGameRegistry();
            TimeSingleton& timeSingleton = registry->ctx<TimeSingleton>();

            interpreter->SetReturnValue<f32>(timeSingleton.deltaTime);

            return true;
        }
        bool GetTimeOfDayCallback(Interpreter* interpreter)
        {
            entt::registry* registry = ServiceLocator::GetGameRegistry();
            DayNightSingleton& dayNightSingleton = registry->ctx<DayNightSingleton>();

            interpreter->SetReturnValue<f32>(dayNightSingleton.seconds);

            return true;
        }

        void Register(Compiler* cc, ScriptAPI* scriptAPI)
        {
            Module* module = cc->CreateNativeModule("Time");
            scriptAPI->RegisterModule(module);

            NativeFunction nfGetTime(cc, module, "GetTime", GetTimeCallback); { nfGetTime.SetReturnTypeF32(NativeFunction::PassAs::Value); }
            NativeFunction nfGetTimeInMS(cc, module, "GetTimeInMS", GetTimeInMSCallback); { nfGetTimeInMS.SetReturnTypeF32(NativeFunction::PassAs::Value); }
            NativeFunction nfGetDeltaTime(cc, module, "GetDeltaTime", GetDeltaTimeCallback); { nfGetDeltaTime.SetReturnTypeF32(NativeFunction::PassAs::Value); }
            NativeFunction nfGetTimeOfDay(cc, module, "GetTimeOfDay", GetTimeOfDayCallback); { nfGetTimeOfDay.SetReturnTypeF32(NativeFunction::PassAs::Value); }
        }

        void Init(ScriptAPI* scriptAPI)
        {
            scriptAPI->AddCallback(Register);
        }
    }
}