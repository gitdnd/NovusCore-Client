#ifndef CMODEL_INC_INCLUDED
#define CMODEL_INC_INCLUDED
#include "common.inc.hlsl"

struct CModelInstanceData
{
    float4x4 instanceMatrix;

    uint modelId;
    uint boneDeformOffset;
    uint boneInstanceDataOffset;
    uint padding0;
    uint vertexOffset;
    uint animatedVertexOffset;
    uint padding1;
    uint padding2;

    /*uint modelId;
    uint activeSequenceId;
    float animProgress;
    uint boneDeformOffset;*/
};

struct AnimationBoneInstanceData
{
    float animationProgress;
    uint packedData0; // sequenceIndex (16 bit), sequenceOverrideIndex (16 bit)
    uint animationframeIndex;
    uint animateState; // 0 == STOPPED, 1 == PLAY_ONCE, 2 == PLAY_LOOP
};

struct AnimationSequence
{
    uint packedData0; // animationId (16 bit), animationSubId (16 bit)
    uint packedData1; // nextSubAnimationId (16 bit), nextAliasId (16 bit)

    uint flags; // 0x1(IsAlwaysPlaying), 0x2(IsAlias), 0x4(BlendTransition)
    float duration;

    uint packedRepeatRange; // Min (16 bit), Max (16 bit)
    uint packedBlendTimes; // Start (16 bit), End (16 bit)

    uint padding0;
    uint padding1;
};

struct AnimationModelInfo
{
    uint packedData0; // numSequences (16 bit), numBones (16 bit)
    uint sequenceOffset;
    uint boneInfoOffset;
    uint padding0;
};

struct AnimationBoneInfo
{
    uint packedData0; // numTranslationTracks, numRotationTracks
    uint packedData1; // numScaleTracks, parentBoneId

    uint translationTrackOffset;
    uint rotationTrackOffset;
    uint scaleTrackOffset;

    uint flags;

    float pivotPointX;
    float pivotPointY;
    float pivotPointZ;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct AnimationTrackInfo
{
    uint sequenceIndex; // Only 16 bit is used here, rest is padding

    uint packedData0; // numTimestamps, numValues
    uint timestampOffset;
    uint valueOffset;
};

struct PackedCModelDrawCallData
{
    uint instanceID;
    uint cullingDataID;
    uint packed; // uint16_t textureUnitOffset, uint16_t numTextureUnits
    uint renderPriority;
}; // 16 bytes

struct CModelDrawCallData
{
    uint instanceID;
    uint cullingDataID;
    uint textureUnitOffset;
    uint numTextureUnits;
    uint renderPriority;
};

[[vk::binding(0, CMODEL)]] StructuredBuffer<PackedCModelDrawCallData> _packedCModelDrawCallDatas;
CModelDrawCallData LoadCModelDrawCallData(uint drawCallID)
{
    PackedCModelDrawCallData packedDrawCallData = _packedCModelDrawCallDatas[drawCallID];
    
    CModelDrawCallData drawCallData;
    
    drawCallData.instanceID = packedDrawCallData.instanceID;
    drawCallData.cullingDataID = packedDrawCallData.cullingDataID;
    
    drawCallData.textureUnitOffset = packedDrawCallData.packed & 0xFFFF;
    drawCallData.numTextureUnits = (packedDrawCallData.packed >> 16) && 0xFFFF;
    
    drawCallData.renderPriority = packedDrawCallData.renderPriority;
    
    return drawCallData;
}

struct PackedCModelVertex
{
    uint packed0; // half positionX, half positionY
    uint packed1; // half positionZ, u8 octNormal[2]
    uint packed2; // half uv0X, half uv0Y
    uint packed3; // half uv1X, half uv1Y
    uint packed4; // bone indices (0..4)
    uint packed5; // bone weights (0..4)
}; // 24 bytes

struct CModelVertex
{
    float3 position;

    float4 uv01;
#if !GEOMETRY_PASS
    float3 normal;
#endif

