#include "AreaUpdateSystem.h"
#include "../../Utils/ServiceLocator.h"
#include "../../Utils/MapUtils.h"
#include "../../Rendering/CameraOrbital.h"
#include "../../Rendering/CameraFreeLook.h"
#include "../Components/Singletons/TimeSingleton.h"
#include "../Components/Singletons/NDBCSingleton.h"
#include "../Components/Singletons/DayNightSingleton.h"

#include <glm/glm.hpp>
#include <imgui/imgui.h>

void AreaUpdateSystem::Init(entt::registry& registry)
{
    registry.set<AreaUpdateSingleton>();
}

void AreaUpdateSystem::Update(entt::registry& registry)
{
    TimeSingleton& timeSingleton = registry.ctx<TimeSingleton>();
    AreaUpdateSingleton& areaUpdateSingleton = registry.ctx<AreaUpdateSingleton>();

    areaUpdateSingleton.updateTimer += timeSingleton.deltaTime;

    if (areaUpdateSingleton.updateTimer >= AreaUpdateTimeToUpdate)
    {
        areaUpdateSingleton.updateTimer -= AreaUpdateTimeToUpdate;

        NDBCSingleton& ndbcSingleton = registry.ctx<NDBCSingleton>();
        MapSingleton& mapSingleton = registry.ctx<MapSingleton>();
        Terrain::Map& currentMap = mapSingleton.GetCurrentMap();

        if (!currentMap.IsLoadedMap())
            return;

        Camera* camera = ServiceLocator::GetCamera();
        vec3 position = camera->GetPosition();

        u16 chunkId = 0;
        u16 cellId = 0;
        GetChunkIdAndCellIdFromPosition(position, chunkId, cellId);

        NDBC::File* areaTableNDBC = ndbcSingleton.GetNDBCFile("AreaTable");
        NDBC::File* lightNDBC = ndbcSingleton.GetNDBCFile("Light");

        const Terrain::Chunk* chunk = currentMap.GetChunkById(chunkId);
        const Terrain::Cell* cell = nullptr;

        const NDBC::AreaTable* zone = nullptr;
        const NDBC::AreaTable* area = nullptr;
        
        u32 zoneId = 0;
        u32 areaId = 0;

        if (chunk != nullptr)
        {
            cell = &chunk->cells[cellId];
            if (cell != nullptr)
            {
                zone = areaTableNDBC->GetRowById<NDBC::AreaTable>(cell->areaId); 
                
                if (zone)
                {
                    if (zone->parentId)
                    {
                        area = zone;
                        zone = areaTableNDBC->GetRowById<NDBC::AreaTable>(area->parentId);

                        areaId = area->id;
                    }

                    zoneId = zone->id;
                }
            }
        }

        areaUpdateSingleton.zoneId = zoneId;
        areaUpdateSingleton.areaId = areaId;

        // Eastern Kingdoms light is default (Can be overriden see below)
        NDBC::Light* defaultLight = lightNDBC->GetRowById<NDBC::Light>(1);
        AreaUpdateLightColorData finalColorData = GetLightColorData(ndbcSingleton, mapSingleton, defaultLight);

        i32 forceUseDefaultLight = *CVarSystem::Get()->GetIntCVar("lights.useDefault");
        if (!forceUseDefaultLight)
        {
            areaUpdateSingleton.totalLightDatas.clear();
            const std::vector<NDBC::Light*>& lights = mapSingleton.GetLightsByMapId(currentMap.id);

            for (NDBC::Light* light : lights)
            {
                const vec3& lightPosition = light->position;

                // LightPosition of (0,0,0) means default, override!
                if (lightPosition == vec3(0, 0, 0))
                {
                    defaultLight = light;
                    continue;
                }

                f32 distanceToLight = glm::distance(position, lightPosition);
                if (distanceToLight <= light->fallOff.y)
                {
                    AreaUpdateLightData& lightData = areaUpdateSingleton.totalLightDatas.emplace_back();
                    lightData.lightId = light->id;
                    lightData.fallOff = light->fallOff;
                    lightData.distanceToCenter = distanceToLight;

                    lightData.colorData = GetLightColorData(ndbcSingleton, mapSingleton, light);
                }
            }

            // Sort Lights by distance to center
            std::sort(areaUpdateSingleton.totalLightDatas.begin(), areaUpdateSingleton.totalLightDatas.end(), [](AreaUpdateLightData a, AreaUpdateLightData b) { return a.distanceToCenter > b.distanceToCenter; });

            AreaUpdateLightColorData lightColor = GetLightColorData(ndbcSingleton, mapSingleton, defaultLight);

            for (AreaUpdateLightData& lightData : areaUpdateSingleton.totalLightDatas)
            {
                f32 lengthOfFallOff = lightData.fallOff.y - lightData.fallOff.x;
                f32 val = (lightData.fallOff.y - lightData.distanceToCenter) / lengthOfFallOff;

                // Check if We are inside the inner radius of the light
                if (lightData.distanceToCenter <= lightData.fallOff.x)
                    val = 1.0f;

                lightColor.ambientColor = glm::mix(lightColor.ambientColor, lightData.colorData.ambientColor, val);
                lightColor.diffuseColor = glm::mix(lightColor.diffuseColor, lightData.colorData.diffuseColor, val);
                lightColor.skybandTopColor = glm::mix(lightColor.skybandTopColor, lightData.colorData.skybandTopColor, val);
                lightColor.skybandMiddleColor = glm::mix(lightColor.skybandMiddleColor, lightData.colorData.skybandMiddleColor, val);
                lightColor.skybandBottomColor = glm::mix(lightColor.skybandBottomColor, lightData.colorData.skybandBottomColor, val);
                lightColor.skybandAboveHorizonColor = glm::mix(lightColor.skybandAboveHorizonColor, lightData.colorData.skybandAboveHorizonColor, val);
                lightColor.skybandHorizonColor = glm::mix(lightColor.skybandHorizonColor, lightData.colorData.skybandHorizonColor, val);
            }

            finalColorData.ambientColor = lightColor.ambientColor;
            finalColorData.diffuseColor = lightColor.diffuseColor;
            finalColorData.skybandTopColor = lightColor.skybandTopColor;
            finalColorData.skybandMiddleColor = lightColor.skybandMiddleColor;
            finalColorData.skybandBottomColor = lightColor.skybandBottomColor;
            finalColorData.skybandAboveHorizonColor = lightColor.skybandAboveHorizonColor;
            finalColorData.skybandHorizonColor = lightColor.skybandHorizonColor;
        }

        mapSingleton.SetLightColorData(finalColorData);

        // Get Light Direction
        {
            DayNightSingleton& dayNightSingleton = registry.ctx<DayNightSingleton>();

            f32 phiValue = 0;
            const f32 thetaValue = 3.926991f;
            const f32 phiTable[4] =
            {
                2.2165682f,
                1.9198623f,
                2.2165682f,
                1.9198623f
            };

            f32 progressDayAndNight = dayNightSingleton.seconds / 86400.0f;
            u32 currentPhiIndex = static_cast<u32>(progressDayAndNight / 0.25f);
            u32 nextPhiIndex = 0;

            if (currentPhiIndex < 3)
                nextPhiIndex = currentPhiIndex + 1;

            // Lerp between the current value of phi and the next value of phi
            {
                f32 currentTimestamp = currentPhiIndex * 0.25f;
                f32 nextTimestamp = nextPhiIndex * 0.25f;

                f32 transitionTime = 0.25f;
                f32 transitionProgress = (progressDayAndNight / 0.25f) - currentPhiIndex;

                f32 currentPhiValue = phiTable[currentPhiIndex];
                f32 nextPhiValue = phiTable[nextPhiIndex];

                phiValue = glm::mix(currentPhiValue, nextPhiValue, transitionProgress);
            }

            // Convert from Spherical Position to Cartesian coordinates
            f32 sinPhi = glm::sin(phiValue);
            f32 cosPhi = glm::cos(phiValue);

            f32 sinTheta = glm::sin(thetaValue);
            f32 cosTheta = glm::cos(thetaValue);

            f32 lightDirX = sinPhi * cosTheta;
            f32 lightDirY = sinPhi * sinTheta;
            f32 lightDirZ = cosPhi;

            // Can also try (X, Z, -Y)
            mapSingleton.SetLightDirection(vec3(lightDirX, lightDirY, lightDirZ));
        }
    }
}

