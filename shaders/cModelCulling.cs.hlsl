permutation PREPARE_SORT = [0, 1];
permutation USE_BITMASKS = [0, 1];
#include "common.inc.hlsl"
#include "cullingUtils.inc.hlsl"
#include "globalData.inc.hlsl"
#include "cModel.inc.hlsl"
#include "pyramidCulling.inc.hlsl"

struct Constants
{
	float4 frustumPlanes[6];
    float3 cameraPosition;   
    uint maxDrawCount;
    uint occlusionCull;
};

struct PackedCullingData
{
    uint data0; // half center.x, half center.y, 
    uint data1; // half center.z, half extents.x,  
    uint data2; // half extents.y, half extents.z, 
    float sphereRadius;
}; // 16 bytes

struct CullingData
{
    AABB boundingBox;
    float sphereRadius;
};

// Inputs
[[vk::push_constant]] Constants _constants;
[[vk::binding(4, CMODEL)]] StructuredBuffer<Draw> _drawCalls;
[[vk::binding(5, CMODEL)]] StructuredBuffer<PackedCullingData> _cullingDatas;
[[vk::binding(6, CMODEL)]] SamplerState _depthSampler;
[[vk::binding(7, CMODEL)]] Texture2D<float> _depthPyramid;

#if USE_BITMASKS
[[vk::binding(8, CMODEL)]] StructuredBuffer<uint> _prevCulledDrawCallBitMask;

// Outputs
[[vk::binding(9, CMODEL)]] RWStructuredBuffer<uint> _culledDrawCallBitMask;
#endif

[[vk::binding(10, CMODEL)]] RWByteAddressBuffer _drawCount;
[[vk::binding(11, CMODEL)]] RWByteAddressBuffer _triangleCount;
[[vk::binding(12, CMODEL)]] RWStructuredBuffer<Draw> _culledDrawCalls;
[[vk::binding(13, CMODEL)]] RWByteAddressBuffer _visibleInstanceMask;
#if PREPARE_SORT
[[vk::binding(14, CMODEL)]] RWStructuredBuffer<uint64_t> _sortKeys; // OPTIONAL, only needed if _constants.shouldPrepareSort
[[vk::binding(15, CMODEL)]] RWStructuredBuffer<uint> _sortValues; // OPTIONAL, only needed if _constants.shouldPrepareSort
#endif

