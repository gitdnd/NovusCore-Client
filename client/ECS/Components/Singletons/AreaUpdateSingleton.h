#pragma once
#include <NovusTypes.h>

namespace NDBC
{
    struct Light;
}

constexpr f32 AreaUpdateTimeToUpdate = 1 / 30.0f;
struct AreaUpdateSingleton
{
    u16 zoneId;
    u16 areaId;
    u16 lightId;

    f32 updateTimer = AreaUpdateTimeToUpdate;
};

struct AreaUpdateLightData
{
    NDBC::Light* light;
    f32 distance;
};

struct AreaUpdateLightColorData
{
    vec3 ambientColor = vec3(0, 0, 0);
    vec3 diffuseColor = vec3(0, 0, 0);

    vec3 skybandTopColor = vec3(0.0f, 0.0f, 0.0f);
    vec3 skybandMiddleColor = vec3(0.0f, 0.0f, 0.0f);
    vec3 skybandBottomColor = vec3(0.0f, 0.0f, 0.0f);
    vec3 skybandAboveHorizonColor = vec3(0.0f, 0.0f, 0.0f);
    vec3 skybandHorizonColor = vec3(0.0f, 0.0f, 0.0f);
};