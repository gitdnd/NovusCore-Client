struct AnimationState
{
    float animationProgress;
};

struct AnimationContext
{
    uint activeSequenceId;
    AnimationState state;
    AnimationBoneInfo boneInfo;

    StructuredBuffer<AnimationTrackInfo> animationTrackInfos;
    StructuredBuffer<uint> trackTimestamps;
    StructuredBuffer<float4> trackValues;
};

float4x4 MatrixTranslate(float3 v)
{
    float4x4 result = { 1, 0, 0, 0,
                        0, 1, 0, 0,
                        0, 0, 1, 0,
                        v[0], v[1], v[2], 1 };
    return result;
}

float4x4 MatrixScale(float3 v)
{
    float4x4 result = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    result[0] = result[0] * v[0];
    result[1] = result[1] * v[1];
    result[2] = result[2] * v[2];

    return result;
}

float4x4 RotationToMatrix(float4 quat)
{
    float x2 = quat.x * quat.x;
    float y2 = quat.y * quat.y;
    float z2 = quat.z * quat.z;
    float sx = quat.w * quat.x;
    float sy = quat.w * quat.y;
    float sz = quat.w * quat.z;
    float xz = quat.x * quat.z;
    float yz = quat.y * quat.z;
    float xy = quat.x * quat.y;

    return float4x4(1 - 2 * (y2 + z2), 2 * (xy + sz), 2 * (xz - sy), 0.0f,
        2 * (xy - sz), 1 - 2 * (x2 + z2), 2 * (sx + yz), 0.0f,
        2 * (sy + xz), 2 * (yz - sx), 1 - 2 * (x2 + y2), 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);
}

float4 slerp(float4 a, float4 b, float t)
{
    const float l2 = dot(a, b);
    if (l2 < 0.0f)
    {
        b = float4(-b.x, -b.y, -b.z, -b.w);
    }

    float4 c;
    c.x = a.x - t * (a.x - b.x);
    c.y = a.y - t * (a.y - b.y);
    c.z = a.z - t * (a.z - b.z);
    c.w = a.w - t * (a.w - b.w);

    return normalize(c);
}

