
#ifndef QUANTIZATION
    // 255 or 1023 (Maximum quantized integer value)
    #define QUANTIZATION 255
#endif

Texture2D tex : register(t0);
Texture2D texDither : register(t1);
SamplerState samp : register(s0);
SamplerState sampDither : register(s1);

cbuffer PS_CONSTANTS : register(b0)
{
    float2 ditherCoordScale;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 pixel = tex.Sample(samp, input.Tex);

    float4 ditherValue = texDither.Sample(sampDither, input.Tex * ditherCoordScale);
    pixel = floor(pixel * QUANTIZATION + ditherValue) / QUANTIZATION;

    return pixel;
};
