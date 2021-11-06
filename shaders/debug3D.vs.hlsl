#include "globalData.inc.hlsl"

struct Vertex3D
{
	float3 pos;
	uint color;
};

[[vk::binding(0, PER_PASS)]] StructuredBuffer<Vertex3D> _vertices;

struct VSInput
{
	uint vertexID : SV_VertexID;
};

struct VSOutput
{
	float4 pos : SV_Position;
	float4 color : Color;
};

float4 GetVertexColor(uint inColor)
{
	float4 color;

	color.a = ((inColor & 0xff000000) >> 24) / 255.0f;
	color.b = ((inColor & 0x00ff0000) >> 16) / 255.0f;
	color.g = ((inColor & 0x0000ff00) >> 8) / 255.0f;
	color.r = (inColor & 0x000000ff) / 255.0f;

	return color;
}

VSOutput main(VSInput input)
{
	Vertex3D vertex = _vertices[input.vertexID];

	VSOutput output;
	output.pos = mul(float4(vertex.pos, 1.0f), _viewData.viewProjectionMatrix);
	output.color = GetVertexColor(vertex.color);
	return output;
}
