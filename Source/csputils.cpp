// Used code from project mpv
// https://github.com/mpv-player/mpv/blob/master/video/csputils.c

#include "stdafx.h"

#include <stdint.h>
#include <math.h>
#include <assert.h>

#include "csputils.h"

#pragma warning(disable:4305)

void mp_invert_matrix3x3(float m[3][3])
{
    float m00 = m[0][0], m01 = m[0][1], m02 = m[0][2],
          m10 = m[1][0], m11 = m[1][1], m12 = m[1][2],
          m20 = m[2][0], m21 = m[2][1], m22 = m[2][2];

    // calculate the adjoint
    m[0][0] =  (m11 * m22 - m21 * m12);
    m[0][1] = -(m01 * m22 - m21 * m02);
    m[0][2] =  (m01 * m12 - m11 * m02);
    m[1][0] = -(m10 * m22 - m20 * m12);
    m[1][1] =  (m00 * m22 - m20 * m02);
    m[1][2] = -(m00 * m12 - m10 * m02);
    m[2][0] =  (m10 * m21 - m20 * m11);
    m[2][1] = -(m00 * m21 - m20 * m01);
    m[2][2] =  (m00 * m11 - m10 * m01);

    // calculate the determinant (as inverse == 1/det * adjoint,
    // adjoint * m == identity * det, so this calculates the det)
    float det = m00 * m[0][0] + m10 * m[0][1] + m20 * m[0][2];
    det = 1.0f / det;

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++)
            m[i][j] *= det;
    }
}

// A := A * B
static void mp_mul_matrix3x3(float a[3][3], float b[3][3])
{
    float a00 = a[0][0], a01 = a[0][1], a02 = a[0][2],
          a10 = a[1][0], a11 = a[1][1], a12 = a[1][2],
          a20 = a[2][0], a21 = a[2][1], a22 = a[2][2];

    for (int i = 0; i < 3; i++) {
        a[0][i] = a00 * b[0][i] + a01 * b[1][i] + a02 * b[2][i];
        a[1][i] = a10 * b[0][i] + a11 * b[1][i] + a12 * b[2][i];
        a[2][i] = a20 * b[0][i] + a21 * b[1][i] + a22 * b[2][i];
    }
}

// return the primaries associated with a certain mp_csp_primaries val
struct mp_csp_primaries mp_get_csp_primaries(enum mp_csp_prim spc)
{
    /*
    Values from: ITU-R Recommendations BT.470-6, BT.601-7, BT.709-5, BT.2020-0

    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.470-6-199811-S!!PDF-E.pdf
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.601-7-201103-I!!PDF-E.pdf
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.709-5-200204-I!!PDF-E.pdf
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2020-0-201208-I!!PDF-E.pdf

    Other colorspaces from https://en.wikipedia.org/wiki/RGB_color_space#Specifications
    */

    // CIE standard illuminant series
    static const struct mp_csp_col_xy
        d50 = {0.34577, 0.35850},
        d65 = {0.31271, 0.32902},
        c   = {0.31006, 0.31616},
        dci = {0.31400, 0.35100},
        e   = {1.0/3.0, 1.0/3.0};

