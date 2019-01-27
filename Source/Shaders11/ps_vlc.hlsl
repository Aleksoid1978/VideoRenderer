// based on globPixelShaderDefault from code of VLC

cbuffer PS_CONSTANT_BUFFER : register(b0)
{
  float Opacity;
  float BoundaryX;
  float BoundaryY;
  float LuminanceScale;
};
cbuffer PS_COLOR_TRANSFORM : register(b1)
{
  float4x4 WhitePoint;
  float4x4 Colorspace;
  float4x4 Primaries;
};
Texture2D shaderTexture[4]; // legacy shader mode
//Texture2DArray shaderTexture[4];
SamplerState SamplerStates[2];

struct PS_INPUT
{
  float4 Position   : SV_POSITION;
  float3 Texture    : TEXCOORD0;
};

/* see http://filmicworlds.com/blog/filmic-tonemapping-operators/ */
inline float4 hable(float4 x) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A*x + (C*B))+(D*E))/(x * (A*x + B) + (D*F))) - E/F;
}

/* https://en.wikipedia.org/wiki/Hybrid_Log-Gamma#Technical_details */
inline float inverse_HLG(float x){
    const float B67_a = 0.17883277;
    const float B67_b = 0.28466892;
    const float B67_c = 0.55991073;
    const float B67_inv_r2 = 4.0; /* 1/0.5² */
    if (x <= 0.5)
        x = x * x * B67_inv_r2;
    else
        x = exp((x - B67_c) / B67_a) + B67_b;
    return x;
}

inline float4 sourceToLinear(float4 rgb) {
    return rgb;
}

inline float4 linearToDisplay(float4 rgb) {
    return rgb;
}

inline float4 transformPrimaries(float4 rgb) {
    return rgb;
}

inline float4 toneMapping(float4 rgb) {
    return rgb;
}

inline float4 adjustRange(float4 rgb) {
    return rgb;
}

inline float4 sampleTexture(SamplerState samplerState, float3 coords) {
    float4 sample;
    sample = shaderTexture[0].Sample(samplerState, coords); /* sampling routine in sample */
    return sample;
}

float4 main( PS_INPUT In ) : SV_TARGET
{
  float4 sample;
  
  if (In.Texture.x > BoundaryX || In.Texture.y > BoundaryY) 
      sample = sampleTexture( SamplerStates[1], In.Texture );
  else
      sample = sampleTexture( SamplerStates[0], In.Texture );
  float4 rgba = max(mul(mul(sample, WhitePoint), Colorspace),0);
  float opacity = rgba.a * Opacity;
  float4 rgb = rgba; rgb.a = 0;
  rgb = sourceToLinear(rgb);
  rgb = transformPrimaries(rgb);
  rgb = toneMapping(rgb);
  rgb = linearToDisplay(rgb);
  rgb = adjustRange(rgb);
  return float4(rgb.rgb, saturate(opacity));
}
