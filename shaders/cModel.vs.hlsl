permutation EDITOR_PASS = [0, 1];
#define GEOMETRY_PASS 1

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
#if !EDITOR_PASS
    nointerpolation uint drawCallID : TEXCOORD0;
    float3 modelPosition : TEXCOORD1;
    float4 uv01 : TEXCOORD2;
#endif
};

VSOutput main(VSInput input)
{
    uint drawCallID = input.instanceID;
    CModelVertex vertex = LoadCModelVertex(input.vertexID);

    CModelDrawCallData drawCallData = LoadCModelDrawCallData(drawCallID);
    CModelInstanceData instanceData = _cModelInstances[drawCallData.instanceID];

    // Skin this vertex
    float4x4 boneTransformMatrix = CalcBoneTransformMatrix(instanceData, vertex);

    float4 position = mul(float4(vertex.position, 1.0f), boneTransformMatrix);
    
    // Save the skinned vertex position (in model-space) if this vertex was animated
    if (instanceData.boneDeformOffset != 4294967295)
    {
        uint localVertexID = input.vertexID - instanceData.vertexOffset; // This gets the local vertex ID relative to the model
        uint animatedVertexID = localVertexID + instanceData.animatedVertexOffset; // This makes it relative to the animated instance

        StoreAnimatedVertexPosition(animatedVertexID, position.xyz);
    }

    position = mul(float4(-position.x, -position.y, position.z, 1.0f), instanceData.instanceMatrix);

    // Pass data to pixelshader
    VSOutput output;
    output.position = mul(position, _viewData.viewProjectionMatrix);
#if !EDITOR_PASS
    output.drawCallID = drawCallID;
    output.modelPosition = position.xyz;
    output.uv01 = vertex.uv01;
#endif

    return output;
}