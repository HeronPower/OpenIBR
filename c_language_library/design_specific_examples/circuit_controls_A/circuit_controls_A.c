#include "circuitControls_A_private_autogen.h"
#include "../../../application/peripherals/peripherals.h"

#include <stdint.h>

void circuitControls_update_derived_config(void)
{
    // nothing to do
}

// Circuit controls implementation
void circuitControls_isr(float t_step)
{
    // init 
    uint16_t i;

    // read adcs
    static float v_dc = 0.0f;
    static float v_ac[3] = {0.0f, 0.0f, 0.0f};
    static float i_L[3] = {0.0f, 0.0f, 0.0f};
    get_sensor_values(&v_dc, v_ac, i_L);

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
    float duty[3] = {0.0f, 0.0f, 0.0f};
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
        v_bridge[i] = clamp(v_bridge[i], 
                            circuitControls_LIMITS_CFG.VBridgeLower, 
                            circuitControls_LIMITS_CFG.VBridgeUpper);
    }

    for(i = 0; i < 3; i++)
    {
        duty[i] = 0.5f + (v_bridge[i] - v_bridge_cm) * 1.0f / fmaxf(v_dc, 1.0f);
        duty[i] = clamp(duty[i], 0.0f, 1.0f);
    }

    update_duty(duty, circuitControls_IN.enable);
    contactor_enable(circuitControls_IN.enable);
    
    // update outputs
    for(i = 0; i < 3; i++)
	{	
		circuitControls_OUT.v_ac[i] = v_ac[i];
        circuitControls_OUT.i_L[i] = i_L[i];
        circuitControls_STATE.duty[i] = duty[i];
    }
    circuitControls_OUT.v_dc = v_dc;
    
}
