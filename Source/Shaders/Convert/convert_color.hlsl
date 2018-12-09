sampler s0 : register(s0);
float3 cm_r : register(c0);
float3 cm_g : register(c1);
float3 cm_b : register(c2);
float3 cm_c : register(c3);

float4 main(float2 tex : TEXCOORD0) : COLOR
{
    float4 color = tex2D(s0, tex); // original pixel
    color.rgb = float3(mul(cm_r, color.rgb), mul(cm_g, color.rgb), mul(cm_b, color.rgb)) + cm_c;

    return color;
}
