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
            if (interpreter->AllocateHeap(16, returnAddress))
            {
                u8* memory = interpreter->GetMemory();

                // Set Return Vec2
                *reinterpret_cast<f64*>(&memory[returnAddress]) = *interpreter->GetParameter<f64>(1);
                *reinterpret_cast<f64*>(&memory[returnAddress + 8]) = *interpreter->GetParameter<f64>(2);
            }

            interpreter->SetReturnValue<u64>(&returnAddress);
            return true;
        }
        bool Vec3Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(24, returnAddress))
            {
                u8* memory = interpreter->GetMemory();

                // Set Return Vec3
                *reinterpret_cast<f64*>(&memory[returnAddress]) = *interpreter->GetParameter<f64>(1);
                *reinterpret_cast<f64*>(&memory[returnAddress + 8]) = *interpreter->GetParameter<f64>(2);
                *reinterpret_cast<f64*>(&memory[returnAddress + 16]) = *interpreter->GetParameter<f64>(3);
            }

            interpreter->SetReturnValue<u64>(&returnAddress);
            return true;
        }
        bool ColorCallback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(24, returnAddress))
            {
                u8* memory = interpreter->GetMemory();

                // Set Return Color
                *reinterpret_cast<f64*>(&memory[returnAddress]) = *interpreter->GetParameter<f64>(1);
                *reinterpret_cast<f64*>(&memory[returnAddress + 8]) = *interpreter->GetParameter<f64>(2);
                *reinterpret_cast<f64*>(&memory[returnAddress + 16]) = *interpreter->GetParameter<f64>(3);
                *reinterpret_cast<f64*>(&memory[returnAddress + 24]) = *interpreter->GetParameter<f64>(4);
            }

            interpreter->SetReturnValue<u64>(&returnAddress);
            return true;
        }

        bool AddVec2Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(16, returnAddress))
            {
                u8* memory = interpreter->GetMemory();

                u64 address1 = *interpreter->GetParameter<u64>(1);
                u64 address2 = *interpreter->GetParameter<u64>(2);

                // Get A Vec
                f64 x1 = *reinterpret_cast<f64*>(&memory[address1]);
                f64 y1 = *reinterpret_cast<f64*>(&memory[address1 + 8]);

                // Get B Vec
                f64 x2 = *reinterpret_cast<f64*>(&memory[address2]);
                f64 y2 = *reinterpret_cast<f64*>(&memory[address2 + 8]);

                // Get Return Vec
                f64* x3 = reinterpret_cast<f64*>(&memory[returnAddress]);
                f64* y3 = reinterpret_cast<f64*>(&memory[returnAddress + 8]);

                // Set Return Values
                *x3 = x1 + x2;
                *y3 = y1 + y2;
            }

            interpreter->SetReturnValue<u64>(&returnAddress);
            return true;
        }
        bool AddVec3Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(24, returnAddress))
            {
                u8* memory = interpreter->GetMemory();

                u64 address1 = *interpreter->GetParameter<u64>(1);
                u64 address2 = *interpreter->GetParameter<u64>(2);

                // Get A Vec
                f64 x1 = *reinterpret_cast<f64*>(&memory[address1]);
                f64 y1 = *reinterpret_cast<f64*>(&memory[address1 + 8]);
                f64 z1 = *reinterpret_cast<f64*>(&memory[address1 + 16]);

                // Get B Vec
                f64 x2 = *reinterpret_cast<f64*>(&memory[address2]);
                f64 y2 = *reinterpret_cast<f64*>(&memory[address2 + 8]);
                f64 z2 = *reinterpret_cast<f64*>(&memory[address2 + 16]);

                // Get Return Vec
                f64* x3 = reinterpret_cast<f64*>(&memory[returnAddress]);
                f64* y3 = reinterpret_cast<f64*>(&memory[returnAddress + 8]);
                f64* z3 = reinterpret_cast<f64*>(&memory[returnAddress + 16]);

                // Set Return Values
                *x3 = x1 + x2;
                *y3 = y1 + y2;
                *z3 = z1 + z2;
            }

            interpreter->SetReturnValue<u64>(&returnAddress);
            return true;
        }
        bool AddColorCallback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(32, returnAddress))
            {
                u8* memory = interpreter->GetMemory();

                u64 address1 = *interpreter->GetParameter<u64>(1);
                u64 address2 = *interpreter->GetParameter<u64>(2);

                // Get A Vec
                f64 r1 = *reinterpret_cast<f64*>(&memory[address1]);
                f64 g1 = *reinterpret_cast<f64*>(&memory[address1 + 8]);
                f64 b1 = *reinterpret_cast<f64*>(&memory[address1 + 16]);
                f64 a1 = *reinterpret_cast<f64*>(&memory[address1 + 24]);

                // Get B Vec
                f64 r2 = *reinterpret_cast<f64*>(&memory[address2]);
                f64 g2 = *reinterpret_cast<f64*>(&memory[address2 + 8]);
                f64 b2 = *reinterpret_cast<f64*>(&memory[address2 + 16]);
                f64 a2 = *reinterpret_cast<f64*>(&memory[address2 + 24]);

                // Get Return Vec
                f64* r3 = reinterpret_cast<f64*>(&memory[returnAddress]);
                f64* g3 = reinterpret_cast<f64*>(&memory[returnAddress + 8]);
                f64* b3 = reinterpret_cast<f64*>(&memory[returnAddress + 16]);
                f64* a3 = reinterpret_cast<f64*>(&memory[returnAddress + 24]);

                // Set Return Values
                *r3 = r1 + r2;
                *g3 = g1 + g2;
                *b3 = b1 + b2;
                *a3 = a1 + a2;
            }

            interpreter->SetReturnValue<u64>(&returnAddress);
            return true;
        }

        bool SubVec2Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(16, returnAddress))
            {
                u8* memory = interpreter->GetMemory();

                u64 address1 = *interpreter->GetParameter<u64>(1);
                u64 address2 = *interpreter->GetParameter<u64>(2);

                // Get A Vec
                f64 x1 = *reinterpret_cast<f64*>(&memory[address1]);
                f64 y1 = *reinterpret_cast<f64*>(&memory[address1 + 8]);

                // Get B Vec
                f64 x2 = *reinterpret_cast<f64*>(&memory[address2]);
                f64 y2 = *reinterpret_cast<f64*>(&memory[address2 + 8]);

                // Get Return Vec
                f64* x3 = reinterpret_cast<f64*>(&memory[returnAddress]);
                f64* y3 = reinterpret_cast<f64*>(&memory[returnAddress + 8]);

                // Set Return Values
                *x3 = x1 - x2;
                *y3 = y1 - y2;
            }

            interpreter->SetReturnValue<u64>(&returnAddress);
            return true;
        }
        bool SubVec3Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(24, returnAddress))
            {
                u8* memory = interpreter->GetMemory();

                u64 address1 = *interpreter->GetParameter<u64>(1);
                u64 address2 = *interpreter->GetParameter<u64>(2);

                // Get A Vec
                f64 x1 = *reinterpret_cast<f64*>(&memory[address1]);
                f64 y1 = *reinterpret_cast<f64*>(&memory[address1 + 8]);
                f64 z1 = *reinterpret_cast<f64*>(&memory[address1 + 16]);

                // Get B Vec
                f64 x2 = *reinterpret_cast<f64*>(&memory[address2]);
                f64 y2 = *reinterpret_cast<f64*>(&memory[address2 + 8]);
                f64 z2 = *reinterpret_cast<f64*>(&memory[address2 + 16]);

                // Get Return Vec
                f64* x3 = reinterpret_cast<f64*>(&memory[returnAddress]);
                f64* y3 = reinterpret_cast<f64*>(&memory[returnAddress + 8]);
                f64* z3 = reinterpret_cast<f64*>(&memory[returnAddress + 16]);

                // Set Return Values
                *x3 = x1 - x2;
                *y3 = y1 - y2;
                *z3 = z1 - z2;
            }

            interpreter->SetReturnValue<u64>(&returnAddress);
            return true;
        }
        bool SubColorCallback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(32, returnAddress))
            {
                u8* memory = interpreter->GetMemory();

                u64 address1 = *interpreter->GetParameter<u64>(1);
                u64 address2 = *interpreter->GetParameter<u64>(2);

                // Get A Vec
                f64 r1 = *reinterpret_cast<f64*>(&memory[address1]);
                f64 g1 = *reinterpret_cast<f64*>(&memory[address1 + 8]);
                f64 b1 = *reinterpret_cast<f64*>(&memory[address1 + 16]);
                f64 a1 = *reinterpret_cast<f64*>(&memory[address1 + 24]);

                // Get B Vec
                f64 r2 = *reinterpret_cast<f64*>(&memory[address2]);
                f64 g2 = *reinterpret_cast<f64*>(&memory[address2 + 8]);
                f64 b2 = *reinterpret_cast<f64*>(&memory[address2 + 16]);
                f64 a2 = *reinterpret_cast<f64*>(&memory[address2 + 24]);

                // Get Return Vec
                f64* r3 = reinterpret_cast<f64*>(&memory[returnAddress]);
                f64* g3 = reinterpret_cast<f64*>(&memory[returnAddress + 8]);
                f64* b3 = reinterpret_cast<f64*>(&memory[returnAddress + 16]);
                f64* a3 = reinterpret_cast<f64*>(&memory[returnAddress + 24]);

                // Set Return Values
                *r3 = r1 - r2;
                *g3 = g1 - g2;
                *b3 = b1 - b2;
                *a3 = a1 - a2;
            }

            interpreter->SetReturnValue<u64>(&returnAddress);
            return true;
        }

        bool MulVec2Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(16, returnAddress))
            {
                u8* memory = interpreter->GetMemory();

                u64 address1 = *interpreter->GetParameter<u64>(1);
                u64 address2 = *interpreter->GetParameter<u64>(2);

                // Get A Vec
                f64 x1 = *reinterpret_cast<f64*>(&memory[address1]);
                f64 y1 = *reinterpret_cast<f64*>(&memory[address1 + 8]);

                // Get B Vec
                f64 x2 = *reinterpret_cast<f64*>(&memory[address2]);
                f64 y2 = *reinterpret_cast<f64*>(&memory[address2 + 8]);

                // Get Return Vec
                f64* x3 = reinterpret_cast<f64*>(&memory[returnAddress]);
                f64* y3 = reinterpret_cast<f64*>(&memory[returnAddress + 8]);

                // Set Return Values
                *x3 = x1 * x2;
                *y3 = y1 * y2;
            }

            interpreter->SetReturnValue<u64>(&returnAddress);
            return true;
        }
        bool MulVec3Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(24, returnAddress))
            {
                u8* memory = interpreter->GetMemory();

                u64 address1 = *interpreter->GetParameter<u64>(1);
                u64 address2 = *interpreter->GetParameter<u64>(2);

                // Get A Vec
                f64 x1 = *reinterpret_cast<f64*>(&memory[address1]);
                f64 y1 = *reinterpret_cast<f64*>(&memory[address1 + 8]);
                f64 z1 = *reinterpret_cast<f64*>(&memory[address1 + 16]);

                // Get B Vec
                f64 x2 = *reinterpret_cast<f64*>(&memory[address2]);
                f64 y2 = *reinterpret_cast<f64*>(&memory[address2 + 8]);
                f64 z2 = *reinterpret_cast<f64*>(&memory[address2 + 16]);

                // Get Return Vec
                f64* x3 = reinterpret_cast<f64*>(&memory[returnAddress]);
                f64* y3 = reinterpret_cast<f64*>(&memory[returnAddress + 8]);
                f64* z3 = reinterpret_cast<f64*>(&memory[returnAddress + 16]);

                // Set Return Values
                *x3 = x1 * x2;
                *y3 = y1 * y2;
                *z3 = z1 * z2;
            }

            interpreter->SetReturnValue<u64>(&returnAddress);
            return true;
        }
        bool MulColorCallback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(32, returnAddress))
            {
                u8* memory = interpreter->GetMemory();

                u64 address1 = *interpreter->GetParameter<u64>(1);
                u64 address2 = *interpreter->GetParameter<u64>(2);

                // Get A Vec
                f64 r1 = *reinterpret_cast<f64*>(&memory[address1]);
                f64 g1 = *reinterpret_cast<f64*>(&memory[address1 + 8]);
                f64 b1 = *reinterpret_cast<f64*>(&memory[address1 + 16]);
                f64 a1 = *reinterpret_cast<f64*>(&memory[address1 + 24]);

                // Get B Vec
                f64 r2 = *reinterpret_cast<f64*>(&memory[address2]);
                f64 g2 = *reinterpret_cast<f64*>(&memory[address2 + 8]);
                f64 b2 = *reinterpret_cast<f64*>(&memory[address2 + 16]);
                f64 a2 = *reinterpret_cast<f64*>(&memory[address2 + 24]);

                // Get Return Vec
                f64* r3 = reinterpret_cast<f64*>(&memory[returnAddress]);
                f64* g3 = reinterpret_cast<f64*>(&memory[returnAddress + 8]);
                f64* b3 = reinterpret_cast<f64*>(&memory[returnAddress + 16]);
                f64* a3 = reinterpret_cast<f64*>(&memory[returnAddress + 24]);

                // Set Return Values
                *r3 = r1 * r2;
                *g3 = g1 * g2;
                *b3 = b1 * b2;
                *a3 = a1 * a2;
            }

            interpreter->SetReturnValue<u64>(&returnAddress);
            return true;
        }

        bool DivVec2Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(16, returnAddress))
            {
                u8* memory = interpreter->GetMemory();

                u64 address1 = *interpreter->GetParameter<u64>(1);
                u64 address2 = *interpreter->GetParameter<u64>(2);

                // Get A Vec
                f64 x1 = *reinterpret_cast<f64*>(&memory[address1]);
                f64 y1 = *reinterpret_cast<f64*>(&memory[address1 + 8]);

                // Get B Vec
                f64 x2 = *reinterpret_cast<f64*>(&memory[address2]);
                f64 y2 = *reinterpret_cast<f64*>(&memory[address2 + 8]);

                // Get Return Vec
                f64* x3 = reinterpret_cast<f64*>(&memory[returnAddress]);
                f64* y3 = reinterpret_cast<f64*>(&memory[returnAddress + 8]);

                // Set Return Values
                *x3 = x1 / x2;
                *y3 = y1 / y2;
            }

            interpreter->SetReturnValue<u64>(&returnAddress);
            return true;
        }
        bool DivVec3Callback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(24, returnAddress))
            {
                u8* memory = interpreter->GetMemory();

                u64 address1 = *interpreter->GetParameter<u64>(1);
                u64 address2 = *interpreter->GetParameter<u64>(2);

                // Get A Vec
                f64 x1 = *reinterpret_cast<f64*>(&memory[address1]);
                f64 y1 = *reinterpret_cast<f64*>(&memory[address1 + 8]);
                f64 z1 = *reinterpret_cast<f64*>(&memory[address1 + 16]);

                // Get B Vec
                f64 x2 = *reinterpret_cast<f64*>(&memory[address2]);
                f64 y2 = *reinterpret_cast<f64*>(&memory[address2 + 8]);
                f64 z2 = *reinterpret_cast<f64*>(&memory[address2 + 16]);

                // Get Return Vec
                f64* x3 = reinterpret_cast<f64*>(&memory[returnAddress]);
                f64* y3 = reinterpret_cast<f64*>(&memory[returnAddress + 8]);
                f64* z3 = reinterpret_cast<f64*>(&memory[returnAddress + 16]);

                // Set Return Values
                *x3 = x1 / x2;
                *y3 = y1 / y2;
                *z3 = z1 / z2;
            }

            interpreter->SetReturnValue<u64>(&returnAddress);
            return true;
        }
        bool DivColorCallback(Interpreter* interpreter)
        {
            size_t returnAddress = 0;
            if (interpreter->AllocateHeap(32, returnAddress))
            {
                u8* memory = interpreter->GetMemory();

                u64 address1 = *interpreter->GetParameter<u64>(1);
                u64 address2 = *interpreter->GetParameter<u64>(2);

                // Get A Vec
                f64 r1 = *reinterpret_cast<f64*>(&memory[address1]);
                f64 g1 = *reinterpret_cast<f64*>(&memory[address1 + 8]);
                f64 b1 = *reinterpret_cast<f64*>(&memory[address1 + 16]);
                f64 a1 = *reinterpret_cast<f64*>(&memory[address1 + 24]);

                // Get B Vec
                f64 r2 = *reinterpret_cast<f64*>(&memory[address2]);
                f64 g2 = *reinterpret_cast<f64*>(&memory[address2 + 8]);
                f64 b2 = *reinterpret_cast<f64*>(&memory[address2 + 16]);
                f64 a2 = *reinterpret_cast<f64*>(&memory[address2 + 24]);

                // Get Return Vec
                f64* r3 = reinterpret_cast<f64*>(&memory[returnAddress]);
                f64* g3 = reinterpret_cast<f64*>(&memory[returnAddress + 8]);
                f64* b3 = reinterpret_cast<f64*>(&memory[returnAddress + 16]);
                f64* a3 = reinterpret_cast<f64*>(&memory[returnAddress + 24]);

                // Set Return Values
                *r3 = r1 / r2;
                *g3 = g1 / g2;
                *b3 = b1 / b2;
                *a3 = a1 / a2;
            }

            interpreter->SetReturnValue<u64>(&returnAddress);
            return true;
        }

        void Register(Compiler* cc, ScriptAPI* scriptAPI)
        {
            Module* module = cc->CreateNativeModule("Math");
            scriptAPI->RegisterModule(module);

            NativeFunction nfVec2(cc, module, "Vec2", Vec2Callback);
            NativeFunction nfVec3(cc, module, "Vec3", Vec3Callback);
            NativeFunction nfColor(cc, module, "Color", ColorCallback);

            NativeFunction nfAddVec2(cc, module, "AddVec2", AddVec2Callback);
            NativeFunction nfAddVec3(cc, module, "AddVec3", AddVec3Callback);
            NativeFunction nfAddColor(cc, module, "AddColor", AddColorCallback);

            NativeFunction nfSubVec2(cc, module, "SubVec2", SubVec2Callback);
            NativeFunction nfSubVec3(cc, module, "SubVec3", SubVec3Callback);
            NativeFunction nfSubColor(cc, module, "SubColor", SubColorCallback);

            NativeFunction nfMulVec2(cc, module, "MulVec2", MulVec2Callback);
            NativeFunction nfMulVec3(cc, module, "MulVec3", MulVec3Callback);
            NativeFunction nfMulColor(cc, module, "MulColor", MulColorCallback);

            NativeFunction nfDivVec2(cc, module, "DivVec2", DivVec2Callback);
            NativeFunction nfDivVec3(cc, module, "DivVec3", DivVec3Callback);
            NativeFunction nfDivColor(cc, module, "DivColor", DivColorCallback);
        }

        void Init(ScriptAPI* scriptAPI)
        {
            scriptAPI->AddCallback(Register);
        }
    }
}