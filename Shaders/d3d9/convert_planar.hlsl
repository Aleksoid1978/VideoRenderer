sampler sY : register(s0);
sampler sV : register(s1);
sampler sU : register(s2);

float4 cm_r : register(c0);
float4 cm_g : register(c1);
float4 cm_b : register(c2);
float3 cm_c : register(c3);

float4 main(float2 tex : TEXCOORD0) : COLOR
{
    float colorY = tex2D(sY, tex).r;
    float colorU = tex2D(sU, tex).r;
    float colorV = tex2D(sV, tex).r;

    float4 color = float4(colorY, colorU, colorV, 0);

    color.rgb = float3(mul(cm_r, color), mul(cm_g, color), mul(cm_b, color)) + cm_c;

    return color;
}
