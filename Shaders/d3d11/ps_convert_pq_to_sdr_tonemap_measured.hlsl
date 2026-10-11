// Variant of ps_convert_pq_to_sdr_tonemap.hlsl that takes the content peak from the per-frame measurement
// (cs_hdr_hist.hlsl + cs_hdr_resolve.hlsl) instead of the file's metadata.
// Needs shader model 5.0 for the StructuredBuffer.
#define MEASURED 1
#include "ps_convert_pq_to_sdr_tonemap.hlsl"
