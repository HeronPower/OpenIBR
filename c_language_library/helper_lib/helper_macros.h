#ifndef HELPER_MACROS_H
#define HELPER_MACROS_H

    #include <math.h>

    #define clamp(value, min, max) fminf(fmaxf((value), (min)), (max))

    #define PI            (3.14159265358f)
    #define SQRT_2        (1.41421356237f)
    #define OVER_SQRT_2    (0.70710678118f)
    #define SQRT_3        (1.73205080757f)
    #define OVER_SQRT_3    (0.57735026919f)
    #define ONE_THIRD     (1.0f / 3.0f)

#endif // HELPER_MACROS_H 
