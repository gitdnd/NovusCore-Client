#pragma once
#include <NovusTypes.h>
#include <Math/Geometry.h>
#include "NDBC/NDBCEditorHandler.h"
#include <InputManager.h>

class Window;
class DebugRenderer;

namespace Terrain
{
    struct Chunk;
    struct Cell;
}

namespace NDBC
{
    struct AreaTable;
}

namespace Editor
{
    enum QueryObjectType
    {
        None = 0,
        Terrain,
        MapObject,
        ComplexModelOpaque,
        ComplexModelTransparent
    };

    struct CModelAnimationEntry
    {
        u16 id;
        const char* name;
    };

    class Editor
    {
    public:
        struct SelectedTerrainData
        {
            Geometry::AABoundingBox boundingBox;
            std::vector<Geometry::Triangle> triangles;

            vec2 adtCoords;
            vec2 chunkCoords;
            vec2 chunkWorldPos;
            vec2 cellCoords;

            u32 chunkId;
            u32 cellId;

            Terrain::Chunk* chunk;
            Terrain::Cell* cell;

            NDBC::AreaTable* zone;
            NDBC::AreaTable* area;

            bool drawWireframe = false;
        };

        struct SelectedMapObjectData
        {
            Geometry::AABoundingBox boundingBox;
            u32 instanceLookupDataID;

            u32 numRenderBatches;
            i32 selectedRenderBatch;
            bool drawWireframe = false;
            bool wireframeEntireObject = true;
        };

        struct SelectedComplexModelData
        {
            Geometry::AABoundingBox boundingBox;
            u32 drawCallDataID;
            u32 instanceID;
            bool isOpaque;

            u32 numRenderBatches;
            i32 selectedRenderBatch;
            bool drawWireframe = false;
            bool wireframeEntireObject = true;

            u32 selectedAnimationEntry = 0;
            std::vector<CModelAnimationEntry> animationEntries;
        };

    public:
        Editor();

        void Update(f32 deltaTime);
        void DrawImguiMenuBar();

        void ClearSelection();
        bool HasSelectedObject() { return _activeToken; }
        u32 GetActiveToken() { return _activeToken; }

        const SelectedTerrainData& GetSelectedTerrainData() { return _selectedTerrainData; }
        const SelectedMapObjectData& GetSelectedMapObjectData() { return _selectedMapObjectData; }
        const SelectedComplexModelData& GetSelectedComplexModelData() { return _selectedComplexModelData; }

    private:
        void TerrainSelectionDrawImGui();
        void MapObjectSelectionDrawImGui();
        void ComplexModelSelectionDrawImGui();

        bool IsRayIntersectingAABB(const vec3& rayOrigin, const vec3& oneOverRayDir, const Geometry::AABoundingBox& boundingBox, f32& t);
        bool OnMouseClickLeft(i32 key, KeybindAction action, KeybindModifier modifier);

        NDBCEditorHandler _ndbcEditorHandler;
    private:
        u32 _activeToken = 0;
        u32 _queriedToken = 0;
        bool _selectedObjectDataInitialized = false;

        SelectedTerrainData _selectedTerrainData;
        SelectedMapObjectData _selectedMapObjectData;
        SelectedComplexModelData _selectedComplexModelData;
    };
}