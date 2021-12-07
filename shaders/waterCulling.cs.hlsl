#include "common.inc.hlsl"
#include "cullingUtils.inc.hlsl"
#include "globalData.inc.hlsl"
#include "pyramidCulling.inc.hlsl"

struct Constants
{
    float4 frustumPlanes[6];
    float3 cameraPosition;
    uint maxDrawCount;
    uint occlusionCull;
};

struct WaterAABB
{
    float4 min;
    float4 max;
};

// Inputs
[[vk::push_constant]] Constants _constants;
[[vk::binding(0, PER_PASS)]] StructuredBuffer<Draw> _drawCalls;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<WaterAABB> _boundingBoxes;
[[vk::binding(2, PER_PASS)]] SamplerState _depthSampler;
[[vk::binding(3, PER_PASS)]] Texture2D<float> _depthPyramid;

// Outputs
[[vk::binding(4, PER_PASS)]] RWStructuredBuffer<Draw> _culledDrawCalls;
[[vk::binding(5, PER_PASS)]] RWByteAddressBuffer _drawCount;
[[vk::binding(6, PER_PASS)]] RWByteAddressBuffer _triangleCount;

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

    WaterAABB waterAABB = _boundingBoxes[drawCallID];

    AABB aabb;
    aabb.min = waterAABB.min.xyz;
    aabb.max = waterAABB.max.xyz;

    // Cull the AABB
    if (!IsAABBInsideFrustum(_constants.frustumPlanes, aabb))
    {
        return;
    }
    else if (_constants.occlusionCull)
    {
        bool isIntersectingNearZ = IsIntersectingNearZ(aabb.min, aabb.max, _viewData.viewProjectionMatrix);

        if (!isIntersectingNearZ && !IsVisible(aabb.min, aabb.max, _viewData.eyePosition.xyz, _depthPyramid, _depthSampler, _viewData.viewProjectionMatrix))
        {
            return;
        }
    }

    // Update triangle count
    uint outTriangles;
    _triangleCount.InterlockedAdd(0, drawCall.indexCount / 3, outTriangles);

    // Store DrawCall
    uint outIndex;
    _drawCount.InterlockedAdd(0, 1, outIndex);
    _culledDrawCalls[outIndex] = drawCall;
}