    switch (spc) {
    case MP_CSP_PRIM_BT_470M:
        return {
            {0.670, 0.330},
            {0.210, 0.710},
            {0.140, 0.080},
            c
        };
    case MP_CSP_PRIM_BT_601_525:
        return {
            {0.630, 0.340},
            {0.310, 0.595},
            {0.155, 0.070},
            d65
        };
    case MP_CSP_PRIM_BT_601_625:
        return {
            {0.640, 0.330},
            {0.290, 0.600},
            {0.150, 0.060},
            d65
        };
    // This is the default assumption if no colorspace information could
    // be determined, eg. for files which have no video channel.
    case MP_CSP_PRIM_AUTO:
    case MP_CSP_PRIM_BT_709:
        return {
            {0.640, 0.330},
            {0.300, 0.600},
            {0.150, 0.060},
            d65
        };
    case MP_CSP_PRIM_BT_2020:
        return {
            {0.708, 0.292},
            {0.170, 0.797},
            {0.131, 0.046},
            d65
        };
    case MP_CSP_PRIM_APPLE:
        return {
            {0.625, 0.340},
            {0.280, 0.595},
            {0.115, 0.070},
            d65
        };
    case MP_CSP_PRIM_ADOBE:
        return {
            {0.640, 0.330},
            {0.210, 0.710},
            {0.150, 0.060},
            d65
        };
    case MP_CSP_PRIM_PRO_PHOTO:
        return {
            {0.7347, 0.2653},
            {0.1596, 0.8404},
            {0.0366, 0.0001},
            d50
        };
    case MP_CSP_PRIM_CIE_1931:
        return {
            {0.7347, 0.2653},
            {0.2738, 0.7174},
            {0.1666, 0.0089},
            e
        };
    // From SMPTE RP 431-2 and 432-1
    case MP_CSP_PRIM_DCI_P3:
    case MP_CSP_PRIM_DISPLAY_P3:
        return {
            {0.680, 0.320},
            {0.265, 0.690},
            {0.150, 0.060},
            spc == MP_CSP_PRIM_DCI_P3 ? dci : d65
        };
    // From Panasonic VARICAM reference manual
    case MP_CSP_PRIM_V_GAMUT:
        return {
            {0.730, 0.280},
            {0.165, 0.840},
            {0.100, -0.03},
            d65
        };
    // From Sony S-Log reference manual
    case MP_CSP_PRIM_S_GAMUT:
        return {
            {0.730, 0.280},
            {0.140, 0.855},
            {0.100, -0.05},
            d65
        };
    // from EBU Tech. 3213-E
    case MP_CSP_PRIM_EBU_3213:
        return {
            {0.630, 0.340},
            {0.295, 0.605},
            {0.155, 0.077},
            d65
        };
    // From H.273, traditional film with Illuminant C
    case MP_CSP_PRIM_FILM_C:
        return {
            {0.681, 0.319},
            {0.243, 0.692},
            {0.145, 0.049},
            c
        };
    // From libplacebo source code
    case MP_CSP_PRIM_ACES_AP0:
        return {
            {0.7347, 0.2653},
            {0.0000, 1.0000},
            {0.0001, -0.0770},
            {0.32168, 0.33767},
        };
    // From libplacebo source code
    case MP_CSP_PRIM_ACES_AP1:
        return {
            {0.713, 0.293},
            {0.165, 0.830},
            {0.128, 0.044},
            {0.32168, 0.33767},
        };
    default:
        return {{0}};
    }
}

// Get the nominal peak for a given colorspace, relative to the reference white
// level. In other words, this returns the brightest encodable value that can
// be represented by a given transfer curve.
float mp_trc_nom_peak(enum mp_csp_trc trc)
{
    switch (trc) {
    case MP_CSP_TRC_PQ:           return 10000.0 / MP_REF_WHITE;
    case MP_CSP_TRC_HLG:          return 12.0 / MP_REF_WHITE_HLG;
    case MP_CSP_TRC_V_LOG:        return 46.0855;
    case MP_CSP_TRC_S_LOG1:       return 6.52;
    case MP_CSP_TRC_S_LOG2:       return 9.212;
    }

    return 1.0;
}

bool mp_trc_is_hdr(enum mp_csp_trc trc)
{
    return mp_trc_nom_peak(trc) > 1.0;
}

// Compute the RGB/XYZ matrix as described here:
// http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
void mp_get_rgb2xyz_matrix(struct mp_csp_primaries space, float m[3][3])
{
    float S[3], X[4], Z[4];

    // Convert from CIE xyY to XYZ. Note that Y=1 holds true for all primaries
    X[0] = space.red.x   / space.red.y;
    X[1] = space.green.x / space.green.y;
    X[2] = space.blue.x  / space.blue.y;
    X[3] = space.white.x / space.white.y;

    Z[0] = (1 - space.red.x   - space.red.y)   / space.red.y;
    Z[1] = (1 - space.green.x - space.green.y) / space.green.y;
    Z[2] = (1 - space.blue.x  - space.blue.y)  / space.blue.y;
    Z[3] = (1 - space.white.x - space.white.y) / space.white.y;

    // S = XYZ^-1 * W
    for (int i = 0; i < 3; i++) {
        m[0][i] = X[i];
        m[1][i] = 1;
        m[2][i] = Z[i];
    }

    mp_invert_matrix3x3(m);

    for (int i = 0; i < 3; i++)
        S[i] = m[i][0] * X[3] + m[i][1] * 1 + m[i][2] * Z[3];

    // M = [Sc * XYZc]
    for (int i = 0; i < 3; i++) {
        m[0][i] = S[i] * X[i];
        m[1][i] = S[i] * 1;
        m[2][i] = S[i] * Z[i];
    }
}

