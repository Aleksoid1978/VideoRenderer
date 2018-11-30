sampler s0 : register(s0);
float2 dxdy : register(c0);

float4 main(float2 tex : TEXCOORD0) : COLOR
{
	float4 c0 = tex2D(s0, (tex+.5)*dxdy); // nearest neighbor
	c0 = 1.0 - c0; // invert
	return c0;
}
