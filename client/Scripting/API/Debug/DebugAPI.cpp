#include "DebugAPI.h"
#include "../../ScriptAPI.h"

#include <Nai/Compiler/Compiler.h>

namespace ScriptingAPI
{
    namespace DebugAPI
    {
        void AddStringToResult(std::string& string, const char* buffer, u32 offset, u32 count)
        {
            if (count == 0)
                return;

            string += std::string(buffer, offset, count);
        }
        void PrintWithArgs(Interpreter* interpreter, const char* buffer, u32 length)
        {
            std::string result;
            result.reserve(length);

            u32 lastFormatSign = 0;
            u8 paramCounter = 2;

            for (u32 i = 0; i < length;)
            {
                char c = buffer[i];
                char cc = buffer[i + 1];

                u32 delta = i - lastFormatSign;

                if (c == '%' && cc == 'u')
                {
                    AddStringToResult(result, buffer, lastFormatSign, delta);

                    u32* num = interpreter->GetParameter<u32>(paramCounter++);
                    result += std::to_string(*num);

                    i += 2;
                    lastFormatSign = i;
                }
                if (c == '%' && cc == 'l')
                {
                    AddStringToResult(result, buffer, lastFormatSign, delta);

                    u64* num = interpreter->GetParameter<u64>(paramCounter++);
                    result += std::to_string(*num);

                    i += 2;
                    lastFormatSign = i;
                }
                if (c == '%' && cc == 'f')
                {
                    AddStringToResult(result, buffer, lastFormatSign, delta);

                    f64* num = interpreter->GetParameter<f64>(paramCounter++);
                    result += std::to_string(*num);

                    i += 2;
                    lastFormatSign = i;
                }
                else if (c == '%' && cc == 's')
                {
                    AddStringToResult(result, buffer, lastFormatSign, delta);

                    char* str = interpreter->GetParameter<char>(paramCounter++, true);
                    result += std::string(str);

                    i += 2;
                    lastFormatSign = i;
                }
                else
                {
                    i++;
                }
            }

            // Append remaining string
            if (lastFormatSign < length)
            {
                u32 delta = length - lastFormatSign;
                AddStringToResult(result, buffer, lastFormatSign, delta);
            }

            DebugHandler::Print(result);
        }
        bool PrintCallback(Interpreter* interpreter)
        {
            char* format = interpreter->GetParameter<char>(1, true);
            PrintWithArgs(interpreter, format, static_cast<u32>(strlen(format)));

            return true;
        }

        void Register(Compiler* cc, ScriptAPI* scriptAPI)
        {
            Module* module = cc->CreateNativeModule("Debug");
            scriptAPI->RegisterModule(module);

            NativeFunction nfPrint(cc, module, "Print", PrintCallback); { nfPrint.AddParamChar("string", NativeFunction::PassAs::Pointer); }
        }

        void Init(ScriptAPI* scriptAPI)
        {
            scriptAPI->AddCallback(Register);
        }
    }
}