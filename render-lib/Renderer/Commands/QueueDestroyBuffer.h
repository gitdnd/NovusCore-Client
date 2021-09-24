#pragma once
#include <NovusTypes.h>
#include "../Descriptors/BufferDesc.h"

namespace Renderer
{
    namespace Commands
    {
        struct QueueDestroyBuffer
        {
            static const BackendDispatchFunction DISPATCH_FUNCTION;

            BufferID buffer = BufferID::Invalid();
        };
    }
}