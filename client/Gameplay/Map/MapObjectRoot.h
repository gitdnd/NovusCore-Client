/*
    MIT License

    Copyright (c) 2018-2020 NovusCore

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

namespace Terrain
{
    constexpr i32 MAP_OBJECT_ROOT_TOKEN = 1852665714; // UTF8 -> Binary -> Decimal for "nmor"
    constexpr i32 MAP_OBJECT_ROOT_VERSION = 2;

#pragma pack(push, 1)
    struct MapObjectRootHeader
    {
        u32 token = 0;
        u32 version = 0;
    };

    struct MapObjectMaterialFlags
    {
        union
        {
            u32 flags;
            struct
            {
                bool unlit : 1;
                bool unfogged : 1;
                bool unculled : 1;
                bool extLight : 1;
                bool sidn : 1;
                bool window : 1;
                bool clampS : 1;
                bool clampT : 1;
                bool unknown : 1;
            };
        };
    };
    static_assert(sizeof(MapObjectMaterialFlags) == 4); // This should always be the size of one u32

    constexpr u32 INVALID_TEXTURE_ID = std::numeric_limits<u32>().max();
    struct MapObjectMaterial
    {
        u16 materialType;
        u16 transparencyMode;
        MapObjectMaterialFlags flags;
        u32 textureNameID[3] = { INVALID_TEXTURE_ID, INVALID_TEXTURE_ID, INVALID_TEXTURE_ID };
    };

    struct MapObjectRoot
    {
        MapObjectRootHeader header;
        std::vector<MapObjectMaterial> materials;
        u32 numMapObjects;
    };
#pragma pack(pop)
}