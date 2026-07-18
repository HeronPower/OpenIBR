#include "modesAndProtection_A_private_autogen.h"

// custom libraries
#include "../../helper_lib/helper_macros.h"
#include "../../helper_lib/timers.h"

// std libraries
#include <stdint.h>

void modesAndProtection_update_derived_config(void)
{
    // nothing to do
}

void modesAndProtection_5kHz(float t_step)
{
    static float protection_clear_timer = 0.1f;     // start with safe value

    bool dcOv_flag = modesAndProtection_IN.vdc > modesAndProtection_CONFIG.dcOverVoltageThreshold;
    bool dcUV_flag = modesAndProtection_IN.vdc < modesAndProtection_CONFIG.dcUnderVoltageThreshold;

    bool dcV_excursion = clear_timer( &protection_clear_timer, modesAndProtection_CONFIG.VdcEventClearTime, t_step,
                                        (dcOv_flag || dcUV_flag) );

    bool protection_all_clear = !dcV_excursion;

    modesAndProtection_OUT.enable =     modesAndProtection_IN.enableRequest
                                    &&  modesAndProtection_IN.acOk
                                    &&  protection_all_clear;
    // report STATE
    modesAndProtection_STATE.dcOV = dcOv_flag;
    modesAndProtection_STATE.dcUV = dcUV_flag;
    modesAndProtection_STATE.protectionAllClear = protection_all_clear;
} 
