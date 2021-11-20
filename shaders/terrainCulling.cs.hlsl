#include "common.inc.hlsl"
#include "cullingUtils.inc.hlsl"
#include "globalData.inc.hlsl"
#include "terrain.inc.hlsl"

#include "pyramidCulling.inc.hlsl"

#define USE_PACKED_HEIGHT_RANGE 1

struct Constants
{
    float4 frustumPlanes[6];
    float4x4 viewmat;
    uint occlusionCull;
};

[[vk::push_constant]] Constants _constants;
[[vk::binding(0, TERRAIN)]] StructuredBuffer<CellInstance> _instances;
[[vk::binding(1, TERRAIN)]] StructuredBuffer<uint> _heightRanges;
[[vk::binding(2, TERRAIN)]] StructuredBuffer<uint> _prevCulledInstancesBitMask;
[[vk::binding(3, TERRAIN)]] RWStructuredBuffer<uint> _culledInstancesBitMask;

[[vk::binding(4, TERRAIN)]] RWStructuredBuffer<CellInstance> _culledInstances;
[[vk::binding(5, TERRAIN)]] RWByteAddressBuffer _drawCount;

[[vk::binding(6, TERRAIN)]] SamplerState _depthSampler;
[[vk::binding(7, TERRAIN)]] Texture2D<float> _depthPyramid;

float2 ReadHeightRange(uint instanceIndex)
{
#if USE_PACKED_HEIGHT_RANGE
	const uint packed = _heightRanges[instanceIndex];
	const float min = f16tof32(packed >> 16);
	const float max = f16tof32(packed);
	return float2(min, max);
#else
    const float2 minmax = asfloat(_heightRanges.Load2(instanceIndex * 8));
    return minmax;
#endif
}

bool IsAABBInsideFrustum(float4 frustum[6], AABB aabb)
{
    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        const float4 plane = frustum[i];

        float3 vmin;

        // X axis 
        if (plane.x > 0)
        {
            vmin.x = aabb.min.x;
        }
        else
        {
            vmin.x = aabb.max.x;
        }

        // Y axis 
        if (plane.y > 0)
        {
            vmin.y = aabb.min.y;
        }
        else
        {
            vmin.y = aabb.max.y;
        }

        // Z axis 
        if (plane.z > 0)
        {
            vmin.z = aabb.min.z;
        }
        else
        {
            vmin.z = aabb.max.z;
        }

        if (dot(plane.xyz, vmin) + plane.w <= 0)
        {
            return false;
        }
    }

    return true;
}

struct CSInput
{
    uint3 dispatchThreadID : SV_DispatchThreadID;
    uint3 groupID : SV_GroupID;
    uint3 groupThreadID : SV_GroupThreadID;
};

[numthreads(32, 1, 1)]
void main(CSInput input)
{
    const uint instanceIndex = input.dispatchThreadID.x;
	CellInstance instance = _instances[instanceIndex];

    const uint cellID = instance.packedChunkCellID & 0xffff;
    const uint chunkID = instance.packedChunkCellID >> 16;

    const float2 heightRange = ReadHeightRange(instanceIndex);
    AABB aabb = GetCellAABB(chunkID, cellID, heightRange);
    
    bool isVisible = true;
    if (!IsAABBInsideFrustum(_constants.frustumPlanes, aabb))
    {
        isVisible = false;
    }
    else if (_constants.occlusionCull)
    {
        bool isIntersectingNearZ = IsIntersectingNearZ(aabb.min, aabb.max, _viewData.viewProjectionMatrix);

        if (!isIntersectingNearZ && !IsVisible(aabb.min, aabb.max, _viewData.eyePosition.xyz, _depthPyramid, _depthSampler, _viewData.viewProjectionMatrix))
        {
            isVisible = false;
        }
    }

    uint bitMask = WaveActiveBallot(isVisible).x;

    // The first thread writes the bitmask
    if (input.groupThreadID.x == 0)
    {
        _culledInstancesBitMask[input.groupID.x] = bitMask;
    }

    uint occluderBitMask = _prevCulledInstancesBitMask[input.groupID.x];
    uint renderBitMask = bitMask & ~occluderBitMask; // This should give us all currently visible objects that were not occluders

    // We only want to render objects that are visible and not occluders since they were already rendered this frame
    bool shouldRender = renderBitMask & (1u << input.groupThreadID.x);
    if (shouldRender)
    {
        uint culledInstanceIndex;
        _drawCount.InterlockedAdd(4, 1, culledInstanceIndex);

        uint firstInstanceOffset = _drawCount.Load(16);
        _culledInstances[firstInstanceOffset + culledInstanceIndex] = _instances[instanceIndex];
    }
}
