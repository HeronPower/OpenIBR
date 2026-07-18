#ifndef CONTROL_LIB_H
#define CONTROL_LIB_H

#include "helper_macros.h"

static inline float sogi_band_pass(float integs[2], 
                                   float input, 
                                   float alpha, 
                                   float f_notch, 
                                   float t_step) {
    float k_resonator = 2.0f * PI * f_notch * t_step;
    
    float error = input - integs[0];

    integs[0] += (alpha * error - integs[1]) * k_resonator;

    integs[1] += integs[0] * k_resonator;

    return integs[0];
}

#endif // CONTROL_LIB_H
