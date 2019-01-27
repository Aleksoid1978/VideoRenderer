// based on blit_vs from code of mpv

void main(float2 pos : POSITION, float2 coord : TEXCOORD0,
          out float4 out_pos : SV_Position, out float2 out_coord : TEXCOORD0)
{
    out_pos = float4(pos, 0.0, 1.0);
    out_coord = coord;
}