float4x4 GetBoneMatrix(AnimationContext ctx)
{
    const AnimationState state = ctx.state;
    const AnimationBoneInfo boneInfo = ctx.boneInfo;

    const uint sequenceIndex = ctx.activeSequenceId;
    const uint numTranslationSequences = boneInfo.packedData0 & 0xFFFF;
    const uint numRotationSequences = (boneInfo.packedData0 >> 16) & 0xFFFF;
    const uint numScaleSequences = boneInfo.packedData1 & 0xFFFF;

    const float3 pivotPoint = float3(boneInfo.pivotPointX, boneInfo.pivotPointY, boneInfo.pivotPointZ);

    float4x4 boneMatrix = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    float4 translationValue = float4(0.f, 0.f, 0.f, 0.f);
    float4 rotationValue = float4(0.f, 0.f, 0.f, 1.f);
    float4 scaleValue = float4(1.f, 1.f, 1.f, 0.f);

    if (sequenceIndex != 65535)
    {
        if (sequenceIndex < numScaleSequences)
        {
            AnimationTrackInfo trackInfo = ctx.animationTrackInfos[boneInfo.scaleTrackOffset + sequenceIndex];

            uint numTimestamps = trackInfo.packedData0 & 0xFFFF;
            uint numValues = (trackInfo.packedData0 >> 16) & 0xFFFF;

            for (int j = 0; j < numTimestamps; j++)
            {
                float trackTimestamp = ((float)ctx.trackTimestamps[trackInfo.timestampOffset + j] / 1000.f);
                if (state.animationProgress < trackTimestamp)
                {
                    float defaultTimestamp = 0.f;
                    float4 defaultValue = float4(1.f, 1.f, 1.f, 0.f);

                    if (j > 0)
                    {
                        defaultTimestamp = ((float)ctx.trackTimestamps[trackInfo.timestampOffset + (j - 1)] / 1000.f);
                        defaultValue = ctx.trackValues[trackInfo.valueOffset + (j - 1)];
                    }

                    float nextValueTimestamp = ((float)ctx.trackTimestamps[trackInfo.timestampOffset + j] / 1000.f);
                    float4 nextValue = ctx.trackValues[trackInfo.valueOffset + j];

                    float time = (state.animationProgress - defaultTimestamp) / (nextValueTimestamp - defaultTimestamp);
                    scaleValue = lerp(defaultValue, nextValue, time);

                    break;
                }
            }
        }

        if (sequenceIndex < numRotationSequences)
        {
            AnimationTrackInfo trackInfo = ctx.animationTrackInfos[boneInfo.rotationTrackOffset + sequenceIndex];

            uint numTimestamps = trackInfo.packedData0 & 0xFFFF;
            uint numValues = (trackInfo.packedData0 >> 16) & 0xFFFF;

            for (int j = 0; j < numTimestamps; j++)
            {
                float trackTimestamp = ((float)ctx.trackTimestamps[trackInfo.timestampOffset + j] / 1000.f);
                if (state.animationProgress < trackTimestamp)
                {
                    float defaultTimestamp = 0.f;
                    float4 defaultValue = float4(0.f, 0.f, 0.f, 1.f);

                    if (j > 0)
                    {
                        defaultTimestamp = ((float)ctx.trackTimestamps[trackInfo.timestampOffset + (j - 1)] / 1000.f);
                        defaultValue = ctx.trackValues[trackInfo.valueOffset + (j - 1)];
                    }

                    float nextValueTimestamp = ((float)ctx.trackTimestamps[trackInfo.timestampOffset + j] / 1000.f);
                    float4 nextValue = ctx.trackValues[trackInfo.valueOffset + j];

                    float time = (state.animationProgress - defaultTimestamp) / (nextValueTimestamp - defaultTimestamp);
                    rotationValue = slerp(normalize(defaultValue), normalize(nextValue), time);
                    break;
                }
            }
        }

        if (sequenceIndex < numTranslationSequences)
        {
            AnimationTrackInfo trackInfo = ctx.animationTrackInfos[boneInfo.translationTrackOffset + sequenceIndex];

            uint numTimestamps = trackInfo.packedData0 & 0xFFFF;
            uint numValues = (trackInfo.packedData0 >> 16) & 0xFFFF;

            for (int j = 0; j < numTimestamps; j++)
            {
                float trackTimestamp = ((float)ctx.trackTimestamps[trackInfo.timestampOffset + j] / 1000.f);
                if (state.animationProgress < trackTimestamp)
                {
                    float defaultTimestamp = 0.f;
                    float4 defaultValue = float4(0.f, 0.f, 0.f, 0.f);

                    if (j > 0)
                    {
                        defaultTimestamp = ((float)ctx.trackTimestamps[trackInfo.timestampOffset + (j - 1)] / 1000.f);
                        defaultValue = ctx.trackValues[trackInfo.valueOffset + (j - 1)];
                    }

                    float nextValueTimestamp = ((float)ctx.trackTimestamps[trackInfo.timestampOffset + j] / 1000.f);
                    float4 nextValue = ctx.trackValues[trackInfo.valueOffset + j];

                    float time = (state.animationProgress - defaultTimestamp) / (nextValueTimestamp - defaultTimestamp);
                    translationValue = lerp(defaultValue, nextValue, time);

                    break;
                }
            }
        }
    }

    boneMatrix = mul(MatrixTranslate(pivotPoint.xyz), boneMatrix);

    boneMatrix = mul(MatrixTranslate(translationValue.xyz), boneMatrix);
    boneMatrix = mul(RotationToMatrix(rotationValue), boneMatrix);
    boneMatrix = mul(MatrixScale(scaleValue.xyz), boneMatrix);

    boneMatrix = mul(MatrixTranslate(-pivotPoint.xyz), boneMatrix);

    return boneMatrix;
}