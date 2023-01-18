// convert Half OverUnder to Interlace

Texture2D tex : register(t0);
SamplerState samp : register(s0);

cbuffer PS_CONSTANTS : register(b0)
{
	float height;
	float none;
	float dtop;
	float dbottom;
};

struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target
{
	float2 pos = input.Tex;

	if (pos.y >= dbottom) {
		return float4(0.0, 0.0, 0.0, 1.0);
	}

	if (fmod((pos.y - dtop) * height, 2) < 1.0) {
		// even
		pos.y = (dtop + pos.y) / 2;
	} else {
		// odd
		pos.y = (dbottom + pos.y) / 2;
	}

	return tex.Sample(samp, pos);
}
