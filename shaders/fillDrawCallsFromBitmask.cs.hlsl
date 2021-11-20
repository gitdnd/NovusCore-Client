#include "common.inc.hlsl"
#include "cullingUtils.inc.hlsl"
#include "globalData.inc.hlsl"

#include "pyramidCulling.inc.hlsl"

struct Constants
{
    uint numTotalDraws;
};

[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<Draw> _draws;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _culledDrawCallsBitMask;

[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<Draw> _culledDraws;
[[vk::binding(3, PER_PASS)]] RWByteAddressBuffer _drawCount;
[[vk::binding(4, PER_PASS)]] RWByteAddressBuffer _triangleCount;

struct CSInput
{
    uint3 dispatchThreadId : SV_DispatchThreadID;
    uint3 groupID : SV_GroupID;
    uint3 groupThreadID : SV_GroupThreadID;
};

[numthreads(32, 1, 1)]
void main(CSInput input)
{
    uint index = input.dispatchThreadId.x;

    if (index >= _constants.numTotalDraws)
        return;

    uint bitMask = _culledDrawCallsBitMask[input.groupID.x];
    uint bitIndex = input.groupThreadID.x;

    if (bitMask & (1u << bitIndex))
    {
        Draw draw = _draws[index];

        uint outTriangles;
        _triangleCount.InterlockedAdd(0, draw.indexCount / 3, outTriangles);

        uint outIndex;
        _drawCount.InterlockedAdd(0, 1, outIndex);

        _culledDraws[outIndex] = draw;
    }
}