    uint4 boneIndices;
    float4 boneWeights;
};

float3 UnpackCModelPosition(PackedCModelVertex packedVertex)
{
    float3 position;

    position.x = f16tof32(packedVertex.packed0);
    position.y = f16tof32(packedVertex.packed0 >> 16);
    position.z = f16tof32(packedVertex.packed1);

    return position;
}

float3 UnpackCModelNormal(PackedCModelVertex packedVertex)
{
    uint x = (packedVertex.packed1 >> 16) & 0xFF;
    uint y = packedVertex.packed1 >> 24;

    float2 octNormal = float2(x, y) / 255.0f;
    return OctNormalDecode(octNormal);
}

float4 UnpackCModelUVs(PackedCModelVertex packedVertex)
{
    float4 uvs;

    uvs.x = f16tof32(packedVertex.packed2);
    uvs.y = f16tof32(packedVertex.packed2 >> 16);
    uvs.z = f16tof32(packedVertex.packed3);
    uvs.w = f16tof32(packedVertex.packed3 >> 16);

    return uvs;
}

uint4 UnpackCModelBoneIndices(PackedCModelVertex packedVertex)
{
    uint4 boneIndices;

    boneIndices.x = packedVertex.packed4 & 0xFF;
    boneIndices.y = (packedVertex.packed4 >> 8) & 0xFF;
    boneIndices.z = (packedVertex.packed4 >> 16) & 0xFF;
    boneIndices.w = (packedVertex.packed4 >> 24) & 0xFF;

    return boneIndices;
}

float4 UnpackCModelBoneWeights(PackedCModelVertex packedVertex)
{
    float4 boneWeights;

    boneWeights.x = (float)(packedVertex.packed5 & 0xFF) / 255.f;
    boneWeights.y = (float)((packedVertex.packed5 >> 8) & 0xFF) / 255.f;
    boneWeights.z = (float)((packedVertex.packed5 >> 16) & 0xFF) / 255.f;
    boneWeights.w = (float)((packedVertex.packed5 >> 24) & 0xFF) / 255.f;

    return boneWeights;
}

[[vk::binding(1, CMODEL)]] StructuredBuffer<PackedCModelVertex> _packedCModelVertices;
CModelVertex LoadCModelVertex(uint vertexID)
{
    PackedCModelVertex packedVertex = _packedCModelVertices[vertexID];

    CModelVertex vertex;
    vertex.position = UnpackCModelPosition(packedVertex);
    vertex.uv01 = UnpackCModelUVs(packedVertex);

#if !GEOMETRY_PASS
    vertex.normal = UnpackCModelNormal(packedVertex);
#endif

    vertex.boneIndices = UnpackCModelBoneIndices(packedVertex);
    vertex.boneWeights = UnpackCModelBoneWeights(packedVertex);

    return vertex;
}

[[vk::binding(2, CMODEL)]] StructuredBuffer<CModelInstanceData> _cModelInstances;
[[vk::binding(3, CMODEL)]] StructuredBuffer<float4x4> _cModelAnimationBoneDeformMatrices;

struct PackedAnimatedVertexPosition
{
    uint packed0; // half2 position.xy
    uint packed1; // half position.z, padding
};
[[vk::binding(4, CMODEL)]] RWStructuredBuffer<PackedAnimatedVertexPosition> _animatedCModelVertexPositions;

void StoreAnimatedVertexPosition(uint animatedVertexID, float3 position)
{
    PackedAnimatedVertexPosition packedAnimatedVertexPosition;
    packedAnimatedVertexPosition.packed0 = f32tof16(position.x);
    packedAnimatedVertexPosition.packed0 |= f32tof16(position.y) << 16;
    packedAnimatedVertexPosition.packed1 = f32tof16(position.z);

    _animatedCModelVertexPositions[animatedVertexID] = packedAnimatedVertexPosition;
}

float3 LoadAnimatedVertexPosition(uint animatedVertexID)
{
    PackedAnimatedVertexPosition packedAnimatedVertexPosition = _animatedCModelVertexPositions[animatedVertexID];

    float3 position;
    position.x = f16tof32(packedAnimatedVertexPosition.packed0);
    position.y = f16tof32(packedAnimatedVertexPosition.packed0 >> 16);
    position.z = f16tof32(packedAnimatedVertexPosition.packed1);
    return position;
}

float4x4 CalcBoneTransformMatrix(const CModelInstanceData instanceData, CModelVertex vertex)
{
    float4x4 boneTransformMatrix = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);

