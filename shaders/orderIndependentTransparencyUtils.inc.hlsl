#ifndef OIT_INC_INCLUDED
#define OIT_INC_INCLUDED

float CalculateOITWeight(float4 color, float depth)
{
    // This distance wants to be 0.1 < z < 500 according to the whitepaper, our nearclip is 1.0 and farclip is 100000.0f so we need to remap it
    float z = Map(depth, 0.1f, 100000.0f, 0.1f, 500.0f);
    float distWeight = clamp(0.03f / (1e-5f + pow(z / 200.0f, 4.0f)), 1e-2f, 3e3f);

    float alphaWeight = max(min(1.0f, max(max(color.r, color.g), color.b) * color.a), color.a);
    alphaWeight *= alphaWeight;

    float weight = alphaWeight * distWeight;

    return weight;
}

#endif