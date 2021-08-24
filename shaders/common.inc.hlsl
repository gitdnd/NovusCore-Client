#ifndef COMMON_INCLUDED
#define COMMON_INCLUDED

#define SIZEOF_UINT  4
#define SIZEOF_UINT2 8
#define SIZEOF_UINT3 12
#define SIZEOF_UINT4 16
#define SIZEOF_DRAW_INDIRECT_ARGUMENTS 16

enum ObjectType : uint
{
    Terrain = 1,
    MapObject = 2,
    CModelOpaque = 3,
    CModelTransparent = 4
};

float2 OctWrap(float2 v)
{
    return (1.0 - abs(v.yx)) * (v.xy >= 0.0 ? 1.0 : -1.0);
}

float2 OctNormalEncode(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
    n.xy = n.xy * 0.5 + 0.5;
    return n.xy;
}

float3 OctNormalDecode(float2 f)
{
    f = f * 2.0 - 1.0;

    // https://twitter.com/Stubbesaurus/status/937994790553227264
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += n.xy >= 0.0 ? -t : t;
    return normalize(n);
}

float4 ToFloat4(int input, float defaultAlphaVal)
{
    return float4(input, 0, 0, defaultAlphaVal);
}

float4 ToFloat4(int2 input, float defaultAlphaVal)
{
    return float4(input, 0, defaultAlphaVal);
}

float4 ToFloat4(int3 input, float defaultAlphaVal)
{
    return float4(input, defaultAlphaVal);
}

float4 ToFloat4(int4 input, float defaultAlphaVal)
{
    return float4(input);
}

float4 ToFloat4(uint input, float defaultAlphaVal)
{
    return float4(input, 0, 0, defaultAlphaVal);
}

float4 ToFloat4(uint2 input, float defaultAlphaVal)
{
    return float4(input, 0, defaultAlphaVal);
}

float4 ToFloat4(uint3 input, float defaultAlphaVal)
{
    return float4(input, defaultAlphaVal);
}

float4 ToFloat4(uint4 input, float defaultAlphaVal)
{
    return float4(input);
}

float4 ToFloat4(float input, float defaultAlphaVal)
{
    return float4(input, 0, 0, defaultAlphaVal);
}

float4 ToFloat4(float2 input, float defaultAlphaVal)
{
    return float4(input, 0, defaultAlphaVal);
}

float4 ToFloat4(float3 input, float defaultAlphaVal)
{
    return float4(input, defaultAlphaVal);
}

float4 ToFloat4(float4 input, float defaultAlphaVal)
{
    return input;
}

struct Draw
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    uint vertexOffset;
    uint firstInstance;
};

uint3 GetVertexIDs(uint triangleID, Draw draw, StructuredBuffer<uint> indexBuffer)
{
    uint localIndexID = triangleID * 3;

    uint globalIndexID = draw.firstIndex + localIndexID; // This always points to the index of the first vertex in the triangle

    uint3 vertexIDs;
    [unroll]
    for (uint i = 0; i < 3; i++)
    {
        // Our index buffer is made up of uint16_t's, but hardware support for accessing them is kinda bad.
        // So instead we're going to access them as uints, and then treat them as packed pairs of uint16_t's
        uint indexToLoad = globalIndexID + i;
        uint indexPairToLoad = (indexToLoad) / 2;

        uint localVertexIDPair = indexBuffer[indexPairToLoad];

        bool isOdd = indexToLoad % 2 == 1;
        vertexIDs[i] = draw.vertexOffset + ((localVertexIDPair >> (16 * isOdd)) & 0xFFFF);
    }

    return vertexIDs;
}
#endif // COMMON_INCLUDED