    if (instanceData.boneDeformOffset != 4294967295)
    {
        boneTransformMatrix = float4x4(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

        [unroll]
        for (int j = 0; j < 4; j++)
        {
            boneTransformMatrix += mul(vertex.boneWeights[j], _cModelAnimationBoneDeformMatrices[instanceData.boneDeformOffset + vertex.boneIndices[j]]);
        }
    }

    return boneTransformMatrix;
}

[[vk::binding(5, CMODEL)]] StructuredBuffer<Draw> _cModelDraws;
[[vk::binding(6, CMODEL)]] StructuredBuffer<uint> _cModelIndices;

struct CModelTextureUnit
{
    uint data1; // (Is Projected Texture (1 bit) + Material Flag (10 bit) + Material Blending Mode (3 bit) + Unused Padding (2 bits)) + Material Type (16 bit)
    uint textureIDs[2];
    uint padding;
};

[[vk::binding(7, CMODEL)]] StructuredBuffer<CModelTextureUnit> _cModelTextureUnits;
[[vk::binding(10, CMODEL)]] Texture2D<float4> _cModelTextures[4096]; // We give this index 10 because it always needs to be last in this descriptor set

enum CModelPixelShaderID
{
    Opaque,
    Opaque_Opaque,
    Opaque_Mod,
    Opaque_Mod2x,
    Opaque_Mod2xNA,
    Opaque_Add,
    Opaque_AddNA,
    Opaque_AddAlpha,
    Opaque_AddAlpha_Alpha,
    Opaque_Mod2xNA_Alpha,
    Mod,
    Mod_Opaque,
    Mod_Mod,
    Mod_Mod2x,
    Mod_Mod2xNA,
    Mod_Add,
    Mod_AddNA,
    Mod2x,
    Mod2x_Mod,
    Mod2x_Mod2x,
    Add,
    Add_Mod,
    Fade,
    Decal
};

float4 ShadeCModel(uint pixelId, float4 texture1, float4 texture2, out float3 specular)
{
    float4 result = float4(0, 0, 0, 0);
    float4 diffuseColor = float4(1, 1, 1, 1);
    specular = float3(0, 0, 0);

    if (pixelId == CModelPixelShaderID::Opaque)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = diffuseColor.a;
    }
    else if (pixelId == CModelPixelShaderID::Opaque_Opaque)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb * texture2.rgb;
        result.a = diffuseColor.a;
    }
    else if (pixelId == CModelPixelShaderID::Opaque_Mod)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb * texture2.rgb;
        result.a = texture2.a * diffuseColor.a;
    }
    else if (pixelId == CModelPixelShaderID::Opaque_Mod2x)
    {
        result.rgb = diffuseColor.rgb * 2.0f * texture1.rgb * texture2.rgb;
        result.a = diffuseColor.a * 2.0f * texture2.a;
    }
    else if (pixelId == CModelPixelShaderID::Opaque_Mod2xNA)
    {
        result.rgb = diffuseColor.rgb * 2.0f * texture1.rgb * texture2.rgb;
        result.a = diffuseColor.a;
    }
    else if (pixelId == CModelPixelShaderID::Opaque_Add)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb + texture2.rgb;
        result.a = diffuseColor.a + texture1.a;
    }
    else if (pixelId == CModelPixelShaderID::Opaque_AddNA)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb + texture2.rgb;
        result.a = diffuseColor.a;
    }
    else if (pixelId == CModelPixelShaderID::Opaque_AddAlpha)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = diffuseColor.a;

        specular = texture2.rgb * texture2.a;
    }
    else if (pixelId == CModelPixelShaderID::Opaque_AddAlpha_Alpha)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = diffuseColor.a;

        specular = texture2.rgb * texture2.a * (1.0f - texture1.a);
    }
    else if (pixelId == CModelPixelShaderID::Opaque_Mod2xNA_Alpha)
    {
        result.rgb = diffuseColor.rgb * lerp(texture1.rgb * texture2.rgb * 2.0f, texture1.rgb, texture1.aaa);
        result.a = diffuseColor.a;
    }
    else if (pixelId == CModelPixelShaderID::Mod)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = diffuseColor.a * texture1.a;
    }
    else if (pixelId == CModelPixelShaderID::Mod_Opaque)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb * texture2.rgb;
        result.a = diffuseColor.a * texture1.a;
    }
    else if (pixelId == CModelPixelShaderID::Mod_Mod)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb * texture2.rgb;
        result.a = diffuseColor.a * texture1.a * texture2.a;
    }
    else if (pixelId == CModelPixelShaderID::Mod_Mod2x)
    {
        result.rgb = diffuseColor.rgb * 2.0f * texture1.rgb * texture2.rgb;
        result.a = diffuseColor.a * 2.0f * texture1.a * texture2.a;
    }
    else if (pixelId == CModelPixelShaderID::Mod_Mod2xNA)
    {
        result.rgb = diffuseColor.rgb * 2.0f * texture1.rgb * texture2.rgb;
        result.a = texture1.a * diffuseColor.a;
    }
    else if (pixelId == CModelPixelShaderID::Mod_Add)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = diffuseColor.a * (texture1.a + texture2.a);

        specular = texture2.rgb;
    }
    else if (pixelId == CModelPixelShaderID::Mod_AddNA)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = texture1.a * diffuseColor.a;

        specular = texture2.rgb;
    }
    else if (pixelId == CModelPixelShaderID::Mod2x)
    {
        result.rgb = diffuseColor.rgb * 2.0f * texture1.rgb;
        result.a = diffuseColor.a * 2.0f * texture1.a;
    }
    else if (pixelId == CModelPixelShaderID::Mod2x_Mod)
    {
        result.rgb = diffuseColor.rgb * 2.0f * texture1.rgb * texture2.rgb;
        result.a = diffuseColor.a * 2.0f * texture1.a * texture2.a;
    }
    else if (pixelId == CModelPixelShaderID::Mod2x_Mod2x)
    {
        result = diffuseColor * 4.0f * texture1 * texture2;
    }
    else if (pixelId == CModelPixelShaderID::Add)
    {
        result = diffuseColor + texture1;
    }
    else if (pixelId == CModelPixelShaderID::Add_Mod)
    {
        result.rgb = (diffuseColor.rgb + texture1.rgb) * texture2.a;
        result.a = (diffuseColor.a + texture1.a) * texture2.a;
    }
    else if (pixelId == CModelPixelShaderID::Fade)
    {
        result.rgb = (texture1.rgb - diffuseColor.rgb) * diffuseColor.a + diffuseColor.rgb;
        result.a = diffuseColor.a;
    }
    else if (pixelId == CModelPixelShaderID::Decal)
    {
        result.rgb = (diffuseColor.rgb - texture1.rgb) * diffuseColor.a + texture1.rgb;
        result.a = diffuseColor.a;
    }

    result.rgb = result.rgb;
    return result;
}

