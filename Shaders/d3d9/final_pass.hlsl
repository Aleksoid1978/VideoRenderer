
#ifndef QUANTIZATION
	// 255 or 1023 (Maximum quantized integer value)
	#define QUANTIZATION 255
#endif

sampler image : register(s0);
sampler ditherMatrix : register(s1);
float2 ditherMatrixCoordScale : register(c0);

float4 main(float2 imageCoord : TEXCOORD0) : COLOR
{
	float4 pixel = tex2D(image, imageCoord);

	float4 ditherValue = tex2D(ditherMatrix, imageCoord * ditherMatrixCoordScale);
	pixel = floor(pixel * QUANTIZATION + ditherValue) / QUANTIZATION;

	return pixel;
};