// M := M * XYZd<-XYZs
static void mp_apply_chromatic_adaptation(struct mp_csp_col_xy src,
                                          struct mp_csp_col_xy dest, float m[3][3])
{
    // If the white points are nearly identical, this is a wasteful identity
    // operation.
    if (fabs(src.x - dest.x) < 1e-6 && fabs(src.y - dest.y) < 1e-6)
        return;

    // XYZd<-XYZs = Ma^-1 * (I*[Cd/Cs]) * Ma
    // http://www.brucelindbloom.com/index.html?Eqn_ChromAdapt.html
    float C[3][2], tmp[3][3] = {{0}};

    // Ma = Bradford matrix, arguably most popular method in use today.
    // This is derived experimentally and thus hard-coded.
    float bradford[3][3] = {
        {  0.8951,  0.2664, -0.1614 },
        { -0.7502,  1.7135,  0.0367 },
        {  0.0389, -0.0685,  1.0296 },
    };

    for (int i = 0; i < 3; i++) {
        // source cone
        C[i][0] = bradford[i][0] * mp_xy_X(src)
                + bradford[i][1] * 1
                + bradford[i][2] * mp_xy_Z(src);

        // dest cone
        C[i][1] = bradford[i][0] * mp_xy_X(dest)
                + bradford[i][1] * 1
                + bradford[i][2] * mp_xy_Z(dest);
    }

    // tmp := I * [Cd/Cs] * Ma
    for (int i = 0; i < 3; i++)
        tmp[i][i] = C[i][1] / C[i][0];

    mp_mul_matrix3x3(tmp, bradford);

    // M := M * Ma^-1 * tmp
    mp_invert_matrix3x3(bradford);
    mp_mul_matrix3x3(m, bradford);
    mp_mul_matrix3x3(m, tmp);
}

// get the coefficients of an ST 428-1 xyz -> rgb conversion matrix
// intent = the rendering intent used to convert to the target primaries
static void mp_get_xyz2rgb_coeffs(struct mp_csp_params *params,
                                  enum mp_render_intent intent, struct mp_cmat *m)
{
    // Convert to DCI-P3
    struct mp_csp_primaries prim = mp_get_csp_primaries(MP_CSP_PRIM_DCI_P3);
    float brightness = params->brightness;
    mp_get_rgb2xyz_matrix(prim, m->m);
    mp_invert_matrix3x3(m->m);

    // All non-absolute mappings want to map source white to target white
    if (intent != MP_INTENT_ABSOLUTE_COLORIMETRIC) {
        // SMPTE 428-1 defines the calibration white point as CIE xy (0.314, 0.351)
        static const struct mp_csp_col_xy smpte428 = {0.314, 0.351};
        mp_apply_chromatic_adaptation(smpte428, prim.white, m->m);
    }

    // Since this outputs linear RGB rather than companded RGB, we
    // want to linearize any brightness additions. 2 is a reasonable
    // approximation for any sort of gamma function that could be in use.
    // As this is an aesthetic setting only, any exact values do not matter.
    brightness *= fabs(brightness);

    for (int i = 0; i < 3; i++)
        m->c[i] = brightness;
}

// Get multiplication factor required if image data is fit within the LSBs of a
// higher smaller bit depth fixed-point texture data.
// This is broken. Use mp_get_csp_uint_mul().
double mp_get_csp_mul(enum mp_csp csp, int input_bits, int texture_bits)
{
    assert(texture_bits >= input_bits);

    // Convenience for some irrelevant cases, e.g. rgb565 or disabling expansion.
    if (!input_bits)
        return 1;

    // RGB always uses the full range available.
    if (csp == MP_CSP_RGB)
        return ((1LL << input_bits) - 1.) / ((1LL << texture_bits) - 1.);

    if (csp == MP_CSP_XYZ)
        return 1;

    // High bit depth YUV uses a range shifted from 8 bit.
    return (1LL << input_bits) / ((1LL << texture_bits) - 1.) * 255 / 256;
}

