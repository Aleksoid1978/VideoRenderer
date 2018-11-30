sampler s0 : register(s0);

// http://avisynth.nl/index.php/Colorimetry

#ifndef Kr
    #define Kr  0.2126
#endif
#ifndef Kg
    #define Kg  0.7152
#endif
#ifndef Kb
    #define Kb  0.0722
#endif

#define Y  pixel[0]
#define Cb pixel[1]
#define Cr pixel[2]

float4 main(float2 tex : TEXCOORD0) : COLOR
{
    float4 pixel = tex2D(s0, tex); // original pixel
    pixel.yz = pixel.yz * 2 - 1;   // CbCr [0;1] to [-1;1]

    return float4(Y + Cr * (1-Kr), Y - Cb * ((1-Kb)*Kb/Kg) - Cr * ((1-Kr)*Kr/Kg), Y + Cb * (1-Kb), 0);
}
