#ifndef CMPLX_H
#define CMPLX_H

// Complex number typedef
typedef struct {
    float real;
    float imag;
} cmplx_t;

// Function declarations
cmplx_t cmplx_create(float real, float imag);
cmplx_t cmplx_add(cmplx_t a, cmplx_t b);
cmplx_t cmplx_sub(cmplx_t a, cmplx_t b);
cmplx_t cmplx_scale(cmplx_t a, float scale);
cmplx_t cmplx_multiply(cmplx_t a, cmplx_t b);
cmplx_t cmplx_conjugate(cmplx_t a);
float cmplx_mag(cmplx_t a);


#endif // CMPLX_H 