/* Fill in the Y, U, V vectors of a yuv-to-rgb conversion matrix
 * based on the given luma weights of the R, G and B components (lr, lg, lb).
 * lr+lg+lb is assumed to equal 1.
 * This function is meant for colorspaces satisfying the following
 * conditions (which are true for common YUV colorspaces):
 * - The mapping from input [Y, U, V] to output [R, G, B] is linear.
 * - Y is the vector [1, 1, 1].  (meaning input Y component maps to 1R+1G+1B)
 * - U maps to a value with zero R and positive B ([0, x, y], y > 0;
 *   i.e. blue and green only).
 * - V maps to a value with zero B and positive R ([x, y, 0], x > 0;
 *   i.e. red and green only).
 * - U and V are orthogonal to the luma vector [lr, lg, lb].
 * - The magnitudes of the vectors U and V are the minimal ones for which
 *   the image of the set Y=[0...1],U=[-0.5...0.5],V=[-0.5...0.5] under the
 *   conversion function will cover the set R=[0...1],G=[0...1],B=[0...1]
 *   (the resulting matrix can be converted for other input/output ranges
 *   outside this function).
 * Under these conditions the given parameters lr, lg, lb uniquely
 * determine the mapping of Y, U, V to R, G, B.
 */
static void luma_coeffs(struct mp_cmat *mat, float lr, float lg, float lb)
{
    assert(fabs(lr+lg+lb - 1) < 1e-6);
    *mat = mp_cmat{
        { {1, 0,                    2 * (1-lr)          },
          {1, -2 * (1-lb) * lb/lg, -2 * (1-lr) * lr/lg  },
          {1,  2 * (1-lb),          0                   } },
        // Constant coefficients (mat->c) not set here
    };
}

