#include "circuitControls_B_private_autogen.h"

#include "../../helper_lib/helper_macros.h"
#include <stdint.h>

void circuitControls_update_derived_config(void)
{
    // nothing to do
}

// Circuit controls implementation -- same algorithm as circuit_controls_A,
// but v_ac/i_L come from IN (not ADCs), and v_bridge is returned directly
// instead of being converted to a PWM duty cycle (no hardware to drive).
void circuitControls_isr(float t_step)
{
    // init
    uint16_t i;

    float v_ac[3];
    float i_L[3];
    float v_dc = circuitControls_IN.v_dc;
    for (i = 0; i < 3; i++)
    {
        v_ac[i] = circuitControls_IN.v_ac[i];
        i_L[i] = circuitControls_IN.i_L[i];
    }

    // Track sensor DC offset only while disabled (no current can flow, so
    // whatever the ADC reads is offset); freeze it during operation.
    static float i_L_offset[3] = {0.0f, 0.0f, 0.0f};

    for (i = 0; i < 3; i++)
    {
        i_L_offset[i] += circuitControls_IN.enable ? 0.0f : (i_L[i] - i_L_offset[i]) * circuitControls_CONTROLLER_CFG.wOffsetCalibration * t_step;
        i_L[i] -= i_L_offset[i];
    }

    // circuit controller
    static float vCdamp[3] = {0.0f, 0.0f, 0.0f};
    float i_virtualDamp[3] = {0.0f, 0.0f, 0.0f};

    for (i = 0; i < 3; i++)
    {
        // virtual damper
        vCdamp[i] += (v_ac[i] - vCdamp[i]) * circuitControls_CONTROLLER_CFG.wDamp * t_step;
        i_virtualDamp[i] = (v_ac[i] - vCdamp[i]) * circuitControls_CONTROLLER_CFG.gDamp;
    }

    // current control
    float i_ref[3] = {0.0f, 0.0f, 0.0f};
    float i_err[3] = {0.0f, 0.0f, 0.0f};
    static float i_err_lpf[3] = {0.0f, 0.0f, 0.0f};
    float v_bridge[3] = {0.0f, 0.0f, 0.0f};
    float v_bridge_cm = 0.0f;
    for (i = 0; i < 3; i++)
    {
        i_ref[i] = circuitControls_IN.i_ref[i] - i_virtualDamp[i];
        i_err[i] = (i_ref[i] - i_L[i]);

        i_err_lpf[i] += (i_err[i] - i_err_lpf[i]) * circuitControls_CONTROLLER_CFG.wSlowPath * t_step;
        v_bridge[i] =   circuitControls_CONTROLLER_CFG.kFeedFwd * v_ac[i]
                      + circuitControls_CONTROLLER_CFG.kP * i_err[i]
                      + circuitControls_CONTROLLER_CFG.kSlowPath * i_err_lpf[i];
        v_bridge_cm += v_bridge[i] * ONE_THIRD;
        // The bridge can't output more than half the DC bus voltage per phase.
        v_bridge[i] = clamp(v_bridge[i], -0.5f * v_dc, 0.5f * v_dc);
    }
    (void)v_bridge_cm; /* only needed for the duty conversion circuit_controls_A does; not applicable here */

    // update outputs
    for (i = 0; i < 3; i++)
    {
        circuitControls_OUT.v_bridge[i] = v_bridge[i];
    }
}
