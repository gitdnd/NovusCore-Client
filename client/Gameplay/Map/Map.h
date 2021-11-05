/*
    MIT License

    Copyright (c) 2018-2019 NovusCore

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/
#pragma once
#include <NovusTypes.h>
#include <robin_hood.h>
#include <entity/fwd.hpp>
#include <limits>
#include <Containers/StringTable.h>
#include <Utils/SafeVector.h>
#include "Chunk.h"

// First of all, forget every naming convention wowdev.wiki uses, it's extremely confusing.
// A Map (e.g. Eastern Kingdoms) consists of 64x64 Chunks which may or may not be used.
// A Chunk consists of 16x16 Cells which are all being used.
// A Cell consists of two interlapping grids. There is the 9*9 OUTER grid and the 8*8 INNER grid.

class FileReader;
namespace Terrain
{
    constexpr f32 MAP_SIZE = MAP_CHUNK_SIZE * MAP_CHUNKS_PER_MAP_STRIDE; // yards
    constexpr f32 MAP_HALF_SIZE = MAP_SIZE / 2.0f; // yards

    struct MapDetailFlag
    {
        u32 UseMapObjectInsteadOfTerrain : 1;
    };

    struct MapHeader
    {
        u32 token = 1313685840; // UTF8 -> Binary -> Decimal for "nmap"
        u32 version = 2;

        MapDetailFlag flags;

        std::string mapObjectName = "";
        Placement mapObjectPlacement;

        static bool Read(FileReader& reader, Terrain::MapHeader& header);
    };

    struct PlacementDetails
    {
        u32 loadedIndex = 0;
        u32 instanceIndex = 0;
    };

    struct Map
    {
        Map() {}

        MapHeader header;

        u16 id = std::numeric_limits<u16>().max(); // Default Map to Invalid ID
        std::string_view name;
        robin_hood::unordered_map<u16, Chunk> chunks;
        robin_hood::unordered_map<u16, SafeVector<entt::entity>> chunksEntityList;
        robin_hood::unordered_map<u16, SafeVector<entt::entity>> chunksCollidableEntityList;
        robin_hood::unordered_map<u16, StringTable> stringTables;

        bool IsLoadedMap() { return id != std::numeric_limits<u16>().max(); }
        bool IsMapLoaded(u16 newId) { return id == newId; }

        Terrain::Chunk* GetChunkById(u16 chunkID)
        {
            auto itr = chunks.find(chunkID);
            if (itr == chunks.end())
                return nullptr;

            return &itr->second;
        }
        SafeVector<entt::entity>* GetEntityListByChunkID(u16 chunkID)
        {
            auto itr = chunksEntityList.find(chunkID);
            if (itr == chunksEntityList.end())
                return nullptr;

            return &itr->second;
        }
        SafeVector<entt::entity>* GetCollidableEntityListByChunkID(u16 chunkID)
        {
            auto itr = chunksCollidableEntityList.find(chunkID);
            if (itr == chunksCollidableEntityList.end())
                return nullptr;

            return &itr->second;
        }

        void GetChunkPositionFromChunkId(u16 chunkId, u16& x, u16& y) const;
        bool GetChunkIdFromChunkPosition(u16 x, u16 y, u16& chunkId) const;

        void Clear()
        {
            id = std::numeric_limits<u16>().max();

            memset(&header.flags, 0, sizeof(header.flags));
            header.mapObjectName = "";
            header.mapObjectPlacement.nameID = std::numeric_limits<u32>().max();
            header.mapObjectPlacement.position = vec3(0, 0, 0);
            header.mapObjectPlacement.rotation = quaternion(1, 0, 0, 0);
            header.mapObjectPlacement.scale = 0;

            chunks.clear();

            for (auto& pair : chunksEntityList)
            {
                pair.second.Clear();
            }
            chunksEntityList.clear();

            for (auto& pair : chunksCollidableEntityList)
            {
                pair.second.Clear();
            }
            chunksCollidableEntityList.clear();

            for (auto& pair : stringTables)
            {
                pair.second.Clear();
            }
            stringTables.clear();
        }
    };
}