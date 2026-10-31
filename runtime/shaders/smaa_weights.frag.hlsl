// SMAA 1x, second pass: blending weights from the edges of smaa_edges.frag.hlsl (see
// smaa.hlsli). t1 is SMAA's area texture (linear filtering), t2 its search texture (point),
// both from smaa/AreaTex.h and smaa/SearchTex.h, made once by host.cpp.
//
// Compiled by tools/shaders.py into runtime/src/shaders_embedded.h.

#define SMAA_PASS_WEIGHTS
#include "smaa.hlsli"

float4 main(PSInput input) : SV_Target {
    float2 pixcoord;
    float4 offset[3];
    SMAABlendingWeightCalculationVS(input.v_uv, pixcoord, offset);
    return SMAABlendingWeightCalculationPS(input.v_uv, pixcoord, offset, edgesTex, areaTex, searchTex, float4(0, 0, 0, 0));
}
