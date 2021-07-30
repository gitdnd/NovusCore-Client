struct Color
{
    float4 value;
};
[[vk::push_constant]] Color _color;

struct VertexOutput
{
    float4 position : SV_POSITION;
};

float4 main(VertexOutput input) : SV_Target
{
    return _color.value;
}