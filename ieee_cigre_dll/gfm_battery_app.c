/*
 * Orchestration layer for the GFM battery IEEE/CIGRE DLL: app_task_init /
 * app_task_5kHz wire together the six c_language_library modules
 * (acMonitor, acChecks, machineModel, currentSource, powerLimiter,
 * currentLimiting) for this application. See
 * documentation/9_using_the_ieee_cigre_dll.md, section 2, for the
 * signal flow and module wiring.
 */

#include "gfm_battery_app.h"

// modules
#include "../c_language_library/gfm_essentials/machine_model_A/machineModel_A_autogen.h"
#include "../c_language_library/gfm_essentials/current_limiting_A/currentLimiting_A_autogen.h"

#include "../c_language_library/companion_algorithms/ac_monitor_A/acMonitor_A_autogen.h"
#include "../c_language_library/companion_algorithms/ac_checks_A/acChecks_A_autogen.h"
#include "../c_language_library/companion_algorithms/current_source_A/currentSource_A_autogen.h"
#include "../c_language_library/companion_algorithms/power_limiter_A/powerLimiter_A_autogen.h"
#include "../c_language_library/design_specific_examples/circuit_controls_B/circuitControls_B_autogen.h"

#include "dll_config_generated.h"

// helpers
#include <stdint.h>

void app_task_init(double H, double D, double kDroop)
{
	// ac checks config
	set_acChecks_GENERAL_CFG_VllRated(VLL_RATED);
	set_acChecks_QUALIFY_CFG_tRequalify(2);	// make it start quick
	acChecks_update_derived_config();

    // ac monitor config
    set_acMonitor_GENERAL_CFG_VllRated(VLL_RATED);
    set_acMonitor_GENERAL_CFG_nominalFrequency(F_NOM_RATED);
    acMonitor_update_derived_config();

    // machine model config
    set_machineModel_GENERAL_CFG_VllRated(VLL_RATED);
    set_machineModel_GENERAL_CFG_SRated(S_RATED);
	set_machineModel_GENERAL_CFG_nominalFrequency(F_NOM_RATED);
	set_machineModel_MECHANICAL_CFG_InertialConstant(H);
	set_machineModel_MECHANICAL_CFG_kDroop(kDroop);
	set_machineModel_MECHANICAL_CFG_kDamper(D);
    machineModel_update_derived_config();

    // current limiting config
	set_currentLimiting_CONFIG_IRated(I_RATED);
    currentLimiting_update_derived_config();

    // current source config
    set_currentSource_GENERAL_CFG_SRated(S_RATED);
	set_currentSource_GENERAL_CFG_VllRated(VLL_RATED);
    currentSource_update_derived_config();

	// power limiter config
	set_powerLimiter_GENERAL_CFG_VllRated(VLL_RATED);
	powerLimiter_update_derived_config();

	// circuit controls (current controller) config -- advanced parameters,
	// see dll_config.yaml
	{
		set_circuitControls_CONTROLLER_CFG_wDamp(CC_WDAMP_DEFAULT);
		set_circuitControls_CONTROLLER_CFG_gDamp(CC_GDAMP_DEFAULT);
		set_circuitControls_CONTROLLER_CFG_kFeedFwd(CC_KFEEDFWD_DEFAULT);
		set_circuitControls_CONTROLLER_CFG_kP(CC_KP_DEFAULT);
		set_circuitControls_CONTROLLER_CFG_kSlowPath(CC_KSLOWPATH_DEFAULT);
		set_circuitControls_CONTROLLER_CFG_wSlowPath(CC_WSLOWPATH_DEFAULT);
		set_circuitControls_CONTROLLER_CFG_wOffsetCalibration(CC_WOFFSETCALIBRATION_DEFAULT);
		circuitControls_update_derived_config();
	}
}

void app_task_5kHz(double v_bridge[3], bool* pwm_enable, // outputs
				   const double v_ac[3], const double i_L[3], double v_dc, double P_request, double Q_request, bool enable, double P_lim_high, double P_lim_low) // inputs
{
	// init
	uint16_t i;

	// ac monitor
	for(i = 0; i < 3; i++)
	{
		acMonitor_IN.v_ac[i] = (float)v_ac[i];
	}
	acMonitor_5kHz(200e-6f);

	// ac checks
	// 1kHz counter - substitute for RTOS scheduling
    static unsigned int counter_1kHz = 0;
    if (++counter_1kHz >= 5)
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

	// machine model
	for(i = 0; i < 3; i++)
	{
		machineModel_IN.v_ac[i] = (float)v_ac[i];
	}
	machineModel_IN.enable = enable;
	machineModel_IN.PReq = 0.0f;		// dispatch using current-source in this approach
	machineModel_IN.FRef = F_NOM_RATED;
	machineModel_IN.VRef = VLL_RATED;
	machineModel_5kHz(200e-6f);

    // current source
    currentSource_IN.P_request = (float)P_request;
    currentSource_IN.Q_request = (float)Q_request;
    currentSource_IN.V_pve = acMonitor_OUT.Vpve;
	for (i = 0; i < 3; i++)
    {
        currentSource_IN.pll_osc[i] = acMonitor_OUT.pll_osc[i];
    }
    currentSource_5kHz(200e-6f);

	// power limiter
	for (i = 0; i < 3; i++)
	{
		powerLimiter_IN.i_ac[i] = machineModel_OUT.i_ref[i] + currentSource_OUT.i_ref[i];
		powerLimiter_IN.v_ac[i] = (float)v_ac[i];
		powerLimiter_IN.osc[i] = acMonitor_OUT.pll_osc[i];
	}
	powerLimiter_IN.P_limit_upper = (double)P_lim_high;
	powerLimiter_IN.P_limit_lower = (double)P_lim_low;
	powerLimiter_5kHz(200e-6f);

	// current limiting
	for (i = 0; i < 3; i++)
	{
		currentLimiting_IN.osc[i] = acMonitor_OUT.pll_osc[i];
		currentLimiting_IN.i_ref[i] = currentSource_OUT.i_ref[i] + machineModel_OUT.i_ref[i] - powerLimiter_OUT.i_excess[i];
	}
	currentLimiting_IN.I_limit = I_RATED * I_LIMIT_PU;
	currentLimiting_5kHz(200e-6f);

	// circuit controls (current controller)
	for (i = 0; i < 3; i++)
	{
		circuitControls_IN.i_ref[i] = currentLimiting_OUT.i_limited[i];
		circuitControls_IN.i_L[i] = (float)i_L[i];
		circuitControls_IN.v_ac[i] = (float)v_ac[i];
	}
	circuitControls_IN.enable = enable;
	circuitControls_IN.v_dc = (float)v_dc;
	circuitControls_isr(200e-6f);

	// assign outputs
	// --------------

	for (i = 0; i < 3; i++)
	{
		v_bridge[i] = circuitControls_OUT.v_bridge[i];
	}

	*pwm_enable = acChecks_OUT.acOk && enable;
}