// get the coefficients of the yuv -> rgb conversion matrix
void mp_get_csp_matrix(struct mp_csp_params *params, struct mp_cmat *m)
{
    enum mp_csp colorspace = params->color.space;
    if (colorspace <= MP_CSP_AUTO || colorspace >= MP_CSP_COUNT)
        colorspace = MP_CSP_BT_601;
    enum mp_csp_levels levels_in = params->color.levels;
    if (levels_in <= MP_CSP_LEVELS_AUTO || levels_in >= MP_CSP_LEVELS_COUNT)
        levels_in = MP_CSP_LEVELS_TV;

    switch (colorspace) {
    case MP_CSP_BT_601:     luma_coeffs(m, 0.299,  0.587,  0.114 ); break;
    case MP_CSP_BT_709:     luma_coeffs(m, 0.2126, 0.7152, 0.0722); break;
    case MP_CSP_SMPTE_240M: luma_coeffs(m, 0.2122, 0.7013, 0.0865); break;
    case MP_CSP_BT_2020_NC: luma_coeffs(m, 0.2627, 0.6780, 0.0593); break;
    case MP_CSP_BT_2020_C: {
        // Note: This outputs into the [-0.5,0.5] range for chroma information.
        // If this clips on any VO, a constant 0.5 coefficient can be added
        // to the chroma channels to normalize them into [0,1]. This is not
        // currently needed by anything, though.
        *m = mp_cmat{{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}};
        break;
    }
    case MP_CSP_RGB: {
        *m = mp_cmat{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
        levels_in = (mp_csp_levels)-1;
        break;
    }
    case MP_CSP_XYZ: {
        // The vo should probably not be using a matrix generated by this
        // function for XYZ sources, but if it does, let's just convert it to
        // an equivalent RGB space based on the colorimetry metadata it
        // provided in mp_csp_params. (At the risk of clipping, if the
        // chosen primaries are too small to fit the actual data)
        mp_get_xyz2rgb_coeffs(params, MP_INTENT_RELATIVE_COLORIMETRIC, m);
        levels_in = (mp_csp_levels)-1;
        break;
    }
    case MP_CSP_YCGCO: {
        *m = mp_cmat{
            {{1,  -1,  1},
             {1,   1,  0},
             {1,  -1, -1}},
        };
        break;
    }
    default:
        abort();
    };

    if (params->is_float)
        levels_in = (mp_csp_levels)-1;

    if ((colorspace == MP_CSP_BT_601 || colorspace == MP_CSP_BT_709 ||
         colorspace == MP_CSP_SMPTE_240M || colorspace == MP_CSP_BT_2020_NC))
    {
        // Hue is equivalent to rotating input [U, V] subvector around the origin.
        // Saturation scales [U, V].
        float huecos = params->gray ? 0 : params->saturation * cos(params->hue);
        float huesin = params->gray ? 0 : params->saturation * sin(params->hue);
        for (int i = 0; i < 3; i++) {
            float u = m->m[i][1], v = m->m[i][2];
            m->m[i][1] = huecos * u - huesin * v;
            m->m[i][2] = huesin * u + huecos * v;
        }
    }

    // The values below are written in 0-255 scale - thus bring s into range.
    double s =
        mp_get_csp_mul(colorspace, params->input_bits, params->texture_bits) / 255;
    // NOTE: The yuvfull ranges as presented here are arguably ambiguous,
    // and conflict with at least the full-range YCbCr/ICtCp values as defined
    // by ITU-R BT.2100. If somebody ever complains about full-range YUV looking
    // different from their reference display, this comment is probably why.
    struct yuvlevels { double ymin, ymax, cmax, cmid; }
        yuvlim =  { 16*s, 235*s, 240*s, 128*s },
        yuvfull = {  0*s, 255*s, 255*s, 128*s },
        anyfull = {  0*s, 255*s, 255*s/2, 0 }, // cmax picked to make cmul=ymul
        yuvlev;
    switch (levels_in) {
    case MP_CSP_LEVELS_TV: yuvlev = yuvlim; break;
    case MP_CSP_LEVELS_PC: yuvlev = yuvfull; break;
    case -1: yuvlev = anyfull; break;
    default:
        abort();
    }

    int levels_out = params->levels_out;
    if (levels_out <= MP_CSP_LEVELS_AUTO || levels_out >= MP_CSP_LEVELS_COUNT)
        levels_out = MP_CSP_LEVELS_PC;
    struct rgblevels { double min, max; }
        rgblim =  { 16/255., 235/255. },
        rgbfull = {      0,        1  },
        rgblev;
    switch (levels_out) {
    case MP_CSP_LEVELS_TV: rgblev = rgblim; break;
    case MP_CSP_LEVELS_PC: rgblev = rgbfull; break;
    default:
        abort();
    }

    double ymul = (rgblev.max - rgblev.min) / (yuvlev.ymax - yuvlev.ymin);
    double cmul = (rgblev.max - rgblev.min) / (yuvlev.cmax - yuvlev.cmid) / 2;

    // Contrast scales the output value range (gain)
    ymul *= params->contrast;
    cmul *= params->contrast;

    for (int i = 0; i < 3; i++) {
        m->m[i][0] *= ymul;
        m->m[i][1] *= cmul;
        m->m[i][2] *= cmul;
        // Set c so that Y=umin,UV=cmid maps to RGB=min (black to black),
        // also add brightness offset (black lift)
        m->c[i] = rgblev.min - m->m[i][0] * yuvlev.ymin
                  - (m->m[i][1] + m->m[i][2]) * yuvlev.cmid
                  + params->brightness;
    }
}

void mp_invert_cmat(struct mp_cmat *out, struct mp_cmat *in)
{
    *out = *in;
    mp_invert_matrix3x3(out->m);

    // fix the constant coefficient
    // rgb = M * yuv + C
    // M^-1 * rgb = yuv + M^-1 * C
    // yuv = M^-1 * rgb - M^-1 * C
    //                  ^^^^^^^^^^
    out->c[0] = -(out->m[0][0] * in->c[0] + out->m[0][1] * in->c[1] + out->m[0][2] * in->c[2]);
    out->c[1] = -(out->m[1][0] * in->c[0] + out->m[1][1] * in->c[1] + out->m[1][2] * in->c[2]);
    out->c[2] = -(out->m[2][0] * in->c[0] + out->m[2][1] * in->c[1] + out->m[2][2] * in->c[2]);
}


/////////////////////
// additional code //
/////////////////////

void mul_matrix3x3(float a[3][3], float b[3][3])
{
    mp_mul_matrix3x3(a, b);
}

void transpose_matrix3x3(float t[3][3], const float m[3][3])
{
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            t[j][i] = m[i][j];
        }
    }
}

void GetColorspaceGamutConversionMatrix(float matrix[3][3], mp_csp_prim csp_in, mp_csp_prim csp_out)
{
	float matrix_rgb2xyz_in[3][3];
	mp_get_rgb2xyz_matrix(mp_get_csp_primaries(csp_in), matrix_rgb2xyz_in);

	mp_get_rgb2xyz_matrix(mp_get_csp_primaries(csp_out), matrix);
	mp_invert_matrix3x3(matrix);
	mp_mul_matrix3x3(matrix, matrix_rgb2xyz_in);
}
