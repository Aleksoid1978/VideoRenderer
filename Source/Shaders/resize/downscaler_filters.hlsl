#define PI acos(-1.)

#ifndef FILTER
    #define FILTER 2
#endif

#if (FILTER == 0)

// box
#define filter_support (0.5)
inline float filter(float x)
{
    if (x >= -0.5 && x < 0.5)
        return 1.0;
    return 0.0;
}

#elif (FILTER == 1)

// bilinear
#define filter_support (1.0)
inline float filter(float x)
{
    if (x < 0.0)
        x = -x;
    if (x < 1.0)
        return 1.0 - x;
    return 0.0;
}

#elif (FILTER == 2)

// hamming
#define filter_support (1.0)
inline float filter(float x)
{
    if (x < 0.0)
        x = -x;
    if (x == 0.0)
        return 1.0;
    if (x >= 1.0)
        return 0.0;
    x *= PI;
    return sin(x) / x * (0.54 + 0.46 * cos(x));
}

#elif (FILTER == 3)

// bicubic
// https://en.wikipedia.org/wiki/Bicubic_interpolation#Bicubic_convolution_algorithm
#ifndef A
#define A -0.5
#endif
#define filter_support (2.0)
inline float filter(float x)
{
    if (x < 0.0)
        x = -x;
    if (x < 1.0)
        return ((A + 2.0) * x - (A + 3.0)) * x*x + 1;
    if (x < 2.0)
        return (((x - 5) * x + 8) * x - 4) * A;
    return 0.0;
}
#undef A

#elif (FILTER == 4)

// lanczos
#define filter_support (3.0)
inline float sinc_filter(float x)
{
    if (x == 0.0)
        return 1.0;
    x *= PI;
    return sin(x) / x;
}
inline float filter(float x)
{
    /* truncated sinc */
    if (-3.0 <= x && x < 3.0)
        return sinc_filter(x) * sinc_filter(x/3);
    return 0.0;
}

#endif
