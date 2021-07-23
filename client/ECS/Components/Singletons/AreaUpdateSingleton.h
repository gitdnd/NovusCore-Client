#pragma once
#include <NovusTypes.h>

namespace NDBC
{
    struct Light;
}

constexpr f32 AreaUpdateTimeToUpdate = 1 / 30.0f;

struct AreaUpdateLightColorData
{
    // Colors are defaulted so the world isn't completely dark when no map is loaded
    vec3 ambientColor = vec3(0.60f, 0.53f, 0.40f);
    vec3 diffuseColor = vec3(0.41f, 0.51f, 0.60f);

    vec3 skybandTopColor = vec3(0.00f, 0.12f, 0.29f);
    vec3 skybandMiddleColor = vec3(0.23f, 0.64f, 0.81f);
    vec3 skybandBottomColor = vec3(0.60f, 0.86f, 0.96f);
    vec3 skybandAboveHorizonColor = vec3(0.69f, 0.85f, 0.88f); 
    vec3 skybandHorizonColor = vec3(0.71f, 0.71f, 0.71f);

    AreaUpdateLightColorData operator+(const AreaUpdateLightColorData& other) const
    {
        AreaUpdateLightColorData colorData;

        colorData.ambientColor = ambientColor + other.ambientColor;
        colorData.diffuseColor = diffuseColor + other.diffuseColor;

        colorData.skybandTopColor = skybandTopColor + other.skybandTopColor;
        colorData.skybandMiddleColor = skybandMiddleColor + other.skybandMiddleColor;
        colorData.skybandBottomColor = skybandBottomColor + other.skybandBottomColor;
        colorData.skybandAboveHorizonColor = skybandAboveHorizonColor + other.skybandAboveHorizonColor;
        colorData.skybandHorizonColor = skybandHorizonColor + other.skybandHorizonColor;

        return colorData;
    }
    AreaUpdateLightColorData operator/(float scalar) const
    {
        AreaUpdateLightColorData colorData;

        colorData.ambientColor = ambientColor / scalar;
        colorData.diffuseColor = diffuseColor / scalar;

        colorData.skybandTopColor = skybandTopColor / scalar;
        colorData.skybandMiddleColor = skybandMiddleColor / scalar;
        colorData.skybandBottomColor = skybandBottomColor / scalar;
        colorData.skybandAboveHorizonColor = skybandAboveHorizonColor / scalar;
        colorData.skybandHorizonColor = skybandHorizonColor / scalar;

        return colorData;
    }
    void operator+=(const AreaUpdateLightColorData& other)
    {
        ambientColor += other.ambientColor;
        diffuseColor += other.diffuseColor;

        skybandTopColor += other.skybandTopColor;
        skybandMiddleColor += other.skybandMiddleColor;
        skybandBottomColor += other.skybandBottomColor;
        skybandAboveHorizonColor += other.skybandAboveHorizonColor;
        skybandHorizonColor += other.skybandHorizonColor;
    }
};

struct AreaUpdateLightData
{
    u32 lightId;
    vec2 fallOff;
    f32 distanceToCenter;
    f32 distanceToInnerRadius;

    AreaUpdateLightColorData colorData;
};

struct AreaUpdateSingleton
{
    u16 zoneId;
    u16 areaId;
    u16 lightId;

    std::vector<AreaUpdateLightData> totalLightDatas;

    f32 updateTimer = AreaUpdateTimeToUpdate;
};