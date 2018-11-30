sampler s0 : register(s0);

#define Y  pixel[0]
#define Cg pixel[1]
#define Co pixel[2]

float4 main(float2 tex : TEXCOORD0) : COLOR
{
	float4 pixel = tex2D(s0, tex); // original pixel
	pixel.yz = 2 * pixel.yz - 1;   // CgCo [0..1] to [-1..1]
	float tmp = Y - Cg;
	return float4(tmp + Co, Y + Cg, tmp - Co, 0);
}
