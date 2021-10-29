
// SPDSample
//
// Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

//--------------------------------------------------------------------------------------
// Push Constants
//--------------------------------------------------------------------------------------
struct SpdConstants
{
    uint mips;
    uint numWorkGroups;
    uint2 workGroupOffset;
    float2 invInputSize;
};

[[vk::push_constant]]
ConstantBuffer<SpdConstants> spdConstants;
//--------------------------------------------------------------------------------------
// Texture definitions
//--------------------------------------------------------------------------------------
[[vk::binding(3, PER_PASS)]] Texture2D<float4> imgSrc;
[[vk::binding(0, PER_PASS)]] RWTexture2D<float4> imgDst[12]; // don't access mip [5]
[[vk::binding(1, PER_PASS)]] globallycoherent RWTexture2D<float4> imgDst5;
//[[vk::binding(0, PER_PASS)]] RWTexture2D<float4> imgDst[12]; // don't access mip [5]
//[[vk::binding(1, PER_PASS)]] globallycoherent RWTexture2D<float4> imgDst5;
[[vk::binding(4, PER_PASS)]] SamplerState srcSampler;

//--------------------------------------------------------------------------------------
// Buffer definitions - global atomic counter
//--------------------------------------------------------------------------------------
struct SpdGlobalAtomicBuffer
{
    uint counter[6];
};
[[vk::binding(2, PER_PASS)]] globallycoherent RWStructuredBuffer<SpdGlobalAtomicBuffer> spdGlobalAtomic;

#define A_GPU
#define A_HLSL

#include "DownSampler/ffx_a.inc.hlsl"

groupshared AU1 spdCounter;

// define fetch and store functions
#ifndef SPD_PACKED_ONLY
groupshared AF1 spdIntermediateR[16][16];
groupshared AF1 spdIntermediateG[16][16];
groupshared AF1 spdIntermediateB[16][16];
groupshared AF1 spdIntermediateA[16][16];

AF4 SpdLoadSourceImage(ASU2 p, AU1 slice)
{
    AF2 textureCoord = p * spdConstants.invInputSize + spdConstants.invInputSize;
    return imgSrc.SampleLevel(srcSampler, textureCoord, 0);
}
AF4 SpdLoad(ASU2 tex, AU1 slice)
{
    return imgDst5[tex];
}
void SpdStore(ASU2 pix, AF4 outValue, AU1 index, AU1 slice)
{
    if (index == 5)
    {
        imgDst5[pix] = outValue;
        return;
    }
    imgDst[index][pix] = outValue;
}
void SpdIncreaseAtomicCounter(AU1 slice)
{
    InterlockedAdd(spdGlobalAtomic[0].counter[0], 1, spdCounter);
}
AU1 SpdGetAtomicCounter()
{
    return spdCounter;
}
void SpdResetAtomicCounter(AU1 slice)
{
    spdGlobalAtomic[0].counter[0] = 0;
}
AF4 SpdLoadIntermediate(AU1 x, AU1 y)
{
    return AF4(
        spdIntermediateR[x][y],
        spdIntermediateG[x][y],
        spdIntermediateB[x][y],
        spdIntermediateA[x][y]);
}
void SpdStoreIntermediate(AU1 x, AU1 y, AF4 value)
{
    spdIntermediateR[x][y] = value.x;
    spdIntermediateG[x][y] = value.y;
    spdIntermediateB[x][y] = value.z;
    spdIntermediateA[x][y] = value.w;
}
AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3)
{
    return min(min(v0, v1), min(v2, v3));//(v0 + v1 + v2 + v3) * 0.25;
}
#endif

// define fetch and store functions Packed
#ifdef A_HALF
groupshared AH2 spdIntermediateRG[16][16];
groupshared AH2 spdIntermediateBA[16][16];

AH4 SpdLoadSourceImageH(ASU2 p, AU1 slice)
{
    AF2 textureCoord = p * spdConstants.invInputSize + spdConstants.invInputSize;
    return AH4(imgSrc.SampleLevel(srcSampler, textureCoord, 0));
}
AH4 SpdLoadH(ASU2 p, AU1 slice)
{
    return AH4(imgDst5[p]);
}
void SpdStoreH(ASU2 p, AH4 value, AU1 mip, AU1 slice)
{
    if (mip == 5)
    {
        imgDst5[p] = AF4(value);
        return;
    }
    imgDst[mip][p] = AF4(value);
}
void SpdIncreaseAtomicCounter(AU1 slice)
{
    InterlockedAdd(spdGlobalAtomic[0].counter[0], 1, spdCounter);
}
AU1 SpdGetAtomicCounter()
{
    return spdCounter;
}
void SpdResetAtomicCounter(AU1 slice)
{
    spdGlobalAtomic[0].counter[0] = 0;
}
AH4 SpdLoadIntermediateH(AU1 x, AU1 y)
{
    return AH4(
        spdIntermediateRG[x][y].x,
        spdIntermediateRG[x][y].y,
        spdIntermediateBA[x][y].x,
        spdIntermediateBA[x][y].y);
}
void SpdStoreIntermediateH(AU1 x, AU1 y, AH4 value)
{
    spdIntermediateRG[x][y] = value.xy;
    spdIntermediateBA[x][y] = value.zw;
}
AH4 SpdReduce4H(AH4 v0, AH4 v1, AH4 v2, AH4 v3)
{
    return (v0 + v1 + v2 + v3) * AH1(0.25);
}
#endif

#define SPD_LINEAR_SAMPLER

#include "DownSampler/ffx_spd.inc.hlsl"

// Main function
//--------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void main(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex)
{
#ifndef A_HALF
    SpdDownsample(
        AU2(WorkGroupId.xy),
        AU1(LocalThreadIndex),
        AU1(spdConstants.mips),
        AU1(spdConstants.numWorkGroups),
        AU1(WorkGroupId.z),
        AU2(spdConstants.workGroupOffset));
#else
    SpdDownsampleH(
        AU2(WorkGroupId.xy),
        AU1(LocalThreadIndex),
        AU1(spdConstants.mips),
        AU1(spdConstants.numWorkGroups),
        AU1(WorkGroupId.z),
        AU2(spdConstants.workGroupOffset));
#endif
}