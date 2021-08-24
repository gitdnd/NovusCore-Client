#pragma once
#include <NovusTypes.h>
#include "../Descriptors/ImageDesc.h"
#include "../Descriptors/DepthImageDesc.h"

namespace Renderer
{
    namespace Commands
    {
        struct ClearImageColor
        {
            static const BackendDispatchFunction DISPATCH_FUNCTION;

            ImageID image = ImageID::Invalid();
            Color color = Color::Clear;
        };

        struct ClearImageUInt
        {
            static const BackendDispatchFunction DISPATCH_FUNCTION;

            ImageID image = ImageID::Invalid();
            uvec4 values = uvec4(0, 0, 0, 0);
        };

        struct ClearImageInt
        {
            static const BackendDispatchFunction DISPATCH_FUNCTION;

            ImageID image = ImageID::Invalid();
            ivec4 values = ivec4(0, 0, 0, 0);
        };
        
        struct ClearDepthImage
        {
            static const BackendDispatchFunction DISPATCH_FUNCTION;

            DepthImageID image = DepthImageID::Invalid();
            DepthClearFlags flags = DepthClearFlags::BOTH;
            f32 depth = 0.0f;
            u8 stencil = 0;
        };
    }
}