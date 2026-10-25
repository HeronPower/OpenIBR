/* Target-specific conversion/clamp preflight. No controller is linked here. */
#include <float.h>
#include <math.h>
#include <stdio.h>
int main(void)
{
    volatile double qn = NAN, pi = INFINITY, ni = -INFINITY;
    float fq = (float)qn, fp = (float)pi, fn = (float)ni;
    int supported = sizeof(float) == 4 && sizeof(double) == 8 && FLT_RADIX == 2 &&
        FLT_MANT_DIG == 24 && DBL_MANT_DIG == 53 && FLT_MAX_EXP == 128 && DBL_MAX_EXP == 1024 &&
        isnan(fq) && isinf(fp) && !signbit(fp) && isinf(fn) && signbit(fn) &&
        qn != 0.0 && fminf(fmaxf(fq, -1.0f), 1.0f) == -1.0f;
    printf("{\"float_bytes\":%zu,\"double_bytes\":%zu,\"FLT_RADIX\":%d,\"FLT_MANT_DIG\":%d,\"DBL_MANT_DIG\":%d,\"FLT_MAX_EXP\":%d,\"DBL_MAX_EXP\":%d,\"FLT_MAX\":\"%a\",\"quiet_nan_preserved\":%s,\"positive_infinity_preserved\":%s,\"negative_infinity_preserved\":%s,\"nan_nonzero_comparison\":%s,\"nan_clamp\":\"%a\",\"target_supported\":%s}\n",
           sizeof(float), sizeof(double), FLT_RADIX, FLT_MANT_DIG, DBL_MANT_DIG, FLT_MAX_EXP, DBL_MAX_EXP, (double)FLT_MAX,
           isnan(fq) ? "true" : "false", isinf(fp) && !signbit(fp) ? "true" : "false",
           isinf(fn) && signbit(fn) ? "true" : "false", qn != 0.0 ? "true" : "false",
           (double)fminf(fmaxf(fq, -1.0f), 1.0f), supported ? "true" : "false");
    return supported ? 0 : 2;
}
