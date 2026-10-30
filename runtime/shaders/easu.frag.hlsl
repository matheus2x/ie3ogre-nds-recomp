// FSR 1 EASU (edge adaptive spatial upsampling), after AMD FidelityFX Super Resolution 1
// (ffx_fsr1.h, Copyright (c) 2021 Advanced Micro Devices, Inc., MIT license, see
// licenses/fidelityfx-fsr.txt): one screen's picture enlarged to the size it is shown at in
// the window. Drawn by SDL's GPU renderer through an SDL_GPURenderState (see fxaa.frag.hlsl
// for the bindings). The picture's alpha is the 3D mask (gpu2d.cpp): 3D pixels go through
// EASU; 2D pixels (HUD, text) through sharp bilinear, which keeps them crisp at any size.
// The mask goes on in the alpha, for RCAS.
//
// Compiled by tools/shaders.py into runtime/src/shaders_embedded.h.

cbuffer Constants : register(b0, space3) {
    float4 input_size;   // xy = the picture's size, zw = 1 / that
    float4 output_size;  // xy = the size drawn, zw = 1 / that
};

Texture2D u_texture : register(t0, space2);
SamplerState u_sampler : register(s0, space2);

// SDL's vertex shader outputs under Direct3D 12 (see fxaa.frag.hlsl).
struct PSInput {
    float4 v_color : TEXCOORD0;
    float2 v_uv : TEXCOORD1;
};

float3 tap(int2 p) {
    int2 size = int2(input_size.xy);
    return u_texture.Load(int3(clamp(p, int2(0, 0), size - 1), 0)).rgb;
}

float luma(float3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }

// Direction and length of the edge from one of the four bilinear neighbors:
//   a
// b c d
//   e
void edge(inout float2 dir, inout float len, float w, float la, float lb, float lc, float ld, float le) {
    float dc = ld - lc, cb = lc - lb;
    float len_x = max(abs(dc), abs(cb));
    len_x = len_x > 0 ? 1.0 / len_x : 0;
    float dir_x = ld - lb;
    dir.x += dir_x * w;
    len_x = saturate(abs(dir_x) * len_x);
    len += len_x * len_x * w;
    float ec = le - lc, ca = lc - la;
    float len_y = max(abs(ec), abs(ca));
    len_y = len_y > 0 ? 1.0 / len_y : 0;
    float dir_y = le - la;
    dir.y += dir_y * w;
    len_y = saturate(abs(dir_y) * len_y);
    len += len_y * len_y * w;
}

// One tap of the filter: a Lanczos-like lobe stretched along the edge.
void accumulate(inout float3 color, inout float weight, float2 off, float2 dir, float2 len, float lob, float clp, float3 c) {
    float2 v = float2(off.x * dir.x + off.y * dir.y, off.x * -dir.y + off.y * dir.x) * len;
    float d2 = min(v.x * v.x + v.y * v.y, clp);
    float wb = 2.0 / 5.0 * d2 - 1.0;
    float wa = lob * d2 - 1.0;
    wb *= wb;
    wa *= wa;
    wb = 25.0 / 16.0 * wb - (25.0 / 16.0 - 1.0);
    float w = wb * wa;
    color += c * w;
    weight += w;
}

// Sharp bilinear for the 2D: whole pixels, smoothed only on their borders.
float3 sharp(float2 uv) {
    float2 texel = uv * input_size.xy;
    float2 scale = output_size.xy * input_size.zw;
    float2 region = 0.5 - 0.5 / scale;
    float2 dist = frac(texel) - 0.5;
    float2 f = (dist - clamp(dist, -region, region)) * scale + 0.5;
    return u_texture.SampleLevel(u_sampler, (floor(texel) + f) * input_size.zw, 0).rgb;
}

float4 main(PSInput input) : SV_Target {
    float2 pos = input.v_uv * output_size.xy;  // the pixel's center, in output pixels
    float mask = u_texture.Load(int3(int2(pos * input_size.xy * output_size.zw), 0)).a;
    if (mask < 0.5) return float4(sharp(input.v_uv), 0.0);

    // The 12 taps around the position, f being the one at its top left:
    //     b c
    //   e f g h
    //   i j k l
    //     n o
    float2 pp = pos * input_size.xy * output_size.zw - 0.5;
    float2 fp = floor(pp);
    pp -= fp;
    int2 p = int2(fp);
    float3 b = tap(p + int2(0, -1)), c = tap(p + int2(1, -1));
    float3 e = tap(p + int2(-1, 0)), f = tap(p), g = tap(p + int2(1, 0)), h = tap(p + int2(2, 0));
    float3 i = tap(p + int2(-1, 1)), j = tap(p + int2(0, 1)), k = tap(p + int2(1, 1)), l = tap(p + int2(2, 1));
    float3 n = tap(p + int2(0, 2)), o = tap(p + int2(1, 2));
    float lb = luma(b), lc = luma(c), le = luma(e), lf = luma(f), lg = luma(g), lh = luma(h);
    float li = luma(i), lj = luma(j), lk = luma(k), ll = luma(l), ln = luma(n), lo = luma(o);

    float2 dir = 0;
    float len = 0;
    edge(dir, len, (1 - pp.x) * (1 - pp.y), lb, le, lf, lg, lj);
    edge(dir, len, pp.x * (1 - pp.y), lc, lf, lg, lh, lk);
    edge(dir, len, (1 - pp.x) * pp.y, lf, li, lj, lk, ln);
    edge(dir, len, pp.x * pp.y, lg, lj, lk, ll, lo);

    float dir_r = dir.x * dir.x + dir.y * dir.y;
    bool zero = dir_r < 1.0 / 32768.0;
    dir_r = zero ? 1 : rsqrt(dir_r);
    dir.x = zero ? 1 : dir.x;
    dir *= dir_r;
    len = len * 0.5;
    len *= len;
    float stretch = (dir.x * dir.x + dir.y * dir.y) / max(abs(dir.x), abs(dir.y));
    float2 len2 = float2(1 + (stretch - 1) * len, 1 - 0.5 * len);
    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    float clp = 1.0 / lob;

    float3 color = 0;
    float weight = 0;
    accumulate(color, weight, float2(0, -1) - pp, dir, len2, lob, clp, b);
    accumulate(color, weight, float2(1, -1) - pp, dir, len2, lob, clp, c);
    accumulate(color, weight, float2(-1, 1) - pp, dir, len2, lob, clp, i);
    accumulate(color, weight, float2(0, 1) - pp, dir, len2, lob, clp, j);
    accumulate(color, weight, float2(0, 0) - pp, dir, len2, lob, clp, f);
    accumulate(color, weight, float2(-1, 0) - pp, dir, len2, lob, clp, e);
    accumulate(color, weight, float2(1, 1) - pp, dir, len2, lob, clp, k);
    accumulate(color, weight, float2(2, 1) - pp, dir, len2, lob, clp, l);
    accumulate(color, weight, float2(2, 0) - pp, dir, len2, lob, clp, h);
    accumulate(color, weight, float2(1, 0) - pp, dir, len2, lob, clp, g);
    accumulate(color, weight, float2(1, 2) - pp, dir, len2, lob, clp, o);
    accumulate(color, weight, float2(0, 2) - pp, dir, len2, lob, clp, n);

    // No ringing: kept between the four nearest pixels.
    float3 lo3 = min(min(f, g), min(j, k));
    float3 hi3 = max(max(f, g), max(j, k));
    return float4(clamp(color / weight, lo3, hi3), 1.0);
}
