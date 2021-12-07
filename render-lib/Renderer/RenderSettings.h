#pragma once
#include <NovusTypes.h>
#include "RenderStates.h"

namespace Renderer
{
    namespace Settings
    {
        const i32 SCREEN_WIDTH = 1920;
        const i32 SCREEN_HEIGHT = 1080;
        constexpr size_t STAGING_BUFFER_SIZE = 32 * 1024 * 1024; // 32 MB

        const FrontFaceState FRONT_FACE_STATE = FrontFaceState::COUNTERCLOCKWISE;
    }
}
