#include "DebugAPI.h"

#include "../../ScriptAPI.h"
#include <Nai/Compiler/Compiler.h>

namespace ScriptingAPI
{
    namespace MathAPI
    {
        bool Vec2Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(sizeof(vec2), returnAddress))
            {
                vec2 result = vec2(*interpreter->ReadParameter<f32>(), *interpreter->ReadParameter<f32>());
                interpreter->SetValueAtAddress(returnAddress, result);
            }

            interpreter->SetReturnValue<u64>(returnAddress);
            return true;
        }
        bool Vec3Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(sizeof(vec3), returnAddress))
            {
                vec3 result = vec3(*interpreter->ReadParameter<f32>(), *interpreter->ReadParameter<f32>(), *interpreter->ReadParameter<f32>());
                interpreter->SetValueAtAddress(returnAddress, result);
            }

            interpreter->SetReturnValue<u64>(returnAddress);
            return true;
        }
        bool ColorCallback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(sizeof(Color), returnAddress))
            {
                Color result = Color(*interpreter->ReadParameter<f32>(), *interpreter->ReadParameter<f32>(), *interpreter->ReadParameter<f32>(), *interpreter->ReadParameter<f32>());
                interpreter->SetValueAtAddress(returnAddress, result);
            }

            interpreter->SetReturnValue<u64>(returnAddress);
            return true;
        }

        bool AddVec2Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(sizeof(vec2), returnAddress))
            {
                vec2* vecA = interpreter->ReadParameter<vec2>(true);
                vec2* vecB = interpreter->ReadParameter<vec2>(true);

                vec2 result = *vecA + *vecB;
                interpreter->SetValueAtAddress<vec2>(returnAddress, result);
            }

            interpreter->SetReturnValue<u64>(returnAddress);
            return true;
        }
        bool AddVec3Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(sizeof(vec3), returnAddress))
            {
                vec3* vecA = interpreter->ReadParameter<vec3>(true);
                vec3* vecB = interpreter->ReadParameter<vec3>(true);

                vec3 result = *vecA + *vecB;
                interpreter->SetValueAtAddress<vec3>(returnAddress, result);
            }

            interpreter->SetReturnValue<u64>(returnAddress);
            return true;
        }
        bool AddColorCallback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(sizeof(Color), returnAddress))
            {
                Color* colorA = interpreter->ReadParameter<Color>(true);
                Color* colorB = interpreter->ReadParameter<Color>(true);

                Color result = *colorA + *colorB;
                interpreter->SetValueAtAddress<Color>(returnAddress, result);
            }

            interpreter->SetReturnValue<u64>(returnAddress);
            return true;
        }

        bool SubVec2Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(sizeof(vec2), returnAddress))
            {
                vec2* vecA = interpreter->ReadParameter<vec2>(true);
                vec2* vecB = interpreter->ReadParameter<vec2>(true);

                vec2 result = *vecA - *vecB;
                interpreter->SetValueAtAddress<vec2>(returnAddress, result);
            }

            interpreter->SetReturnValue<u64>(returnAddress);
            return true;
        }
        bool SubVec3Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(sizeof(vec3), returnAddress))
            {
                vec3* vecA = interpreter->ReadParameter<vec3>(true);
                vec3* vecB = interpreter->ReadParameter<vec3>(true);

                vec3 result = *vecA - *vecB;
                interpreter->SetValueAtAddress<vec3>(returnAddress, result);
            }

            interpreter->SetReturnValue<u64>(returnAddress);
            return true;
        }
        bool SubColorCallback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(sizeof(Color), returnAddress))
            {
                Color* colorA = interpreter->ReadParameter<Color>(true);
                Color* colorB = interpreter->ReadParameter<Color>(true);

                Color result = *colorA - *colorB;
                interpreter->SetValueAtAddress<Color>(returnAddress, result);
            }

            interpreter->SetReturnValue<u64>(returnAddress);
            return true;
        }

        bool MulVec2Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(sizeof(vec2), returnAddress))
            {
                vec2* vecA = interpreter->ReadParameter<vec2>(true);
                vec2* vecB = interpreter->ReadParameter<vec2>(true);

                vec2 result = *vecA * *vecB;
                interpreter->SetValueAtAddress<vec2>(returnAddress, result);
            }

            interpreter->SetReturnValue<u64>(returnAddress);
            return true;
        }
        bool MulVec3Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(sizeof(vec3), returnAddress))
            {
                vec3* vecA = interpreter->ReadParameter<vec3>(true);
                vec3* vecB = interpreter->ReadParameter<vec3>(true);

                vec3 result = *vecA * *vecB;
                interpreter->SetValueAtAddress<vec3>(returnAddress, result);
            }

            interpreter->SetReturnValue<u64>(returnAddress);
            return true;
        }
        bool MulColorCallback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(sizeof(Color), returnAddress))
            {
                Color* colorA = interpreter->ReadParameter<Color>(true);
                Color* colorB = interpreter->ReadParameter<Color>(true);

                Color result = *colorA * *colorB;
                interpreter->SetValueAtAddress<Color>(returnAddress, result);
            }

            interpreter->SetReturnValue<u64>(returnAddress);
            return true;
        }

        bool DivVec2Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(sizeof(vec2), returnAddress))
            {
                vec2* vecA = interpreter->ReadParameter<vec2>(true);
                vec2* vecB = interpreter->ReadParameter<vec2>(true);

                vec2 result = *vecA / *vecB;
                interpreter->SetValueAtAddress<vec2>(returnAddress, result);
            }

            interpreter->SetReturnValue<u64>(returnAddress);
            return true;
        }
        bool DivVec3Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(sizeof(vec3), returnAddress))
            {
                vec3* vecA = interpreter->ReadParameter<vec3>(true);
                vec3* vecB = interpreter->ReadParameter<vec3>(true);

                vec3 result = *vecA / *vecB;
                interpreter->SetValueAtAddress<vec3>(returnAddress, result);
            }

            interpreter->SetReturnValue<u64>(returnAddress);
            return true;
        }
        bool DivColorCallback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(sizeof(Color), returnAddress))
            {
                Color* colorA = interpreter->ReadParameter<Color>(true);
                Color* colorB = interpreter->ReadParameter<Color>(true);

                Color result = *colorA / *colorB;
                interpreter->SetValueAtAddress<Color>(returnAddress, result);
            }

            interpreter->SetReturnValue<u64>(returnAddress);
            return true;
        }

        void RegisterVec2Functions(Compiler* cc, Module* module)
        {
            NativeFunction nfVec2(cc, module, "Vec2", Vec2Callback); { nfVec2.AddParamF32("x", NativeFunction::PassAs::Value); nfVec2.AddParamF32("y", NativeFunction::PassAs::Value); nfVec2.SetReturnTypeUnknown("Vec2", NativeFunction::PassAs::Pointer); }

            NativeFunction nfAddVec2(cc, module, "AddVec2", AddVec2Callback); { nfAddVec2.AddParamUnknown("Vec2", "lhs", NativeFunction::PassAs::Pointer); nfAddVec2.AddParamUnknown("Vec2", "rhs", NativeFunction::PassAs::Pointer); nfAddVec2.SetReturnTypeUnknown("Vec2", NativeFunction::PassAs::Pointer); }
            NativeFunction nfSubVec2(cc, module, "SubVec2", SubVec2Callback); { nfSubVec2.AddParamUnknown("Vec2", "lhs", NativeFunction::PassAs::Pointer); nfSubVec2.AddParamUnknown("Vec2", "rhs", NativeFunction::PassAs::Pointer); nfSubVec2.SetReturnTypeUnknown("Vec2", NativeFunction::PassAs::Pointer); }
            NativeFunction nfMulVec2(cc, module, "MulVec2", MulVec2Callback); { nfMulVec2.AddParamUnknown("Vec2", "lhs", NativeFunction::PassAs::Pointer); nfMulVec2.AddParamUnknown("Vec2", "rhs", NativeFunction::PassAs::Pointer); nfMulVec2.SetReturnTypeUnknown("Vec2", NativeFunction::PassAs::Pointer); }
            NativeFunction nfDivVec2(cc, module, "DivVec2", DivVec2Callback); { nfDivVec2.AddParamUnknown("Vec2", "lhs", NativeFunction::PassAs::Pointer); nfDivVec2.AddParamUnknown("Vec2", "rhs", NativeFunction::PassAs::Pointer); nfDivVec2.SetReturnTypeUnknown("Vec2", NativeFunction::PassAs::Pointer); }
        }
        void RegisterVec3Functions(Compiler* cc, Module* module)
        {
            NativeFunction nfVec3(cc, module, "Vec3", Vec3Callback); { nfVec3.AddParamF32("x", NativeFunction::PassAs::Value); nfVec3.AddParamF32("y", NativeFunction::PassAs::Value); nfVec3.AddParamF32("z", NativeFunction::PassAs::Value); nfVec3.SetReturnTypeUnknown("Vec3", NativeFunction::PassAs::Pointer);  }

            NativeFunction nfAddVec3(cc, module, "AddVec3", AddVec3Callback); { nfAddVec3.AddParamUnknown("Vec3", "lhs", NativeFunction::PassAs::Pointer); nfAddVec3.AddParamUnknown("Vec3", "rhs", NativeFunction::PassAs::Pointer); nfAddVec3.SetReturnTypeUnknown("Vec3", NativeFunction::PassAs::Pointer);  }
            NativeFunction nfSubVec3(cc, module, "SubVec3", SubVec3Callback); { nfSubVec3.AddParamUnknown("Vec3", "lhs", NativeFunction::PassAs::Pointer); nfSubVec3.AddParamUnknown("Vec3", "rhs", NativeFunction::PassAs::Pointer); nfSubVec3.SetReturnTypeUnknown("Vec3", NativeFunction::PassAs::Pointer);  }
            NativeFunction nfMulVec3(cc, module, "MulVec3", MulVec3Callback); { nfMulVec3.AddParamUnknown("Vec3", "lhs", NativeFunction::PassAs::Pointer); nfMulVec3.AddParamUnknown("Vec3", "rhs", NativeFunction::PassAs::Pointer); nfMulVec3.SetReturnTypeUnknown("Vec3", NativeFunction::PassAs::Pointer);  }
            NativeFunction nfDivVec3(cc, module, "DivVec3", DivVec3Callback); { nfDivVec3.AddParamUnknown("Vec3", "lhs", NativeFunction::PassAs::Pointer); nfDivVec3.AddParamUnknown("Vec3", "rhs", NativeFunction::PassAs::Pointer); nfDivVec3.SetReturnTypeUnknown("Vec3", NativeFunction::PassAs::Pointer);  }
        }
        void RegisterColorFunctions(Compiler* cc, Module* module)
        {
            NativeFunction nfColor(cc, module, "Color", ColorCallback); { nfColor.AddParamF32("r", NativeFunction::PassAs::Value); nfColor.AddParamF32("g", NativeFunction::PassAs::Value); nfColor.AddParamF32("b", NativeFunction::PassAs::Value); nfColor.AddParamF32("a", NativeFunction::PassAs::Value); nfColor.SetReturnTypeUnknown("Color", NativeFunction::PassAs::Pointer);  }

            NativeFunction nfAddColor(cc, module, "AddColor", AddColorCallback); { nfAddColor.AddParamUnknown("Color", "lhs", NativeFunction::PassAs::Pointer); nfAddColor.AddParamUnknown("Color", "rhs", NativeFunction::PassAs::Pointer); nfAddColor.SetReturnTypeUnknown("Color", NativeFunction::PassAs::Pointer); }
            NativeFunction nfSubColor(cc, module, "SubColor", SubColorCallback); { nfSubColor.AddParamUnknown("ColorA", "lhs", NativeFunction::PassAs::Pointer); nfSubColor.AddParamUnknown("Color", "rhs", NativeFunction::PassAs::Pointer); nfSubColor.SetReturnTypeUnknown("Color", NativeFunction::PassAs::Pointer); }
            NativeFunction nfMulColor(cc, module, "MulColor", MulColorCallback); { nfMulColor.AddParamUnknown("Color", "lhs", NativeFunction::PassAs::Pointer); nfMulColor.AddParamUnknown("Color", "rhs", NativeFunction::PassAs::Pointer); nfMulColor.SetReturnTypeUnknown("Color", NativeFunction::PassAs::Pointer); }
            NativeFunction nfDivColor(cc, module, "DivColor", DivColorCallback); { nfDivColor.AddParamUnknown("Color", "lhs", NativeFunction::PassAs::Pointer); nfDivColor.AddParamUnknown("Color", "rhs", NativeFunction::PassAs::Pointer); nfDivColor.SetReturnTypeUnknown("Color", NativeFunction::PassAs::Pointer); }
        }

        void Register(Compiler* cc, ScriptAPI* scriptAPI)
        {
            Module* module = cc->CreateNativeModule("Math");
            scriptAPI->RegisterModule(module);

            RegisterVec2Functions(cc, module);
            RegisterVec3Functions(cc, module);
            RegisterColorFunctions(cc, module);
        }

        void Init(ScriptAPI* scriptAPI)
        {
            scriptAPI->AddCallback(Register);
        }
    }
}