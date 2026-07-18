#include "dcVoltageCntl_A_private_autogen.h"

// custom libraries
#include "../../helper_lib/helper_macros.h"
#include "../../helper_lib/control_lib.h"

// std libraries
#include <stdint.h>

static struct
{
    float over_VllRated;
} derived_config =
    {
        .over_VllRated = 1.0f,
};

void dcVoltageCntl_update_derived_config(void)
{

}

void dcVoltageCntl_init(void)
{
    derived_config.over_VllRated = 1.0f/fmaxf(dcVoltageCntl_CONFIG.VllRated, 1e-3f);
}

void dcVoltageCntl_isr(float t_step)
{
    (void) t_step;
}

void dcVoltageCntl_1kHz(float t_step)
{
    (void) t_step;
}

void dcVoltageCntl_5kHz(float t_step)
{
    // filtered voltage error
    static float vdc_sogi_integs[2] = {0.0f, 0.0f};
    float v_notched = dcVoltageCntl_IN.vdc - sogi_band_pass(vdc_sogi_integs, 
                                                            dcVoltageCntl_IN.vdc, 
                                                            dcVoltageCntl_CONFIG.sogiNotchAlpha, 
                                                            2.0f * dcVoltageCntl_IN.freq, 
                                                            t_step);

    float delta_v_notched = v_notched - dcVoltageCntl_IN.vdc_ref;
    static float delta_v_notched_lpf = 0.0f;
    delta_v_notched_lpf += (delta_v_notched - delta_v_notched_lpf) * dcVoltageCntl_CONFIG.wLowPass * t_step;

    float delta_v_deadband = delta_v_notched_lpf - clamp(delta_v_notched_lpf, -dcVoltageCntl_CONFIG.voltageDeadband, dcVoltageCntl_CONFIG.voltageDeadband);

    // controller
    static float output_slow_path = 0.0f;
    output_slow_path += ((delta_v_deadband * dcVoltageCntl_CONFIG.kSlowPath) - output_slow_path) * dcVoltageCntl_CONFIG.wSlowPath * t_step;
    output_slow_path = clamp(output_slow_path, dcVoltageCntl_CONFIG.slowPathMin, dcVoltageCntl_CONFIG.slowPathMax); // saturate

    float output_fast_path = delta_v_deadband * dcVoltageCntl_CONFIG.kp;

    float P_limit_high = fmaxf(dcVoltageCntl_IN.pLimitHigh, dcVoltageCntl_CONFIG.pLimitExternalMinimum);
    float P_limit_low = fminf(dcVoltageCntl_IN.pLimitLow, dcVoltageCntl_CONFIG.pLimitExternalMinimum);
    float P_request = clamp(output_fast_path + output_slow_path, P_limit_low, P_limit_high);
 
    // combine and limit
    uint16_t i;
    for (i = 0; i < 3; i++) {
        dcVoltageCntl_OUT.i_ref[i] = dcVoltageCntl_IN.osc[i].real * P_request * derived_config.over_VllRated * SQRT_2 * OVER_SQRT_3;
    }

    // record STATE
    dcVoltageCntl_STATE.powerRequest = P_request;
    dcVoltageCntl_STATE.vdcNotched = v_notched;
    dcVoltageCntl_STATE.vdcErrorNotchedLpf = delta_v_notched_lpf;
    dcVoltageCntl_STATE.vdcErrorDeadband = delta_v_deadband;
} 