void AreaUpdateSystem::GetChunkIdAndCellIdFromPosition(const vec3& position, u16& inChunkId, u16& inCellId)
{
    entt::registry* registry = ServiceLocator::GetGameRegistry();

    vec2 adtCoords = Terrain::MapUtils::WorldPositionToADTCoordinates(position);
    vec2 chunkCoords = Terrain::MapUtils::GetChunkFromAdtPosition(adtCoords);
    vec2 chunkRemainder = chunkCoords - glm::floor(chunkCoords);
    u32 chunkId = Terrain::MapUtils::GetChunkIdFromChunkPos(chunkCoords);

    vec2 cellCoords = (chunkRemainder * Terrain::MAP_CHUNK_SIZE) / Terrain::MAP_CELL_SIZE;
    u32 cellId = Terrain::MapUtils::GetCellIdFromCellPos(cellCoords);

    inChunkId = chunkId;
    inCellId = cellId;
}

AreaUpdateLightColorData AreaUpdateSystem::GetLightColorData(NDBCSingleton& ndbcSingleton, MapSingleton& mapSingleton, const NDBC::Light* light)
{
    entt::registry* registry = ServiceLocator::GetGameRegistry();
    DayNightSingleton& dayNightSingleton = registry->ctx<DayNightSingleton>();
    NDBC::File* lightParamNDBC = ndbcSingleton.GetNDBCFile("LightParams"_h);
    NDBC::File* lightIntBandNDBC = ndbcSingleton.GetNDBCFile("LightIntBand"_h);
    NDBC::File* lightFloatBandNDBC = ndbcSingleton.GetNDBCFile("LightFloatBand"_h);

    AreaUpdateLightColorData colorData;
    u32 timeInSeconds = static_cast<u32>(dayNightSingleton.seconds);

    NDBC::LightParams* lightParams = lightParamNDBC->GetRowById<NDBC::LightParams>(light->paramClearId);

    // The ID specified in the LightParams are 1 indexed as opposed to 0 indexed, so in order to corectly calculate the starting Id into "IntBand" and "FloatBand", we must subtract 1 from the stored LightParamsId
    u32 lightIntBandStartId = ((lightParams->id - 1) * 18) + 1;
    u32 lightFloatBandStartId = ((lightParams->id - 1) * 6) + 1;

    // TODO: If the first timeValue for a given light is higher than our current time, we need to figure out what to do.
    // Do we discard that light in the search or do we handle it in here?

    // Get Ambient Light
    {
        NDBC::LightIntBand* lightIntBand = lightIntBandNDBC->GetRowById<NDBC::LightIntBand>(lightIntBandStartId);
        colorData.ambientColor = GetColorValueFromLightIntBand(lightIntBand, timeInSeconds);
    }

    // Get Diffuse Light
    {
        NDBC::LightIntBand* lightIntBand = lightIntBandNDBC->GetRowById<NDBC::LightIntBand>(lightIntBandStartId + 1);
        colorData.diffuseColor = GetColorValueFromLightIntBand(lightIntBand, timeInSeconds);
    }

    // Get Skyband Color Top
    {
        NDBC::LightIntBand* lightIntBand = lightIntBandNDBC->GetRowById<NDBC::LightIntBand>(lightIntBandStartId + 2);
        colorData.skybandTopColor = GetColorValueFromLightIntBand(lightIntBand, timeInSeconds);
    }

    // Get Skyband Color Middle
    {
        NDBC::LightIntBand* lightIntBand = lightIntBandNDBC->GetRowById<NDBC::LightIntBand>(lightIntBandStartId + 3);
        colorData.skybandMiddleColor = GetColorValueFromLightIntBand(lightIntBand, timeInSeconds);
    }

    // Get Skyband Color Bottom
    {
        NDBC::LightIntBand* lightIntBand = lightIntBandNDBC->GetRowById<NDBC::LightIntBand>(lightIntBandStartId + 4);
        colorData.skybandBottomColor = GetColorValueFromLightIntBand(lightIntBand, timeInSeconds);
    }

    // Get Skyband Color AboveHorizon
    {
        NDBC::LightIntBand* lightIntBand = lightIntBandNDBC->GetRowById<NDBC::LightIntBand>(lightIntBandStartId + 5);
        colorData.skybandAboveHorizonColor = GetColorValueFromLightIntBand(lightIntBand, timeInSeconds);
    }

    // Get Skyband Color Horizon
    {
        NDBC::LightIntBand* lightIntBand = lightIntBandNDBC->GetRowById<NDBC::LightIntBand>(lightIntBandStartId + 6);
        colorData.skybandHorizonColor = GetColorValueFromLightIntBand(lightIntBand, timeInSeconds);
    }

    return colorData;
}