CullingData LoadCullingData(uint instanceIndex)
{
    PackedCullingData packed = _cullingDatas[instanceIndex];
    CullingData cullingData;

    cullingData.boundingBox.min.x = f16tof32(packed.data0);
    cullingData.boundingBox.min.y = f16tof32(packed.data0 >> 16);
    cullingData.boundingBox.min.z = f16tof32(packed.data1);
    
    cullingData.boundingBox.max.x = f16tof32(packed.data1 >> 16);
    cullingData.boundingBox.max.y = f16tof32(packed.data2);
    cullingData.boundingBox.max.z = f16tof32(packed.data2 >> 16);
    
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

#define UINT_MAX 0xFFFFu
uint64_t CalculateSortKey(Draw drawCall, CModelDrawCallData drawCallData, float4x4 instanceMatrix)
{
    // Get the position to sort against
    const float3 refPos = _constants.cameraPosition;
    const float3 position = float3(instanceMatrix._41, instanceMatrix._42, instanceMatrix._43);
    const float distanceFromCamera = distance(refPos, position);
    const float distanceAccuracy = 0.01f;
    
    // We want to construct a 64 bit sorting key, it will look like this but we can't make a union:
    /*
        struct SortingKey
        {
            uint8_t renderPriority : 8;
            
            uint8_t padding : 8; // Use this if we need extra precision on something
    
            uint32_t invDistanceFromCamera : 32; // This is converted to a fixed decimal value based on distance from camera, since the bit format of floats would mess with our comparison
            uint16_t localInstanceID : 16; // This makes the sorting stable if the distance is the same (submeshes inside a mesh)
        };
    */
    
    uint invDistanceFromCameraUint = UINT_MAX - (uint)(distanceFromCamera / distanceAccuracy);
    uint localInstanceID = drawCall.firstInstance % 65535;
    
    uint64_t sortKey = 0;
    
    // Padding here
    sortKey |= invDistanceFromCameraUint << 16;
    sortKey |= localInstanceID;
    
    return sortKey;
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
    if (input.dispatchThreadID.x >= _constants.maxDrawCount)
    {
        return;
    }
    
    // Load DrawCall
    const uint drawCallIndex = input.dispatchThreadID.x;
    
    Draw drawCall = _drawCalls[drawCallIndex];
    
    uint drawCallID = drawCall.firstInstance;
    CModelDrawCallData drawCallData = LoadCModelDrawCallData(drawCallID);
    
    const CModelInstanceData instance = _cModelInstanceDatas[drawCallData.instanceID];
    const CullingData cullingData = LoadCullingData(instance.modelID);
    
    float4x4 instanceMatrix = _cModelInstanceMatrices[drawCallData.instanceID];
    
    // Get center and extents (Center is stored in min & Extents is stored in max)
    float3 center = cullingData.boundingBox.min;
    float3 extents = cullingData.boundingBox.max;
    
    // Transform center
    const float4x4 m = instanceMatrix;
    float3 transformedCenter = mul(float4(center, 1.0f), m).xyz;
    
    // Transform extents (take maximum)
    const float3x3 absMatrix = float3x3(abs(m[0].xyz), abs(m[1].xyz), abs(m[2].xyz));
    float3 transformedExtents =  mul(extents, absMatrix);
    
    // Transform to min/max AABB representation
    AABB aabb;
    aabb.min = transformedCenter - transformedExtents;
    aabb.max = transformedCenter + transformedExtents;
    
    // Cull the AABB
    bool isVisible = true;
    if (!IsAABBInsideFrustum(_constants.frustumPlanes, aabb))
    {
        isVisible = false;
    }
    else if (_constants.occlusionCull)
    {
        float4x4 mvp = mul(_viewData.viewProjectionMatrix, m);
        bool isIntersectingNearZ = IsIntersectingNearZ(aabb.min, aabb.max, mvp);

        if (!isIntersectingNearZ && !IsVisible(aabb.min, aabb.max, _viewData.eyePosition.xyz, _depthPyramid, _depthSampler, _viewData.viewProjectionMatrix))
        {
            isVisible = false;
        }
    }

#if USE_BITMASKS
    uint bitMask = WaveActiveBallot(isVisible).x;

    // The first thread writes the bitmask
    if (input.groupThreadID.x == 0)
    {
        _culledDrawCallBitMask[input.groupID.x] = bitMask;
    }

    uint occluderBitMask = _prevCulledDrawCallBitMask[input.groupID.x];
    uint renderBitMask = bitMask &~occluderBitMask; // This should give us all currently visible objects that were not occluders

    bool shouldRender = renderBitMask & (1u << input.groupThreadID.x);
#else
    bool shouldRender = isVisible;
#endif

    if (isVisible)
    {
        const uint maskOffset = drawCallData.instanceID / 32;
        const uint mask = (uint)1 << (drawCallData.instanceID % 32);
        _visibleInstanceMask.InterlockedOr(maskOffset * SIZEOF_UINT, mask);
    }
    
    if (shouldRender)
    {
        // Update triangle count
        uint outTriangles;
        _triangleCount.InterlockedAdd(0, drawCall.indexCount / 3, outTriangles);

        // Store DrawCall
        uint outIndex;
        _drawCount.InterlockedAdd(0, 1, outIndex);
        _culledDrawCalls[outIndex] = drawCall;

        //uint visibleInstanceIndex;
        //_visibleInstanceCount.InterlockedAdd(0, 1, visibleInstanceIndex);
        //_visibleInstanceIndices[visibleInstanceIndex] = drawCallData.instanceID;

        // If we expect to sort afterwards, output the data needed for it
#if PREPARE_SORT
        _sortKeys[outIndex] = CalculateSortKey(drawCall, drawCallData, instanceMatrix);
        _sortValues[outIndex] = outIndex;
#endif
    }
}