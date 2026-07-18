#include "currentLimiting_A_private_autogen.h"

#include "../../helper_lib/cmplx.h"
#include "../../helper_lib/helper_macros.h"
#include <stdint.h>

void soft_limit(float out[3], float in[3], float soft_limit, float hard_limit);

void currentLimiting_update_derived_config(void)
{
    // nothing to do
}

void currentLimiting_init(void)
{
    // Initialize AC monitoring system
}

void currentLimiting_isr(float t_step)
{
    (void) t_step;
}

void currentLimiting_1kHz(float t_step)
{
    (void) t_step;
}

void currentLimiting_5kHz(float t_step)
{
    // init
    uint16_t i;

    // analyzer_i
    static cmplx_t Iph_lpf[3];
    for (i = 0; i < 3; i++)
    {
        // rotate to pll frame
        cmplx_t Iph = cmplx_scale(cmplx_conjugate(currentLimiting_IN.osc[i]), currentLimiting_IN.i_ref[i] * SQRT_2);

        Iph_lpf[i].real += (Iph.real - Iph_lpf[i].real) * currentLimiting_CONFIG.wAnalyzer * t_step;
        Iph_lpf[i].imag += (Iph.imag - Iph_lpf[i].imag) * currentLimiting_CONFIG.wAnalyzer * t_step;
    }

    // excess sink
    float Iph_highest = fmaxf(  fmaxf( cmplx_mag(Iph_lpf[0]), cmplx_mag(Iph_lpf[1])), 
                                    cmplx_mag(Iph_lpf[2])  );
    
    float scaleFactor = currentLimiting_IN.I_limit * 1.0f/( fmaxf(fmaxf(currentLimiting_IN.I_limit, Iph_highest), 1e-3f) );

    cmplx_t Iexcess[3];
    float i_excess[3];
    for (i = 0; i < 3; i++)
    {
        Iexcess[i] = cmplx_scale(Iph_lpf[i], (1.0f - scaleFactor) * SQRT_2);
        i_excess[i] = Iexcess[i].real * currentLimiting_IN.osc[i].real - Iexcess[i].imag * currentLimiting_IN.osc[i].imag;
    }

    // apply to outputs
    float i_rms_limited[3];
    for (i = 0; i < 3; i++)
    {
        currentLimiting_STATE.i_excess[i] = i_excess[i];
        
        i_rms_limited[i] = currentLimiting_IN.i_ref[i] - i_excess[i];
    }

    // soft limit and copy to output
    soft_limit( currentLimiting_OUT.i_limited, 
                i_rms_limited,
                currentLimiting_CONFIG.peakSoftLimit * currentLimiting_CONFIG.IRated,
                currentLimiting_CONFIG.peakHardLimit * currentLimiting_CONFIG.IRated   );

}

void soft_limit(float out[3], float in[3], float soft_limit, float hard_limit)
{
    float headroom = hard_limit - soft_limit;
    float over_headroom = 1.0f/fmaxf(headroom, 1e-3f);

    unsigned int i;

    for(i=0; i<3; i++)
    {
        float soft_clamped = clamp(in[i], -soft_limit, soft_limit);

        // determine how much was clamped
        float excess = (in[i]-soft_clamped);

        // bend the 'excess' into an adder to the output
        float adder_pos = 1.0f - expf(fminf(-excess * over_headroom, 0.0f));
        float adder_neg = 1.0f - expf(fminf(excess * over_headroom, 0.0f));

        out[i] = soft_clamped + (adder_pos - adder_neg) * headroom;
    }
}
