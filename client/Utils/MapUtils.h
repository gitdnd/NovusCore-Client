#pragma once
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Copyright (c) 2008-2019 NVIDIA Corporation. All rights reserved.
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.

// Code used from PhysX was modified by us

#include <NovusTypes.h>
#include <Math/Geometry.h>
#include <entt.hpp>
#include "ServiceLocator.h"
#include "../Gameplay/Map/Chunk.h"

#include "../ECS/Components/Singletons/MapSingleton.h"

namespace Terrain
{
    namespace MapUtils
    {
        constexpr f32 f32MaxValue = 3.40282346638528859812e+38F;

        bool LoadMap(entt::registry* registry, const NDBC::Map* map);

        inline vec2 GetChunkPosition(u32 chunkID)
        {
            const u32 chunkX = chunkID % Terrain::MAP_CHUNKS_PER_MAP_STRIDE;
            const u32 chunkY = chunkID / Terrain::MAP_CHUNKS_PER_MAP_STRIDE;

            const vec2 chunkPos = MAP_HALF_SIZE - (vec2(chunkX, chunkY) * Terrain::MAP_CHUNK_SIZE);
            return -chunkPos;
        }

        inline vec2 GetCellPosition(u32 chunkID, u32 cellID)
        {
            const u32 cellX = cellID % Terrain::MAP_CELLS_PER_CHUNK_SIDE;
            const u32 cellY = cellID / Terrain::MAP_CELLS_PER_CHUNK_SIDE;

            const vec2 chunkPos = GetChunkPosition(chunkID);
            const vec2 cellPos = vec2(cellX, cellY) * Terrain::MAP_CELL_SIZE;

            vec2 pos = chunkPos + cellPos;
            return vec2(-pos.y, -pos.x);
        }

        inline vec2 GetCellPosition(vec2 chunkPos, u32 cellID)
        {
            const u32 cellX = cellID % Terrain::MAP_CELLS_PER_CHUNK_SIDE;
            const u32 cellY = cellID / Terrain::MAP_CELLS_PER_CHUNK_SIDE;

            const vec2 cellPos = vec2(cellX, cellY) * Terrain::MAP_CELL_SIZE;

            vec2 pos = chunkPos + cellPos;
            return vec2(-pos.y, -pos.x);
        }

        inline void AlignCellBorders(Terrain::Chunk& chunk)
        {
            for (u32 cellID = 0; cellID < Terrain::MAP_CELLS_PER_CHUNK; cellID++)
            {
                Terrain::Cell& currentCell = chunk.cells[cellID];

                u16 chunkX = cellID % Terrain::MAP_CELLS_PER_CHUNK_SIDE;
                u16 chunkY = cellID / Terrain::MAP_CELLS_PER_CHUNK_SIDE;

                u16 aboveCellID = cellID - Terrain::MAP_CELLS_PER_CHUNK_SIDE;
                u16 leftCellID = cellID - 1;

                bool hasCellAbove = chunkY > 0;
                bool hasCellLeft = chunkX > 0;

                if (hasCellAbove)
                {
                    Terrain::Cell& aboveCell = chunk.cells[aboveCellID];

                    // Avoid fixing the very first height value within the cell grid (This is handled by "hasChunkLeft"
                    for (u32 currentHeightID = 1; currentHeightID < Terrain::MAP_CELL_OUTER_GRID_STRIDE; currentHeightID++)
                    {
                        u32 aboveHeightID = currentHeightID + (Terrain::MAP_CELL_TOTAL_GRID_SIZE - Terrain::MAP_CELL_OUTER_GRID_STRIDE);
                        currentCell.heightData[currentHeightID] = aboveCell.heightData[aboveHeightID];
                    }
                }

                if (hasCellLeft)
                {
                    Terrain::Cell& leftCell = chunk.cells[leftCellID];

                    for (u32 currentHeightID = 0; currentHeightID < Terrain::MAP_CELL_TOTAL_GRID_SIZE; currentHeightID += Terrain::MAP_CELL_TOTAL_GRID_STRIDE)
                    {
                        u32 aboveHeightID = currentHeightID + (Terrain::MAP_CELL_OUTER_GRID_STRIDE - 1);
                        currentCell.heightData[currentHeightID] = leftCell.heightData[aboveHeightID];
                    }
                }
            }
        }

