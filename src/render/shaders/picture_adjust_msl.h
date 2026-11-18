#pragma once
// Metal source of picture_adjust.frag.hlsl, in the SDL GPU renderer's resource layout.
static const char picture_adjust_msl[] = R"MSL(
#include <metal_stdlib>
using namespace metal;
struct type_Context { float brightness; float contrast; float2 unused; };
struct main0_out { float4 out_var_SV_Target [[color(0)]]; };
struct main0_in { float4 in_var_COLOR0 [[user(locn0)]]; float2 in_var_TEXCOORD0 [[user(locn1)]]; };
fragment main0_out main0(main0_in in [[stage_in]], constant type_Context& Context [[buffer(0)]], texture2d<float> u_texture [[texture(0)]], sampler u_sampler [[sampler(0)]])
{
    main0_out out = {};
    float4 color = u_texture.sample(u_sampler, in.in_var_TEXCOORD0) * in.in_var_COLOR0;
    float value = max(color.x, max(color.y, color.z));
    float target = clamp(((value - 0.5) * Context.contrast + 0.5) * Context.brightness, 0.0, 1.0);
    float3 rgb = value > 0.0 ? color.xyz * (target / value) : float3(target);
    out.out_var_SV_Target = float4(rgb, color.w);
    return out;
}
)MSL";
