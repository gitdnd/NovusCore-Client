
struct DrawCall
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    uint vertexOffset;
    uint firstInstance;
};

[[vk::binding(0, MAPOBJECT)]] StructuredBuffer<uint> _sortValues;

[[vk::binding(1, MAPOBJECT)]] StructuredBuffer<uint> _culledDrawCount;
[[vk::binding(2, MAPOBJECT)]] StructuredBuffer<DrawCall> _culledDrawCalls;
[[vk::binding(3, MAPOBJECT)]] RWStructuredBuffer<DrawCall> _sortedCulledDrawCalls;

[numthreads(32, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint sortIndex = dispatchThreadId.x;
    uint drawCount = _culledDrawCount[0];
    
    if (sortIndex >= drawCount)
    {
        return;
    }
    
    uint drawCallSortedIndex = _sortValues[sortIndex];
    _sortedCulledDrawCalls[sortIndex] = _culledDrawCalls[drawCallSortedIndex];
}