        inline void AlignChunkBorders(Terrain::Map& map)
        {
            for (auto& chunkItr : map.chunks)
            {
                const u16& chunkID = chunkItr.first;
                Terrain::Chunk& chunk = chunkItr.second;

                u16 chunkX = chunkID % Terrain::MAP_CHUNKS_PER_MAP_STRIDE;
                u16 chunkY = chunkID / Terrain::MAP_CHUNKS_PER_MAP_STRIDE;

                u16 chunkAboveID = chunkID - Terrain::MAP_CHUNKS_PER_MAP_STRIDE;
                u16 chunkLeftID = chunkID - 1;

                bool hasChunkAbove = map.chunks.find(chunkAboveID) != map.chunks.end();
                bool hasChunkLeft = map.chunks.find(chunkLeftID) != map.chunks.end();

                if (hasChunkAbove)
                {
                    Terrain::Chunk& chunkAbove = map.chunks[chunkAboveID];
                    u32 aboveStartCellID = Terrain::MAP_CELLS_PER_CHUNK - Terrain::MAP_CELLS_PER_CHUNK_SIDE;

                    for (u32 i = 0; i < Terrain::MAP_CELLS_PER_CHUNK_SIDE; i++)
                    {
                        Terrain::Cell& currentCell = chunk.cells[i];
                        Terrain::Cell& aboveCell = chunkAbove.cells[aboveStartCellID + i];

                        // Avoid fixing the very first height value within the cell grid (This is handled by "hasChunkLeft"
                        for (u32 currentHeightID = 1; currentHeightID < Terrain::MAP_CELL_OUTER_GRID_STRIDE; currentHeightID++)
                        {
                            u32 aboveHeightID = currentHeightID + (Terrain::MAP_CELL_TOTAL_GRID_SIZE - Terrain::MAP_CELL_OUTER_GRID_STRIDE);
                            currentCell.heightData[currentHeightID] = aboveCell.heightData[aboveHeightID];
                        }
                    }
                }

                if (hasChunkLeft)
                {
                    Terrain::Chunk& chunkLeft = map.chunks[chunkLeftID];
                    u32 leftStartCellID = Terrain::MAP_CELLS_PER_CHUNK_SIDE - 1;

                    for (u32 i = 0; i < Terrain::MAP_CELLS_PER_CHUNK; i += Terrain::MAP_CELLS_PER_CHUNK_SIDE)
                    {
                        Terrain::Cell& currentCell = chunk.cells[i];
                        Terrain::Cell& leftCell = chunkLeft.cells[leftStartCellID + i];

                        for (u32 currentHeightID = 0; currentHeightID < Terrain::MAP_CELL_TOTAL_GRID_SIZE; currentHeightID += Terrain::MAP_CELL_TOTAL_GRID_STRIDE)
                        {
                            u32 aboveHeightID = currentHeightID + (Terrain::MAP_CELL_OUTER_GRID_STRIDE - 1);
                            currentCell.heightData[currentHeightID] = leftCell.heightData[aboveHeightID];
                        }
                    }
                }
            }
        }

        inline vec2 WorldPositionToADTCoordinates(const vec3& position)
        {
            // This is translated to remap positions [-17066 .. 17066] to [0 ..  34132]
            // This is because we want the Chunk Pos to be between [0 .. 64] and not [-32 .. 32]

            // We have to flip "X" and "Y" here due to 3D -> 2D
            return vec2(Terrain::MAP_HALF_SIZE - position.y, Terrain::MAP_HALF_SIZE - position.x);
        }

        inline vec2 GetChunkFromAdtPosition(const vec2& adtPosition)
        {
            return adtPosition / Terrain::MAP_CHUNK_SIZE;
        }

        inline u32 GetChunkIdFromChunkPos(const vec2& chunkPos)
        {
            return Math::FloorToInt(chunkPos.x) + (Math::FloorToInt(chunkPos.y) * Terrain::MAP_CHUNKS_PER_MAP_STRIDE);
        }

