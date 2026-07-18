#include "gfm_pv_sil.h"

// modules
#include "../c_language_library/gfm_essentials/machine_model_A/machineModel_A_autogen.h"
#include "../c_language_library/gfm_essentials/current_limiting_A/currentLimiting_A_autogen.h"

#include "../c_language_library/companion_algorithms/ac_monitor_A/acMonitor_A_autogen.h"
#include "../c_language_library/companion_algorithms/ac_checks_A/acChecks_A_autogen.h"
#include "../c_language_library/companion_algorithms/current_source_A/currentSource_A_autogen.h"
#include "../c_language_library/companion_algorithms/dc_voltage_control_A/dcVoltageCntl_A_autogen.h"
#include "../c_language_library/companion_algorithms/power_limiter_A/powerLimiter_A_autogen.h"

#include "../c_language_library/design_specific_examples/circuit_controls_A/circuitControls_A_autogen.h"
#include "../c_language_library/design_specific_examples/modes_and_protection_A/modesAndProtection_A_autogen.h"

// helpers
#include "../c_language_library/helper_lib/helper_macros.h"
#include <stdint.h>

double VllRated;
double SRated;
double FNom;
double IRated;
double ZBase;

void app_task_init(double vll_rated, double s_rated, double f_nom, double i_rated, double z_base)
{
	VllRated = vll_rated;
	SRated = s_rated;
	FNom = f_nom;
	IRated = i_rated;
	ZBase = z_base;

	// modes and protection config
	set_modesAndProtection_CONFIG_dcOverVoltageThreshold(1800.0f);
	set_modesAndProtection_CONFIG_dcUnderVoltageThreshold(700.0f);
	set_modesAndProtection_CONFIG_VdcEventClearTime(0.2f);
	modesAndProtection_update_derived_config();

	// ac checks config
	set_acChecks_GENERAL_CFG_VllRated(VllRated);
	set_acChecks_QUALIFY_CFG_tRequalify(2);	// make it start quick
	acChecks_update_derived_config();

    // ac monitor config
    set_acMonitor_GENERAL_CFG_VllRated(VllRated);
    set_acMonitor_GENERAL_CFG_nominalFrequency(FNom);
    acMonitor_update_derived_config();
    
    // machine model config
    set_machineModel_GENERAL_CFG_VllRated(VllRated);
    set_machineModel_GENERAL_CFG_SRated(SRated);
    set_machineModel_GENERAL_CFG_nominalFrequency(FNom);
    set_machineModel_MECHANICAL_CFG_kDroop(0.0f);
	set_machineModel_MECHANICAL_CFG_InertialConstant(0.2f);
	set_machineModel_MECHANICAL_CFG_kDamper(0.5f);
    machineModel_update_derived_config();
    
    // current limiting config
	set_currentLimiting_CONFIG_IRated(IRated);
    currentLimiting_update_derived_config();

	// dc voltage controller config
	set_dcVoltageCntl_CONFIG_kp(20e3);			// in SI
	set_dcVoltageCntl_CONFIG_voltageDeadband(0.0f);
	set_dcVoltageCntl_CONFIG_VllRated(VllRated);
	set_dcVoltageCntl_CONFIG_pLimitExternalMinimum(0.0f * SRated);
	dcVoltageCntl_init();

    // current source config
    set_currentSource_GENERAL_CFG_SRated(SRated);
    set_currentSource_GENERAL_CFG_VllRated(VllRated);
    currentSource_update_derived_config();
}

void app_task_5kHz(double i_ref[3], bool* pwm_enable, // outputs
     	           const double v_ac[3], double Q_request, bool enable, double P_lim_high, double P_lim_low, double v_dc, double v_dc_ref) // inputs
{
	// init
	uint16_t i;
    
	// ac monitor
	for(i = 0; i < 3; i++)
	{
		acMonitor_IN.v_ac[i] = (float)v_ac[i];
	}
	acMonitor_5kHz(200e-6f);

	// modes and protection
	modesAndProtection_IN.vdc = v_dc;
	modesAndProtection_IN.acOk = acChecks_OUT.acOk;
	modesAndProtection_IN.enableRequest = enable;
	modesAndProtection_5kHz(200e-6f);

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
	machineModel_IN.PReq = 0.0f;
	machineModel_IN.FRef = FNom;
	machineModel_IN.VRef = VllRated;
	machineModel_5kHz(200e-6f);

	// dc voltage controller
	dcVoltageCntl_IN.vdc = v_dc;
	dcVoltageCntl_IN.vdc_ref = v_dc_ref;
	dcVoltageCntl_IN.pLimitHigh = P_lim_high;
	dcVoltageCntl_IN.pLimitLow = P_lim_low;
	dcVoltageCntl_IN.freq = acMonitor_OUT.frequency;
	for (i = 0; i < 3; i++)
	{
		dcVoltageCntl_IN.osc[i] = acMonitor_OUT.pll_osc[i];
	}
	dcVoltageCntl_5kHz(200e-6f);


    // Current Source (grid-following)
	currentSource_IN.P_request = 0.0f;
    currentSource_IN.Q_request = Q_request;
	for (i = 0; i < 3; i++)
    {
        currentSource_IN.pll_osc[i] = acMonitor_OUT.pll_osc[i];
    }
    currentSource_IN.V_pve = acMonitor_OUT.Vpve;
    currentSource_5kHz(200e-6f);

	// current limiting
	currentLimiting_IN.I_limit = IRated * 1.6f; // 1.6pu RMS overload
	for (i = 0; i < 3; i++)
	{
		currentLimiting_IN.osc[i] = acMonitor_OUT.pll_osc[i];
		currentLimiting_IN.i_ref[i] = currentSource_OUT.i_ref[i] + machineModel_OUT.i_ref[i] + dcVoltageCntl_OUT.i_ref[i] - powerLimiter_OUT.i_excess[i];
	}
	currentLimiting_5kHz(200e-6f);

	// assign outputs back to Simulink signals
	// ---------------------------------------

	for(i = 0; i < 3; i++)
	{
		i_ref[i] = currentLimiting_OUT.i_limited[i];
	}

	*pwm_enable = modesAndProtection_OUT.enable;
}
