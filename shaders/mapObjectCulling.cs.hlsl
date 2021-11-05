permutation DETERMINISTIC_ORDER = [0, 1];

#include "common.inc.hlsl"
#include "cullingUtils.inc.hlsl"
#include "globalData.inc.hlsl"
#include "pyramidCulling.inc.hlsl"
#include "mapObject.inc.hlsl"

struct Constants
{
	float4 frustumPlanes[6];
    float3 cameraPosition;
    uint maxDrawCount;
    uint occlusionCull;
};

struct PackedCullingData
{
    uint packed0; // half center.x, half center.y, 
    uint packed1; // half center.z, half extents.x,  
    uint packed2; // half extents.y, half extents.z, 
    float sphereRadius;
}; // 16 bytes

struct CullingData
{
    AABB boundingBox;
    float sphereRadius;
};

[[vk::binding(5, MAPOBJECT)]] StructuredBuffer<Draw> _draws;
[[vk::binding(6, MAPOBJECT)]] RWStructuredBuffer<Draw> _culledDraws;
[[vk::binding(7, MAPOBJECT)]] RWByteAddressBuffer _drawCount;
[[vk::binding(8, MAPOBJECT)]] RWByteAddressBuffer _triangleCount;

[[vk::binding(9, MAPOBJECT)]] StructuredBuffer<PackedCullingData> _packedCullingData;
//[[vk::binding(6, MAPOBJECT)]] StructuredBuffer<InstanceData> _instanceData;

[[vk::binding(10, MAPOBJECT)]] ConstantBuffer<Constants> _constants;

[[vk::binding(11, MAPOBJECT)]] SamplerState _depthSampler;
[[vk::binding(12, MAPOBJECT)]] Texture2D<float> _depthPyramid;

#if DETERMINISTIC_ORDER
[[vk::binding(13, MAPOBJECT)]] RWStructuredBuffer<uint64_t> _sortKeys;
[[vk::binding(14, MAPOBJECT)]] RWStructuredBuffer<uint> _sortValues;
#endif // DETERMINISTIC_ORDER

CullingData LoadCullingData(uint instanceIndex)
{
    const PackedCullingData packed = _packedCullingData[instanceIndex];
    CullingData cullingData;

    cullingData.boundingBox.min.x = f16tof32(packed.packed0 & 0xffff);
    cullingData.boundingBox.min.y = f16tof32((packed.packed0 >> 16) & 0xffff);
    cullingData.boundingBox.min.z = f16tof32(packed.packed1 & 0xffff);
    
    cullingData.boundingBox.max.x = f16tof32((packed.packed1 >> 16) & 0xffff);
    cullingData.boundingBox.max.y = f16tof32(packed.packed2 & 0xffff);
    cullingData.boundingBox.max.z = f16tof32((packed.packed2 >> 16) & 0xffff);
    
    cullingData.sphereRadius = packed.sphereRadius;
    
    return cullingData;
}

bool IsAABBInsideFrustum(float4 frustum[6], AABB aabb)
{
    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        const float4 plane = frustum[i];

        float3 p;

        // X axis 
        if (plane.x > 0)
        {
            p.x = aabb.max.x;
        }
        else
        {
            p.x = aabb.min.x;
        }

        // Y axis 
        if (plane.y > 0)
        {
            p.y = aabb.max.y;
        }
        else
        {
            p.y = aabb.min.y;
        }

        // Z axis 
        if (plane.z > 0)
        {
            p.z = aabb.max.z;
        }
        else
        {
            p.z = aabb.min.z;
        }

        if (dot(plane.xyz, p) + plane.w <= 0)
        {
            return false;
        }
    }

    return true;
}

[numthreads(32, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= _constants.maxDrawCount)
    {
        return;   
    }
    
    const uint drawIndex = dispatchThreadId.x;
    
    Draw draw = _draws[drawIndex];
    uint instanceID = draw.firstInstance;
    
    const InstanceLookupData lookupData = LoadInstanceLookupData(instanceID);
    
    const CullingData cullingData = LoadCullingData(lookupData.cullingDataID);
    const InstanceData instanceData = _mapObjectInstanceData[lookupData.instanceID];
    
    // Get center and extents (Center is stored in min & Extents is stored in max)
    float3 center = cullingData.boundingBox.min;
    float3 extents = cullingData.boundingBox.max;
    
    // Transform center
    const float4x4 m = instanceData.instanceMatrix;
    float3 transformedCenter = mul(float4(center, 1.0f), m).xyz;
    
    // Transform extents (take maximum)
    const float3x3 absMatrix = float3x3(abs(m[0].xyz), abs(m[1].xyz), abs(m[2].xyz));
    float3 transformedExtents = mul(extents, absMatrix);
    
    // Transform to min/max AABB representation
    AABB aabb;
    aabb.min = transformedCenter - transformedExtents;
    aabb.max = transformedCenter + transformedExtents;
    
    // Cull the AABB
    if (!IsAABBInsideFrustum(_constants.frustumPlanes, aabb))
    {
        return;
    }
    if (_constants.occlusionCull)
    { 
        float4x4 mvp = mul(_viewData.viewProjectionMatrix, m);
        bool isIntersectingNearZ = IsIntersectingNearZ(aabb.min, aabb.max, mvp);

        if (!isIntersectingNearZ && !IsVisible(aabb.min, aabb.max, _viewData.eyePosition.xyz, _depthPyramid, _depthSampler, _viewData.lastViewProjectionMatrix))
        { 
            return;
        }
    }

    uint outTriangles;
    _triangleCount.InterlockedAdd(0, draw.indexCount/3, outTriangles);

    uint outIndex;
	_drawCount.InterlockedAdd(0, 1, outIndex);
    
	_culledDraws[outIndex] = draw;

#if DETERMINISTIC_ORDER
    // We want to set up sort keys and values so we can sort our drawcalls by firstInstance
    _sortKeys[outIndex] = draw.firstInstance;
    _sortValues[outIndex] = outIndex;
#endif // DETERMINISTIC_ORDER
}