        inline u32 GetCellIdFromCellPos(const vec2& cellPos)
        {
            return Math::FloorToInt(cellPos.x) + (Math::FloorToInt(cellPos.y) * Terrain::MAP_CELLS_PER_CHUNK_SIDE);
        }

        inline f32 Sign(vec2 p1, vec2 p2, vec2 p3)
        {
            return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
        }

        inline bool IsPointInTriangle(vec2 v1, vec2 v2, vec2 v3, vec2 pt)
        {
            float d1, d2, d3;
            bool has_neg, has_pos;

            d1 = Sign(pt, v1, v2);
            d2 = Sign(pt, v2, v3);
            d3 = Sign(pt, v3, v1);

            has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
            has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

            return !(has_neg && has_pos);
        }

        inline ivec3 GetVertexIDsFromPatchPos(const vec2& patchPos, const vec2& patchRemainder, vec2& outB, vec2& outC)
        {
            // This is what our height data looks like
            // 0     1     2     3     4     5     6     7     8
            //    9    10    11    12    13    14    15    16
            // 17    18   19    20    21    22    23    24     25
            //    26    27    28    29    30    31   32    33
            // 34    35    36    37    38    39    40   41     42
            //    43    44    45    46    47    48    49    50
            // 51    52    53    54    55    56    57    58    59
            //    60    61    62    63    64    65    66    67
            // 68    69    70    71    72    73    74    75    76
            //    77    78    79    80    81    82    83    84
            // 85    86    87    88    89    90    91    92    93
            //    94    95    96    97    98    99    100   101
            // 102   103   104   105   106   107   108   109   110
            //    111   112   113   114   115   116   117   118
            // 119   120   121   122   123   124   125   126   127
            //    128   129   130   131   132   133   134   135
            // 136   137   138   139   140   141   142   143   144

            /*
                Triangle 1:
                X = TL
                Y = TR
                Z = C

                Triangle 2:
                X = TL
                Y = C
                Z = BL

                Triangle 3:
                X = BL
                Y = C
                Z = C
            
            */

            // Using patchPos we need to build a square looking something like this depending on what cell we're on
            // TL     TR
            //     C
            // BL     BR
            // TL = TopLeft, TR = TopRight, C = Center, BL = BottomLeft, BR = BottomRight

            u16 topLeftVertex = (Math::FloorToInt(patchPos.y) * Terrain::MAP_CELL_TOTAL_GRID_STRIDE) + Math::FloorToInt(patchPos.x);

            // Top Right is always +1 from Top Left
            u16 topRightVertex = topLeftVertex + 1;

            // Bottom Left is a full rowStride from the Top Left vertex
            u16 bottomLeftVertex = topLeftVertex + Terrain::MAP_CELL_TOTAL_GRID_STRIDE;

            // Bottom Right is always +1 from Bottom Left
            u16 bottomRightVertex = bottomLeftVertex + 1;

            // Center is always + cellStride + 1 from Top Left
            u16 centerVertex = topLeftVertex + Terrain::MAP_CELL_OUTER_GRID_STRIDE;

            // The next step is to use the patchRemainder to figure out which of these triangles we are on: https://imgur.com/i9aHwus
            ivec3 vertexIds = vec3(centerVertex, 0, 0);

            // We swap X, Y here to get the values in ADT Space
            constexpr vec2 topLeft = vec2(0, 0);
            constexpr vec2 topRight = vec2(Terrain::MAP_PATCH_SIZE, 0);
            constexpr vec2 center = vec2(Terrain::MAP_PATCH_HALF_SIZE, Terrain::MAP_PATCH_HALF_SIZE);
            constexpr vec2 bottomLeft = vec2(0, Terrain::MAP_PATCH_SIZE);
            constexpr vec2 bottomRight = vec2(Terrain::MAP_PATCH_SIZE, Terrain::MAP_PATCH_SIZE);

            vec2 patchRemainderPos = patchRemainder * Terrain::MAP_PATCH_SIZE;

            // Check North
            if (IsPointInTriangle(topLeft, topRight, center, patchRemainderPos))
            {
                vertexIds.y = topLeftVertex;
                vertexIds.z = topRightVertex;

                outB = topLeft;
                outC = topRight;
            }
            // Check East
            else if (IsPointInTriangle(topRight, bottomRight, center, patchRemainderPos))
            {
                vertexIds.y = topRightVertex;
                vertexIds.z = bottomRightVertex;

                outB = topRight;
                outC = bottomRight;
            }
            // Check South
            else if (IsPointInTriangle(bottomRight, bottomLeft, center, patchRemainderPos))
            {
                vertexIds.y = bottomRightVertex;
                vertexIds.z = bottomLeftVertex;

                outB = bottomRight;
                outC = bottomLeft;
            }
            // Check West
            else if (IsPointInTriangle(bottomLeft, topLeft, center, patchRemainderPos))
            {
                vertexIds.y = bottomLeftVertex;
                vertexIds.z = topLeftVertex;

                outB = bottomLeft;
                outC = topLeft;
            }

            return vertexIds;
        }
        inline f32 GetHeightFromVertexIds(const ivec3& vertexIds, const f32* heightData, const vec2& a, const vec2& b, const vec2& c, const vec2& p)
        {
            // We do standard barycentric triangle interpolation to get the actual height of the position

            f32 det = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
            f32 factorA = (b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y);
            f32 factorB = (c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y);
            f32 alpha = factorA / det;
            f32 beta = factorB / det;
            f32 gamma = 1.0f - alpha - beta;

            f32 aHeight = heightData[vertexIds.x];
            f32 bHeight = heightData[vertexIds.y];
            f32 cHeight = heightData[vertexIds.z];

            return aHeight * alpha + bHeight * beta + cHeight * gamma;
        }

