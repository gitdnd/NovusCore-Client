#pragma once
#include <entity/fwd.hpp>
#include "../Components/Singletons/AreaUpdateSingleton.h"

namespace NDBC
{
    struct Light;
    struct LightIntBand;
}

struct NDBCSingleton;
struct MapSingleton;
class AreaUpdateSystem
{
public:
    static void Init(entt::registry& registry);
    static void Update(entt::registry& registry);

    static void GetChunkIdAndCellIdFromPosition(const vec3& position, u16& inChunkId, u16& inCellId);
    static AreaUpdateLightColorData GetLightColorData(NDBCSingleton& ndbcSingleton, MapSingleton& mapSingleton, const NDBC::Light* light);
    static vec3 AreaUpdateSystem::GetColorValueFromLightIntBand(NDBC::LightIntBand* lightIntBand, u32 timeInSeconds);
    static vec3 UnpackUIntBGRToColor(u32 bgr);
};