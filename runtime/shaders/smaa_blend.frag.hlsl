// SMAA 1x, third pass: the picture blended with its neighbors by the weights of
// smaa_weights.frag.hlsl (t1; see smaa.hlsli). 2D pixels (alpha 0, the 3D mask) are copied
// as they are, and the mask goes on in the alpha, for the filter or FSR 1 after it.
//
// Compiled by tools/shaders.py into runtime/src/shaders_embedded.h.

#define SMAA_PASS_BLEND
#include "smaa.hlsli"

float4 main(PSInput input) : SV_Target {
    float2 uv = input.v_uv;
    float4 center = colorTex.SampleLevel(colorTex_sampler, uv, 0);
    if (center.a < 0.5) return center;
    float4 offset;
    SMAANeighborhoodBlendingVS(uv, offset);
    return float4(SMAANeighborhoodBlendingPS(uv, offset, colorTex, blendTex).rgb, center.a);
}
