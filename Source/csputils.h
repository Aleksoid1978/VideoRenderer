// Used code from project mpv
// https://github.com/mpv-player/mpv/blob/master/video/csputils.h

#pragma once

/* NOTE: the csp and levels AUTO values are converted to specific ones
 * above vf/vo level. At least vf_scale relies on all valid settings being
 * nonzero at vf/vo level.
 */

enum mp_csp {
	MP_CSP_AUTO,
	MP_CSP_BT_601,
	MP_CSP_BT_709,
	MP_CSP_SMPTE_240M,
	MP_CSP_BT_2020_NC,
	MP_CSP_BT_2020_C,
	MP_CSP_RGB,
	MP_CSP_XYZ,
	MP_CSP_YCGCO,
	MP_CSP_COUNT
};

enum mp_csp_levels {
	MP_CSP_LEVELS_AUTO,
	MP_CSP_LEVELS_TV,
	MP_CSP_LEVELS_PC,
	MP_CSP_LEVELS_COUNT,
};

enum mp_csp_prim {
    MP_CSP_PRIM_AUTO,
    MP_CSP_PRIM_BT_601_525,
    MP_CSP_PRIM_BT_601_625,
    MP_CSP_PRIM_BT_709,
    MP_CSP_PRIM_BT_2020,
    MP_CSP_PRIM_BT_470M,
    MP_CSP_PRIM_APPLE,
    MP_CSP_PRIM_ADOBE,
    MP_CSP_PRIM_PRO_PHOTO,
    MP_CSP_PRIM_CIE_1931,
    MP_CSP_PRIM_DCI_P3,
    MP_CSP_PRIM_DISPLAY_P3,
    MP_CSP_PRIM_V_GAMUT,
    MP_CSP_PRIM_S_GAMUT,
    MP_CSP_PRIM_COUNT
};

enum mp_csp_trc {
    MP_CSP_TRC_AUTO,
    MP_CSP_TRC_BT_1886,
    MP_CSP_TRC_SRGB,
    MP_CSP_TRC_LINEAR,
    MP_CSP_TRC_GAMMA18,
    MP_CSP_TRC_GAMMA20,
    MP_CSP_TRC_GAMMA22,
    MP_CSP_TRC_GAMMA24,
    MP_CSP_TRC_GAMMA26,
    MP_CSP_TRC_GAMMA28,
    MP_CSP_TRC_PRO_PHOTO,
    MP_CSP_TRC_PQ,
    MP_CSP_TRC_HLG,
    MP_CSP_TRC_V_LOG,
    MP_CSP_TRC_S_LOG1,
    MP_CSP_TRC_S_LOG2,
    MP_CSP_TRC_COUNT
};

enum mp_csp_light {
    MP_CSP_LIGHT_AUTO,
    MP_CSP_LIGHT_DISPLAY,
    MP_CSP_LIGHT_SCENE_HLG,
    MP_CSP_LIGHT_SCENE_709_1886,
    MP_CSP_LIGHT_SCENE_1_2,
    MP_CSP_LIGHT_COUNT
};

struct mp_colorspace {
	enum mp_csp space;
	enum mp_csp_levels levels;
	enum mp_csp_prim primaries;
	enum mp_csp_trc gamma;
	enum mp_csp_light light;
	float sig_peak; // highest relative value in signal. 0 = unknown/auto
};

struct mp_csp_params {
	struct mp_colorspace color = {MP_CSP_BT_709, MP_CSP_LEVELS_TV}; // input colorspace
	enum mp_csp_levels levels_out = MP_CSP_LEVELS_PC; // output device
	float brightness = 0; //    -1..0..1
	float contrast   = 1; //     0..1..2
	float hue        = 0; //   -pi..0..pi
	float saturation = 1; //     0..1..2
	float gamma      = 1; // 0.125..1..8
	// discard U/V components
	bool gray        = false;
	// texture_bits/input_bits is for rescaling fixed point input to range [0,1]
	int texture_bits = 8;
	int input_bits   = 8;
};

/* Color conversion matrix: RGB = m * YUV + c
 * m is in row-major matrix, with m[row][col], e.g.:
 *     [ a11 a12 a13 ]     float m[3][3] = { { a11, a12, a13 },
 *     [ a21 a22 a23 ]                       { a21, a22, a23 },
 *     [ a31 a32 a33 ]                       { a31, a32, a33 } };
 * This is accessed as e.g.: m[2-1][1-1] = a21
 * In particular, each row contains all the coefficients for one of R, G, B,
 * while each column contains all the coefficients for one of Y, U, V:
 *     m[r,g,b][y,u,v] = ...
 * The matrix could also be viewed as group of 3 vectors, e.g. the 1st column
 * is the Y vector (1, 1, 1), the 2nd is the U vector, the 3rd the V vector.
 * The matrix might also be used for other conversions and colorspaces.
 */
struct mp_cmat {
	float m[3][3];
	float c[3];
};

double mp_get_csp_mul(enum mp_csp csp, int input_bits, int texture_bits);
void mp_get_csp_matrix(struct mp_csp_params *params, struct mp_cmat *out);

void mp_invert_matrix3x3(float m[3][3]);
void mp_invert_cmat(struct mp_cmat *out, struct mp_cmat *in);
