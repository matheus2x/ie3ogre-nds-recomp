// FXAA (after Timothy Lottes' FXAA 3.11, quality preset) over one screen's picture, at the
// picture's own size. Drawn by SDL's GPU renderer through an SDL_GPURenderState: the
// renderer binds the picture at t0/s0 (space2, with linear filtering) and the uniforms come
// in b0 (space3). The picture's alpha is the 3D mask (gpu2d.cpp): 2D pixels (HUD, text)
// are copied as they are.
//
// Compiled by tools/shaders.py into runtime/src/shaders_embedded.h.

cbuffer Constants : register(b0, space3) {
    float4 texel;  // xy = 1 / the picture's size, zw = the size
};

Texture2D u_texture : register(t0, space2);
SamplerState u_sampler : register(s0, space2);

// SDL's own shaders go through SPIR-V (SDL_shadercross), so under Direct3D 12 its vertex
// shader outputs are TEXCOORD0 (color) and TEXCOORD1 (uv); SPIR-V matches by order.
struct PSInput {
    float4 v_color : TEXCOORD0;
    float2 v_uv : TEXCOORD1;
};

static const float kEdgeThreshold = 0.166;
static const float kEdgeThresholdMin = 0.0312;
static const float kSubpixel = 0.25;
static const int kSteps = 10;
static const float kStep[kSteps] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 4.0, 8.0};

float luma(float2 uv) {
    return dot(u_texture.SampleLevel(u_sampler, uv, 0).rgb, float3(0.299, 0.587, 0.114));
}

float4 main(PSInput input) : SV_Target {
    float2 uv = input.v_uv;
    float4 center = u_texture.SampleLevel(u_sampler, uv, 0);
    if (center.a < 0.5) return center;

    float m = dot(center.rgb, float3(0.299, 0.587, 0.114));
    float n = luma(uv + float2(0, -texel.y));
    float s = luma(uv + float2(0, texel.y));
    float w = luma(uv + float2(-texel.x, 0));
    float e = luma(uv + float2(texel.x, 0));
    float lo = min(m, min(min(n, s), min(w, e)));
    float hi = max(m, max(max(n, s), max(w, e)));
    float range = hi - lo;
    if (range < max(kEdgeThresholdMin, hi * kEdgeThreshold)) return center;

    float nw = luma(uv + float2(-texel.x, -texel.y));
    float ne = luma(uv + float2(texel.x, -texel.y));
    float sw = luma(uv + float2(-texel.x, texel.y));
    float se = luma(uv + float2(texel.x, texel.y));

    // How much of a single-pixel feature this is: blurs those a little more.
    float average = (2.0 * (n + s + w + e) + nw + ne + sw + se) / 12.0;
    float sub = saturate(abs(average - m) / range);
    sub = smoothstep(0.0, 1.0, sub);
    sub = sub * sub * kSubpixel;

    float horizontal = abs(nw + sw - 2.0 * w) + 2.0 * abs(n + s - 2.0 * m) + abs(ne + se - 2.0 * e);
    float vertical = abs(nw + ne - 2.0 * n) + 2.0 * abs(w + e - 2.0 * m) + abs(sw + se - 2.0 * s);
    bool is_horizontal = horizontal >= vertical;  // the edge runs along x

    float luma1 = is_horizontal ? n : w;
    float luma2 = is_horizontal ? s : e;
    float gradient1 = luma1 - m, gradient2 = luma2 - m;
    bool first = abs(gradient1) >= abs(gradient2);
    float gradient = 0.25 * max(abs(gradient1), abs(gradient2));
    float step = is_horizontal ? texel.y : texel.x;
    float local;
    if (first) {
        step = -step;
        local = 0.5 * (luma1 + m);
    } else {
        local = 0.5 * (luma2 + m);
    }

    // Walk along the edge, half a pixel towards the steeper side, until both ends leave it.
    float2 edge = uv;
    if (is_horizontal) edge.y += step * 0.5;
    else edge.x += step * 0.5;
    float2 offset = is_horizontal ? float2(texel.x, 0) : float2(0, texel.y);
    float2 uv1 = edge - offset, uv2 = edge + offset;
    float end1 = luma(uv1) - local, end2 = luma(uv2) - local;
    bool done1 = abs(end1) >= gradient, done2 = abs(end2) >= gradient;
    for (int i = 1; i < kSteps && !(done1 && done2); i++) {
        if (!done1) {
            uv1 -= offset * kStep[i];
            end1 = luma(uv1) - local;
            done1 = abs(end1) >= gradient;
        }
        if (!done2) {
            uv2 += offset * kStep[i];
            end2 = luma(uv2) - local;
            done2 = abs(end2) >= gradient;
        }
    }

    float distance1 = is_horizontal ? uv.x - uv1.x : uv.y - uv1.y;
    float distance2 = is_horizontal ? uv2.x - uv.x : uv2.y - uv.y;
    bool nearer1 = distance1 < distance2;
    float distance = min(distance1, distance2);
    float pixel_offset = 0.5 - distance / (distance1 + distance2);
    // Only when the nearer end goes the other way from the center (the pixel is on the edge).
    bool right_side = ((nearer1 ? end1 : end2) < 0.0) != (m < local);
    float final_offset = max(right_side ? pixel_offset : 0.0, sub);

    float2 result = uv;
    if (is_horizontal) result.y += final_offset * step;
    else result.x += final_offset * step;
    return float4(u_texture.SampleLevel(u_sampler, result, 0).rgb, center.a);
}