        inline bool GetTriangleFromWorldPosition(const vec3& position, Geometry::Triangle& triangle, f32& height)
        {
            entt::registry* registry = ServiceLocator::GetGameRegistry();
            MapSingleton& mapSingleton = registry->ctx<MapSingleton>();

            vec2 adtPos = Terrain::MapUtils::WorldPositionToADTCoordinates(position);

            vec2 chunkPos = Terrain::MapUtils::GetChunkFromAdtPosition(adtPos);
            vec2 chunkRemainder = chunkPos - glm::floor(chunkPos);
            u32 chunkId = GetChunkIdFromChunkPos(chunkPos);

            Terrain::Map& currentMap = mapSingleton.GetCurrentMap();
            auto chunkItr = currentMap.chunks.find(chunkId);
            if (chunkItr == currentMap.chunks.end())
                return false;

            Terrain::Chunk& currentChunk = chunkItr->second;

            vec2 cellPos = (chunkRemainder * Terrain::MAP_CHUNK_SIZE) / Terrain::MAP_CELL_SIZE;
            vec2 cellRemainder = cellPos - glm::floor(cellPos);
            u32 cellId = GetCellIdFromCellPos(cellPos);

            vec2 patchPos = (cellRemainder * Terrain::MAP_CELL_SIZE) / Terrain::MAP_PATCH_SIZE;
            vec2 patchRemainder = patchPos - glm::floor(patchPos);

            // NOTE: Order of A, B and C is important, don't swap them around without understanding how it works
            vec2 a = vec2(Terrain::MAP_PATCH_HALF_SIZE, Terrain::MAP_PATCH_HALF_SIZE);
            vec2 b = vec2(0, 0);
            vec2 c = vec2(0, 0);
            
            ivec3 vertexIds = GetVertexIDsFromPatchPos(patchPos, patchRemainder, b, c);

            vec2 chunkWorldPos = glm::floor(chunkPos) * Terrain::MAP_CHUNK_SIZE;
            vec2 cellWorldPos = glm::floor(cellPos) * Terrain::MAP_CELL_SIZE;
            vec2 patchWorldPos = glm::floor(patchPos) * Terrain::MAP_PATCH_SIZE;

            // Below we subtract Terrain::MAP_HALF_SIZE to go from ADT Coordinate back to World Space
            // X, Y here maps to our Y, X
            f32 y = chunkWorldPos.x + cellWorldPos.x + patchWorldPos.x;
            f32 x = chunkWorldPos.y + cellWorldPos.y + patchWorldPos.y;

            // Calculate Vertex A
            {
                triangle.vert1.x = Terrain::MAP_HALF_SIZE - (x + a.y);
                triangle.vert1.y = Terrain::MAP_HALF_SIZE - (y + a.x);
                triangle.vert1.z = currentChunk.cells[cellId].heightData[vertexIds.x];
            }

            // Calculate Vertex B
            {
                triangle.vert2.x = Terrain::MAP_HALF_SIZE - (x + b.y);
                triangle.vert2.y = Terrain::MAP_HALF_SIZE - (y + b.x);
                triangle.vert2.z = currentChunk.cells[cellId].heightData[vertexIds.y];
            }

            // Calculate Vertex C
            {
                triangle.vert3.x = Terrain::MAP_HALF_SIZE - (x + c.y);
                triangle.vert3.y = Terrain::MAP_HALF_SIZE - (y + c.x);
                triangle.vert3.z = currentChunk.cells[cellId].heightData[vertexIds.z];
            }

            height = GetHeightFromVertexIds(vertexIds, &currentChunk.cells[cellId].heightData[0], a, b, c, patchRemainder * Terrain::MAP_PATCH_SIZE);
            return true;
        }
        inline bool IsStandingOnTerrain(const vec3& position, f32& terrainHeight)
        {
            Geometry::Triangle triangle;
            GetTriangleFromWorldPosition(position, triangle, terrainHeight);

            return position.z <= terrainHeight;
        }
        inline std::vector<Geometry::Triangle> GetCellTrianglesFromWorldPosition(const vec3& position)
        {
            entt::registry* registry = ServiceLocator::GetGameRegistry();
            MapSingleton& mapSingleton = registry->ctx<MapSingleton>();

            std::vector<Geometry::Triangle> triangles;
            triangles.reserve(256);

            vec2 adtPos = Terrain::MapUtils::WorldPositionToADTCoordinates(position);

            vec2 chunkPos = Terrain::MapUtils::GetChunkFromAdtPosition(adtPos);
            vec2 chunkRemainder = chunkPos - glm::floor(chunkPos);
            u32 chunkId = GetChunkIdFromChunkPos(chunkPos);

            Terrain::Map& currentMap = mapSingleton.GetCurrentMap();
            auto chunkItr = currentMap.chunks.find(chunkId);
            if (chunkItr == currentMap.chunks.end())
                return triangles;

            Terrain::Chunk& currentChunk = chunkItr->second;

            vec2 cellPos = (chunkRemainder * Terrain::MAP_CHUNK_SIZE) / Terrain::MAP_CELL_SIZE;
            vec2 cellRemainder = cellPos - glm::floor(cellPos);
            u32 cellId = GetCellIdFromCellPos(cellPos);

            vec2 patchPos = (cellRemainder * Terrain::MAP_CELL_SIZE) / Terrain::MAP_PATCH_SIZE;
            vec2 patchRemainder = patchPos - glm::floor(patchPos);
            
            for (u16 x = 0; x < 8; x++)
            {
                for (u16 y = 0; y < 8; y++)
                {
                    for (u16 i = 0; i < 4; i++)
                    {
                        // Default Draw North
                        vec2 remainder = vec2(0, Terrain::MAP_PATCH_HALF_SIZE) / Terrain::MAP_PATCH_SIZE;

                        if (i == 1)
                        {
                            // Draw East
                            remainder = vec2(Terrain::MAP_PATCH_HALF_SIZE, Terrain::MAP_PATCH_SIZE) / Terrain::MAP_PATCH_SIZE;
                        }
                        else if (i == 2)
                        {
                            // Draw South
                            remainder = vec2(Terrain::MAP_PATCH_SIZE, Terrain::MAP_PATCH_HALF_SIZE) / Terrain::MAP_PATCH_SIZE;
                        }
                        else if (i == 3)
                        {
                            // Draw West
                            remainder = vec2(Terrain::MAP_PATCH_HALF_SIZE, Terrain::MAP_PATCH_HALF_SIZE) / Terrain::MAP_PATCH_SIZE;
                        }
                        
                        // NOTE: Order of A, B and C is important, don't swap them around without understanding how it works
                        vec2 a = vec2(Terrain::MAP_PATCH_HALF_SIZE, Terrain::MAP_PATCH_HALF_SIZE);
                        vec2 b = vec2(0, 0);
                        vec2 c = vec2(0, 0);

                        ivec3 vertexIds = GetVertexIDsFromPatchPos(vec2(x, y), remainder, b, c);

                        // X, Y here maps to our Y, X
                        vec2 chunkWorldPos = glm::floor(chunkPos) * Terrain::MAP_CHUNK_SIZE;
                        vec2 cellWorldPos = glm::floor(cellPos) * Terrain::MAP_CELL_SIZE;
                        vec2 patchWorldPos = glm::floor(vec2(x, y)) * Terrain::MAP_PATCH_SIZE;

                        // Below we subtract Terrain::MAP_HALF_SIZE to go from ADT Coordinate back to World Space
                        f32 y = chunkWorldPos.x + cellWorldPos.x + patchWorldPos.x;
                        f32 x = chunkWorldPos.y + cellWorldPos.y + patchWorldPos.y;

                        Geometry::Triangle& triangle = triangles.emplace_back();

                        // Calculate Vertex A
                        {
                            triangle.vert1.x = Terrain::MAP_HALF_SIZE - (x + a.y);
                            triangle.vert1.y = Terrain::MAP_HALF_SIZE - (y + a.x);
                            triangle.vert1.z = currentChunk.cells[cellId].heightData[vertexIds.x];
                        }

                        // Calculate Vertex B
                        {
                            triangle.vert2.x = Terrain::MAP_HALF_SIZE - (x + b.y);
                            triangle.vert2.y = Terrain::MAP_HALF_SIZE - (y + b.x);
                            triangle.vert2.z = currentChunk.cells[cellId].heightData[vertexIds.y];
                        }

                        // Calculate Vertex C
                        {
                            triangle.vert3.x = Terrain::MAP_HALF_SIZE - (x + c.y);
                            triangle.vert3.y = Terrain::MAP_HALF_SIZE - (y + c.x);
                            triangle.vert3.z = currentChunk.cells[cellId].heightData[vertexIds.z];
                        }
                    }
                }
            }

            return triangles;
        }

