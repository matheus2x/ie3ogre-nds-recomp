// SMAA 1x, first pass: luma edges of one screen's picture, at its own size (see smaa.hlsli).
// The picture's alpha is the 3D mask (gpu2d.cpp): an edge counts only between two 3D pixels,
// so the HUD and the text of the 2D are never blended, with the 3D or among themselves.
// The target is cleared first: SMAA discards the pixels without edges.
//
// Compiled by tools/shaders.py into runtime/src/shaders_embedded.h.

#define SMAA_PASS_EDGES
#include "smaa.hlsli"

float4 main(PSInput input) : SV_Target {
    float2 uv = input.v_uv;
    if (colorTex.SampleLevel(colorTex_sampler, uv, 0).a < 0.5) discard;
    float4 offset[3];
    SMAAEdgeDetectionVS(uv, offset);
    float2 edges = SMAALumaEdgeDetectionPS(uv, offset, colorTex);
    float left = colorTex.SampleLevel(colorTex_sampler, offset[0].xy, 0).a;
    float top = colorTex.SampleLevel(colorTex_sampler, offset[0].zw, 0).a;
    return float4(edges * step(0.5, float2(left, top)), 0, 0);
}
