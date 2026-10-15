// Variant of ps_hdr10_tonemap.hlsl for Spline measured, which takes the content peak from the
// per-frame measurement (cs_hdr_hist.hlsl + cs_hdr_resolve.hlsl) instead of the metadata.
// Needs shader model 5.0 for the StructuredBuffer.
#define MEASURED 1
#include "ps_hdr10_tonemap.hlsl"
