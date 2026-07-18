#include "app.h"

// for comms
#include "peripherals/peripherals.h"

// modules
#include "../../../c_language_library/gfm_essentials/machine_model_A/machineModel_A_autogen.h"
#include "../../../c_language_library/gfm_essentials/current_limiting_B/currentLimiting_B_autogen.h"
#include "../../../c_language_library/companion_algorithms/ac_monitor_A/acMonitor_A_autogen.h"
#include "../../../c_language_library/companion_algorithms/ac_checks_A/acChecks_A_autogen.h"
#include "../../../c_language_library/companion_algorithms/current_source_A/currentSource_A_autogen.h"
#include "../../../c_language_library/design_specific_examples/circuit_controls_A/circuitControls_A_autogen.h"

// debug
#include "debug/configurable_dac.h"

struct external_commands_t external_commands = {
    .enable = false,
    .P_request = 0.0f,
    .Q_request = 0.0f,
    .V_ref = VLL_RATED,
    .dF_ref = 0.0f
};

void app_task_init(void)
{
    // circuit controls config
    set_circuitControls_CONTROLLER_CFG_wDamp(1.0f / (0.8f * Z_BASE * .16f * C_BASE));
    set_circuitControls_CONTROLLER_CFG_gDamp(1.0f / (0.8f * Z_BASE));
    set_circuitControls_CONTROLLER_CFG_kSlowPath(0.45f);
    set_circuitControls_CONTROLLER_CFG_wSlowPath(1200.0f);
    set_circuitControls_CONTROLLER_CFG_kP(0.15f);
    set_circuitControls_LIMITS_CFG_VBridgeLower(-1.8f * VLL_RATED * OVER_SQRT_3);
    set_circuitControls_LIMITS_CFG_VBridgeUpper(1.8f * VLL_RATED * OVER_SQRT_3);
    circuitControls_update_derived_config();

    // ac monitor config
    set_acMonitor_GENERAL_CFG_VllRated(VLL_RATED);
    set_acMonitor_GENERAL_CFG_nominalFrequency(F_NOM);
    acMonitor_update_derived_config();

    // ac checks config
    set_acChecks_GENERAL_CFG_VllRated(VLL_RATED);
    set_acChecks_GENERAL_CFG_nominalFrequency(F_NOM);
    acChecks_update_derived_config();

    // machine model config
    set_machineModel_GENERAL_CFG_VllRated(VLL_RATED);
    set_machineModel_GENERAL_CFG_SRated(S_RATED);
    set_machineModel_GENERAL_CFG_nominalFrequency(F_NOM);
    set_machineModel_MECHANICAL_CFG_InertialConstant(2.5f);
    set_machineModel_MECHANICAL_CFG_kDroop(1.0f);
    set_machineModel_MECHANICAL_CFG_kDamper(2.0f);
    set_machineModel_ELECTRICAL_CFG_Xs(0.15f);
    set_machineModel_ELECTRICAL_CFG_rs(0.08);
    machineModel_update_derived_config();

    // current source config
    set_currentSource_GENERAL_CFG_Plimit(0.9f);
    set_currentSource_GENERAL_CFG_Qlimit(0.5f);
    set_currentSource_GENERAL_CFG_SRated(S_RATED);
    set_currentSource_GENERAL_CFG_VllRated(VLL_RATED);

    // current limiting config
    set_currentLimiting_CONFIG_IRated(I_RATED);
    currentLimiting_update_derived_config();
}

void app_task_isr(void)
{
    uint16_t i;
    
    GPIO_writePin(82, 1);

    for(i = 0; i < 3; i++)
    {
        circuitControls_IN.i_ref[i] = currentLimiting_OUT.i_limited[i];
    }
    
    circuitControls_IN.enable = external_commands.enable;
    circuitControls_isr(50e-6f);

    // 1kHz counter - substitute for RTOS scheduling
    static unsigned int counter_1kHz = 0;
    if (++counter_1kHz >= 20)
    {
        counter_1kHz = 0;
        acChecks_IN.freq = acMonitor_OUT.frequency;
        acChecks_IN.Vnve = acMonitor_OUT.Vnve;                
        for (i = 0; i < 3; i++)
        {
            acChecks_IN.Vph[i] = acMonitor_OUT.Vph[i];
        }
        
        acChecks_1kHz(1e-3f);        
    }

    // 5kHz counter - substitute for RTOS scheduling
    static unsigned int counter_5kHz = 0; 
    switch (counter_5kHz++ & 0x3) // 20kHz / 4 = 5kHz 
    {
        case 0:
            for(i = 0; i < 3; i++)
            {
                acMonitor_IN.v_ac[i] = circuitControls_OUT.v_ac[i];
            }

            acMonitor_5kHz(200e-6f);

            break;

        case 1:
            for(i = 0; i < 3; i++)
            {
                machineModel_IN.v_ac[i] = circuitControls_OUT.v_ac[i];
            }

            machineModel_IN.enable = external_commands.enable;
            machineModel_IN.PReq = 0.0f;
            machineModel_IN.VRef = external_commands.V_ref;
            machineModel_IN.FRef = F_NOM + external_commands.dF_ref;

            machineModel_5kHz(200e-6f);

            break;
        case 2:
            for (i = 0; i < 3; i++)
            {
                currentSource_IN.pll_osc[i] = acMonitor_OUT.pll_osc[i];
            }
            
            currentSource_IN.P_request = external_commands.P_request;
            currentSource_IN.Q_request = external_commands.Q_request;
            currentSource_IN.V_pve = acMonitor_OUT.Vpve;
            currentSource_5kHz(200e-6f);

            break;

        case 3:
            currentLimiting_IN.I_limit = I_RATED * 1.5f; // 1.5pu RMS overload
            currentLimiting_IN.freq = acMonitor_OUT.frequency;
            for (i = 0; i < 3; i++)
            {
                currentLimiting_IN.i_ref[i] = currentSource_OUT.i_ref[i] + machineModel_OUT.i_ref[i];
            }

            currentLimiting_5kHz(200e-6f);
            break;
        
        default:
            break;
            
    }

    configurable_dac_update(external_commands.scale_dac1, external_commands.offset_dac1, external_commands.signal_ID_dac1,
                            external_commands.scale_dac2, external_commands.offset_dac2, external_commands.signal_ID_dac2);

    GPIO_writePin(82, 0);

}

void app_task_background_loop(void)
{
    receive_uart_packet(&external_commands);
    
}
