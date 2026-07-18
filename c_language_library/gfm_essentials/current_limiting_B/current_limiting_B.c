#include "currentLimiting_B_private_autogen.h"

#include "../../helper_lib/control_lib.h"
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
    // Initialize current limiting
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
    static float i_ref_sogi_integs[3][2] = { {0.0f, 0.0f}, 
                                             {0.0f, 0.0f}, 
                                             {0.0f, 0.0f} };
    for (i = 0; i < 3; i++)
    {
        currentLimiting_STATE.i_ref_band_pass[i] = sogi_band_pass(i_ref_sogi_integs[i], currentLimiting_IN.i_ref[i], currentLimiting_CONFIG.sogiNotchAlpha, currentLimiting_IN.freq, t_step);
        
        // calculate rms phase current
        static float Iph_squared_lpf[3];
        Iph_squared_lpf[i] += (currentLimiting_STATE.i_ref_band_pass[i] * currentLimiting_STATE.i_ref_band_pass[i] - Iph_squared_lpf[i]) * currentLimiting_CONFIG.wAnalyzer * t_step;

        currentLimiting_STATE.Iph[i] = sqrtf(fmaxf(Iph_squared_lpf[i], 0.0f));
    }

    // excess sink
    float Iph_highest = fmaxf(fmaxf(currentLimiting_STATE.Iph[0], currentLimiting_STATE.Iph[1]), currentLimiting_STATE.Iph[2]);
    
    float scaleFactor = currentLimiting_IN.I_limit * 1.0f/( fmaxf(fmaxf(currentLimiting_IN.I_limit, Iph_highest), 1e-3f) );     // max with 1e-3 to protect from division by zero

    float i_excess[3];
    for (i = 0; i < 3; i++)
    {
        i_excess[i] = currentLimiting_STATE.i_ref_band_pass[i] * (1.0f - scaleFactor);
    }

    // apply to outputs
    float i_net[3];
    for (i = 0; i < 3; i++)
    {
        currentLimiting_STATE.i_excess[i] = i_excess[i];
        
        i_net[i] = currentLimiting_IN.i_ref[i] - i_excess[i];
    }

    // soft limit and copy to output
    soft_limit( currentLimiting_OUT.i_limited, 
                i_net,
                currentLimiting_CONFIG.peakSoftLimit * currentLimiting_CONFIG.IRated,
                currentLimiting_CONFIG.peakHardLimit * currentLimiting_CONFIG.IRated );
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
