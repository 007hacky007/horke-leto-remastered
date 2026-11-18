// HL.EXE gfx_bright/gfx_contrast (0x32f90): RGB->HSV, V'=((V-0.5)*contrast+0.5)*brightness clamped, back to RGB.
// Hue and saturation stay, so the colour scales by V'/V; black (S undefined) becomes grey V'.
Texture2D u_texture : register(t0, space2);
SamplerState u_sampler : register(s0, space2);

cbuffer Context : register(b0, space3) {
    float brightness;
    float contrast;
    float2 unused;
};

// SDL's GPU renderer shaders (built with shadercross) link these as TEXCOORD0 and TEXCOORD1;
// D3D12 matches stages by semantic name, so the colour must not be called COLOR0.
struct PSInput {
    float4 v_color : TEXCOORD0;
    float2 v_uv : TEXCOORD1;
};

float4 main(PSInput input) : SV_Target {
    float4 color = u_texture.Sample(u_sampler, input.v_uv) * input.v_color;
    float value = max(color.r, max(color.g, color.b));
    float target = saturate(((value - 0.5) * contrast + 0.5) * brightness);
    float3 rgb = value > 0.0 ? color.rgb * (target / value) : float3(target, target, target);
    return float4(rgb, color.a);
}
