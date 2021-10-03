
#include "common.inc.hlsl"
#include "globalData.inc.hlsl"
#include "cModel.inc.hlsl"

struct VSInput
{
    uint vertexID : SV_VertexID;
    uint instanceID : SV_InstanceID;
};

struct VSOutput
{
    float4 position : SV_Position;
    nointerpolation uint drawCallID : TEXCOORD0;
    float4 uv01 : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float depth : TEXCOORD3;
};

VSOutput main(VSInput input)
{
    uint drawCallID = input.instanceID;
    CModelVertex vertex = LoadCModelVertex(input.vertexID);

    CModelDrawCallData drawCallData = LoadCModelDrawCallData(drawCallID);
    CModelInstanceData instanceData = _cModelInstanceDatas[drawCallData.instanceID];
    float4x4 instanceMatrix = _cModelInstanceMatrices[drawCallData.instanceID];

    // Skin this vertex
    float4x4 boneTransformMatrix = CalcBoneTransformMatrix(instanceData, vertex);
    float4 position = mul(float4(vertex.position, 1.0f), boneTransformMatrix);

    position = mul(float4(-position.x, -position.y, position.z, 1.0f), instanceMatrix);

    // Pass data to pixelshader
    VSOutput output;
    output.position = mul(position, _viewData.viewProjectionMatrix);
    output.drawCallID = drawCallID;
    output.uv01 = vertex.uv01;
    output.normal = mul(vertex.normal, (float3x3)instanceMatrix);
    output.depth = mul(position, _viewData.viewMatrix).z;

    return output;
}