vec3 AreaUpdateSystem::GetColorValueFromLightIntBand(NDBC::LightIntBand* lightIntBand, u32 timeInSeconds)
{
    const u32 TotalSecondsInOneDay = 86400;
    vec3 color = vec3(0.0f, 0.0f, 0.0f);

    if (lightIntBand->timeValues[0] < timeInSeconds)
    {
        color = UnpackUIntBGRToColor(lightIntBand->colorValues[0]);

        if (lightIntBand->entries > 1)
        {
            u32 currentIndex = 0;
            u32 nextIndex = 0;

            for (i32 i = lightIntBand->entries - 1; i >= 0; i--)
            {
                if (lightIntBand->timeValues[i] <= timeInSeconds)
                {
                    currentIndex = i;
                    break;
                }
            }

            if (currentIndex < lightIntBand->entries - 1)
                nextIndex = currentIndex + 1;

            // Lerp between Current the color of the current timestamp and the color of the next timestamp
            {
                u32 currentTimestamp = lightIntBand->timeValues[currentIndex];
                u32 nextTimestamp = lightIntBand->timeValues[nextIndex];

                f32 transitionTime = 0;
                
                if (nextTimestamp < currentTimestamp)
                {
                    // Calculate diff between current & next (range is 0..86400, it can wrap around)
                    u32 diff = (TotalSecondsInOneDay - currentTimestamp) + nextTimestamp;
                    transitionTime = static_cast<f32>(diff);
                }
                else
                {
                    transitionTime = static_cast<f32>(nextTimestamp - currentTimestamp);
                }

                f32 relativeSeconds = static_cast<f32>(timeInSeconds - currentTimestamp);

                f32 transitionProgress = relativeSeconds / transitionTime;

                vec3 currentColor = UnpackUIntBGRToColor(lightIntBand->colorValues[currentIndex]);
                vec3 nextColor = UnpackUIntBGRToColor(lightIntBand->colorValues[nextIndex]);

                color = glm::mix(currentColor, nextColor, transitionProgress);
            }
        }
    }

    return color;
}

vec3 AreaUpdateSystem::UnpackUIntBGRToColor(u32 bgr)
{
    vec3 result;

    u8 colorR = bgr >> 16;
    u8 colorG = (bgr >> 8) & 0xFF;
    u8 colorB = bgr & 0xFF;

    result.r = colorR / 255.0f;
    result.g = colorG / 255.0f;
    result.b = colorB / 255.0f;
    
    return result;
}
