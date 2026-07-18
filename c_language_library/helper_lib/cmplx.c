#include "cmplx.h"
#include <math.h>

// Create a complex number
cmplx_t cmplx_create(float real, float imag)
{
    cmplx_t result = {real, imag};
    return result;
}

// Add two complex numbers
cmplx_t cmplx_add(cmplx_t a, cmplx_t b)
{
    cmplx_t result = {a.real + b.real, a.imag + b.imag};
    return result;
}

// Subtract two complex numbers
cmplx_t cmplx_sub(cmplx_t a, cmplx_t b)
{
    cmplx_t result = {a.real - b.real, a.imag - b.imag};
    return result;
}

// Scale a complex number by a real factor
cmplx_t cmplx_scale(cmplx_t a, float scale)
{
    cmplx_t result = {a.real * scale, a.imag * scale};
    return result;
}

// Multiply two complex numbers
cmplx_t cmplx_multiply(cmplx_t a, cmplx_t b)
{
    cmplx_t result = {
        a.real * b.real - a.imag * b.imag,
        a.real * b.imag + a.imag * b.real
    };
    return result;
}

// Complex conjugate
cmplx_t cmplx_conjugate(cmplx_t a)
{
    cmplx_t result = {a.real, -a.imag};
    return result;
} 

// Complex magnitude
float cmplx_mag(cmplx_t a)
{
    return sqrtf(a.real * a.real + a.imag * a.imag);
}
