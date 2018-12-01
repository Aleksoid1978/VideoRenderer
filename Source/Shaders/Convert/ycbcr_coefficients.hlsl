#ifndef STD
    #define STD 709
#endif

#if (STD == 601)
// Rec.601/BT.601, SMPTE 170M
#define Kr 0.299
#define Kg 0.587
#define Kb 0.114

#elif (STD == 709)
// Rec.709/BT.709
#define Kr 0.2126
#define Kg 0.7152
#define Kb 0.0722

#elif (STD == 240)
// SMPTE 240M
#define Kr 0.212
#define Kg 0.701
#define Kb 0.212

#elif (STD == FCC)
// FCC
#define Kr 0.3
#define Kg 0.59
#define Kb 0.11

#endif