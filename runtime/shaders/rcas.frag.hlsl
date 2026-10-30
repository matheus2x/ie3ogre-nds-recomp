// FSR 1 RCAS (robust contrast adaptive sharpening), after AMD FidelityFX Super Resolution 1
// (ffx_fsr1.h, Copyright (c) 2021 Advanced Micro Devices, Inc., MIT license, see
// licenses/fidelityfx-fsr.txt): sharpens EASU's output, pixel for pixel. Only where the
// alpha (the 3D mask, kept by easu.frag.hlsl) is set; the 2D is copied.
//
// Compiled by tools/shaders.py into runtime/src/shaders_embedded.h.

cbuffer Constants : register(b0, space3) {
    float4 size;       // xy = the picture's size, zw = 1 / that
    float4 settings;   // x = sharpness (1 = the most, exp2(-stops))
};

Texture2D u_texture : register(t0, space2);
SamplerState u_sampler : register(s0, space2);

// SDL's vertex shader outputs under Direct3D 12 (see fxaa.frag.hlsl).
struct PSInput {
    float4 v_color : TEXCOORD0;
    float2 v_uv : TEXCOORD1;
};

static const float kLimit = 0.25 - 1.0 / 16.0;

float3 tap(int2 p) {
    return u_texture.Load(int3(clamp(p, int2(0, 0), int2(size.xy) - 1), 0)).rgb;
}

float4 main(PSInput input) : SV_Target {
    int2 p = int2(input.v_uv * size.xy);
    float4 center = u_texture.Load(int3(p, 0));
    if (center.a < 0.5) return float4(center.rgb, 1.0);

    //   b
    // d e f
    //   h
    float3 b = tap(p + int2(0, -1)), d = tap(p + int2(-1, 0)), e = center.rgb;
    float3 f = tap(p + int2(1, 0)), h = tap(p + int2(0, 1));
    float3 lo = min(min(b, d), min(f, h));
    float3 hi = max(max(b, d), max(f, h));
    // The strongest negative lobe that neither clips to black nor to white.
    float3 hit_min = min(lo, e) / (4.0 * max(hi, e));
    float3 hit_max = (1.0 - max(hi, e)) / (4.0 * min(lo, e) - 4.0);
    float3 lobe3 = max(-hit_min, hit_max);
    float lobe = max(-kLimit, min(max(lobe3.r, max(lobe3.g, lobe3.b)), 0.0)) * settings.x;
    float3 color = (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
    return float4(saturate(color), 1.0);
}
