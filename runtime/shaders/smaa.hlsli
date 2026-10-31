// SMAA 1x (Jorge Jimenez et al., Copyright (C) 2013, MIT license, see licenses/smaa.txt)
// for the three smaa_*.frag.hlsl passes: smaa/SMAA.hlsl as published (github.com/iryoku/smaa),
// with porting macros for SDL's GPU renderer. Each texture comes with a sampler of its own
// (SDL binds them in pairs: t0/s0 is the texture drawn, t1/s1 and up the render state's),
// so a texture named x is sampled with x_sampler; each pass says which it is before the
// include (SMAA_PASS_EDGES, _WEIGHTS or _BLEND), and the samplers of the functions it does
// not call stand for a dummy one (the compilers drop it). The quality is SMAA's high preset.

cbuffer Constants : register(b0, space3) {
    float4 metrics;  // xy = 1 / the picture's size, zw = the size
};

#if defined(SMAA_PASS_EDGES)
Texture2D colorTex : register(t0, space2);
SamplerState colorTex_sampler : register(s0, space2);
#elif defined(SMAA_PASS_WEIGHTS)
Texture2D edgesTex : register(t0, space2);
SamplerState edgesTex_sampler : register(s0, space2);
Texture2D areaTex : register(t1, space2);
SamplerState areaTex_sampler : register(s1, space2);
Texture2D searchTex : register(t2, space2);
SamplerState searchTex_sampler : register(s2, space2);
#elif defined(SMAA_PASS_BLEND)
Texture2D colorTex : register(t0, space2);
SamplerState colorTex_sampler : register(s0, space2);
Texture2D blendTex : register(t1, space2);
SamplerState blendTex_sampler : register(s1, space2);
#endif

SamplerState smaa_unused_sampler;
#if !defined(SMAA_PASS_EDGES) && !defined(SMAA_PASS_BLEND)
#define colorTex_sampler smaa_unused_sampler
#endif
#if !defined(SMAA_PASS_WEIGHTS)
#define edgesTex_sampler smaa_unused_sampler
#define areaTex_sampler smaa_unused_sampler
#define searchTex_sampler smaa_unused_sampler
#endif
#if !defined(SMAA_PASS_BLEND)
#define blendTex_sampler smaa_unused_sampler
#endif
#define tex_sampler smaa_unused_sampler
#define currentColorTex_sampler smaa_unused_sampler
#define previousColorTex_sampler smaa_unused_sampler

#define SMAA_RT_METRICS metrics
#define SMAA_PRESET_HIGH
#define SMAA_CUSTOM_SL 1
#define SMAATexture2D(tex) Texture2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(tex##_sampler, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(tex##_sampler, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(tex##_sampler, coord, 0, offset)
#define SMAASample(tex, coord) tex.SampleLevel(tex##_sampler, coord, 0)
#define SMAASamplePoint(tex, coord) tex.SampleLevel(tex##_sampler, coord, 0)
#define SMAASampleOffset(tex, coord, offset) tex.SampleLevel(tex##_sampler, coord, 0, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]

#include "smaa/SMAA.hlsl"

// SDL's vertex shader outputs under Direct3D 12 (see fxaa.frag.hlsl).
struct PSInput {
    float4 v_color : TEXCOORD0;
    float2 v_uv : TEXCOORD1;
};
