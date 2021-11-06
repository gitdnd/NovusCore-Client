#include "cModel.inc.hlsl"
#include "cModelAnimation.inc.hlsl"

struct Constants
{
    uint numInstances;
    float deltaTime;
};

// Inputs
[[vk::push_constant]] Constants _constants;
[[vk::binding(4, CMODEL)]] ByteAddressBuffer _visibleInstanceCount;
[[vk::binding(5, CMODEL)]] StructuredBuffer<uint> _visibleInstanceIndices;
[[vk::binding(6, CMODEL)]] StructuredBuffer<AnimationSequence> _animationSequences;
[[vk::binding(7, CMODEL)]] StructuredBuffer<AnimationModelInfo> _animationModelInfos;
[[vk::binding(8, CMODEL)]] StructuredBuffer<AnimationBoneInfo> _animationBoneInfos;
[[vk::binding(9, CMODEL)]] RWStructuredBuffer<float4x4> _animationBoneDeformMatrices;
[[vk::binding(10, CMODEL)]] RWStructuredBuffer<AnimationBoneInstanceData> _animationBoneInstances;
[[vk::binding(11, CMODEL)]] StructuredBuffer<AnimationTrackInfo> _animationTrackInfos;
[[vk::binding(12, CMODEL)]] StructuredBuffer<uint> _animationTrackTimestamps;
[[vk::binding(13, CMODEL)]] StructuredBuffer<float4> _animationTrackValues;

AnimationState GetAnimationState(AnimationBoneInstanceData boneInstanceData)
{
    AnimationState state;
    state.animationProgress = boneInstanceData.animationProgress;

    return state;
}

[numthreads(32, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint visibleInstanceCount = _visibleInstanceCount.Load(0);
    if (dispatchThreadId.x >= visibleInstanceCount)
    {
        return;
    }

    const uint instanceID = _visibleInstanceIndices[dispatchThreadId.x];

    CModelInstanceData instanceData = _cModelInstanceDatas[instanceID];
    const AnimationModelInfo modelInfo = _animationModelInfos[instanceData.modelID];

    int numSequences = modelInfo.packedData0 & 0xFFFF;
    int numBones = (modelInfo.packedData0 >> 16) & 0xFFFF;

    if (numSequences == 0)
        return;

    for (int i = 0; i < numBones; i++)
    {
        AnimationBoneInstanceData boneInstance = _animationBoneInstances[instanceData.boneInstanceDataOffset + i];
        AnimationBoneInfo boneInfo = _animationBoneInfos[modelInfo.boneInfoOffset + i];
        uint parentBoneId = (boneInfo.packedData1 >> 16) & 0xFFFF;

        uint sequenceIndex = boneInstance.packedData0 & 0xFFFF;
        AnimationSequence sequence = _animationSequences[modelInfo.sequenceOffset + sequenceIndex];

        if (boneInstance.animateState != 0)
        {
            boneInstance.animationProgress += 1.f * _constants.deltaTime;

            if (boneInstance.animationProgress >= sequence.duration)
            {
                uint isLooping = boneInstance.animateState == 2;

                if (isLooping)
                {
                    boneInstance.animateState = 2;
                    boneInstance.animationProgress -= sequence.duration;
                }
                else
                {
                    boneInstance.animateState = 0;
                    boneInstance.animationProgress = sequence.duration - 0.01f;
                }
            }

            _animationBoneInstances[instanceData.boneInstanceDataOffset + i] = boneInstance;
        }

        const AnimationState state = GetAnimationState(boneInstance);
        float4x4 parentBoneMatrix = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        float4x4 currBoneMatrix = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        float3 parentPivotPoint = float3(0.f, 0.f, 0.f);

        if (parentBoneId != 65535)
        {
            parentBoneMatrix = _animationBoneDeformMatrices[instanceData.boneDeformOffset + parentBoneId];

            AnimationBoneInfo parentBoneInfo = _animationBoneInfos[modelInfo.boneInfoOffset + parentBoneId];
            parentPivotPoint = float3(parentBoneInfo.pivotPointX, parentBoneInfo.pivotPointY, parentBoneInfo.pivotPointZ);
        }

        if ((boneInfo.flags & 1) != 0)
        {
            AnimationContext ctx;
            ctx.activeSequenceId = sequenceIndex;
            ctx.animationTrackInfos = _animationTrackInfos;
            ctx.trackTimestamps = _animationTrackTimestamps;
            ctx.trackValues = _animationTrackValues;
            ctx.boneInfo = boneInfo;
            ctx.state = state;

            currBoneMatrix = GetBoneMatrix(ctx);
            currBoneMatrix = mul(currBoneMatrix, parentBoneMatrix);

            //DebugRenderBone(instanceData, modelBoneInfo.offset + i, float3(boneInfo.pivotPointX, boneInfo.pivotPointY, boneInfo.pivotPointZ), currBoneMatrix, modelBoneInfo.offset + parentBoneId, parentPivotPoint, parentBoneMatrix, true);
            _animationBoneDeformMatrices[instanceData.boneDeformOffset + i] = currBoneMatrix;
        }
        else
        {
            _animationBoneDeformMatrices[instanceData.boneDeformOffset + i] = parentBoneMatrix;
        }
    }
}