        inline f32 GetHeightFromWorldPosition(const vec3& position)
        {
            entt::registry* registry = ServiceLocator::GetGameRegistry();
            MapSingleton& mapSingleton = registry->ctx<MapSingleton>();

            vec2 adtPos = Terrain::MapUtils::WorldPositionToADTCoordinates(position);

            vec2 chunkPos = Terrain::MapUtils::GetChunkFromAdtPosition(adtPos);
            vec2 chunkRemainder = chunkPos - glm::floor(chunkPos);
            u32 chunkId = GetChunkIdFromChunkPos(chunkPos);

            Terrain::Map& currentMap = mapSingleton.GetCurrentMap();
            auto chunkItr = currentMap.chunks.find(chunkId);
            if (chunkItr == currentMap.chunks.end())
                return false;

            Terrain::Chunk& currentChunk = chunkItr->second;

            vec2 cellPos = (chunkRemainder * Terrain::MAP_CHUNK_SIZE) / Terrain::MAP_CELL_SIZE;
            vec2 cellRemainder = cellPos - glm::floor(cellPos);
            u32 cellId = GetCellIdFromCellPos(cellPos);

            vec2 patchPos = (cellRemainder * Terrain::MAP_CELL_SIZE) / Terrain::MAP_PATCH_SIZE;
            vec2 patchRemainder = patchPos - glm::floor(patchPos);

            // NOTE: Order of A, B and C is important, don't swap them around without understanding how it works
            vec2 a = vec2(Terrain::MAP_PATCH_HALF_SIZE, Terrain::MAP_PATCH_HALF_SIZE);
            vec2 b = vec2(0, 0);
            vec2 c = vec2(0, 0);

            ivec3 vertexIds = GetVertexIDsFromPatchPos(patchPos, patchRemainder, b, c);

            return GetHeightFromVertexIds(vertexIds, &currentChunk.cells[cellId].heightData[0], a, b, c, patchRemainder * Terrain::MAP_PATCH_SIZE);
        }
    }
}