float4 BlendCModel(uint blendingMode, float4 previousColor, float4 color)
{
    float4 result = previousColor;

    if (blendingMode == 0) // OPAQUE
    {
        result = float4(color.rgb, 1);
    }
    else if (blendingMode == 1) // ALPHA KEY
    {
        // I don't think discarding here is needed since we already discard alphakeyed pixels in the geometry pass
        if (color.a >= 224.0f / 255.0f)
        {
            float3 blendedColor = color.rgb * color.a + previousColor.rgb * (1 - color.a);
            result.rgb += blendedColor;
            result.a = max(color.a, previousColor.a); // TODO: Check if this is actually needed
        }
        /*else
        {
            discard;
        }*/
    }
    else if (blendingMode == 2) // ALPHA
    {
        float3 blendedColor = color.rgb * color.a + previousColor.rgb * (1 - color.a);
        result.rgb += blendedColor;
        result.a = max(color.a, previousColor.a); // TODO: Check if this is actually needed
    }
    else if (blendingMode == 3) // NO ALPHA ADD
    {
        // TODO
        result.rgb += color.rgb;
    }
    else if (blendingMode == 4) // ADD
    {
        // TODO
        result.rgb += color.rgb * color.a;
        result.a = color.a;
    }
    else if (blendingMode == 5) // MOD
    {
        // TODO
    }
    else if (blendingMode == 6) // MOD2X
    {
        // TODO
    }
    else if (blendingMode == 7) // BLEND ADD
    {
        // TODO
    }

    return result;
}

#endif // CMODEL_INC_INCLUDED