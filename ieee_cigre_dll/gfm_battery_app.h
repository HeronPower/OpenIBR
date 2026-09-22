/*
 * Declarations for this DLL's orchestration layer: app_task_init /
 * app_task_5kHz wire together the c_language_library modules for the
 * grid-forming battery application. See gfm_battery_app.c.
 */

#ifndef BATTERY_APP_RUNNER_H
    #define BATTERY_APP_RUNNER_H

    #include "stdbool.h"
    #include <math.h>

    /* Nameplate ratings/hardware capabilities -- fixed for a given
     * physical unit, so these are plain compiled-in constants, not DLL
     * Parameters. IRated/ZBase are derived from the other three (same
     * formulas machine_model_A.c computes at runtime for its own
     * per-unit gains).
     *
     * F_NOM_RATED (not F_NOM) deliberately -- test_harness/ac_network.h
     * defines its own F_NOM for the network model, a separate concept
     * (grid nominal frequency vs. this inverter's nameplate rating) that
     * both files happen to include in the same translation unit. */
    #define VLL_RATED   480.0
    #define S_RATED     1.0e6
    #define F_NOM_RATED 60.0
    #define I_LIMIT_PU  1.6
    #define I_RATED     (S_RATED / (sqrt(3.0) * VLL_RATED))
    #define Z_BASE      ((VLL_RATED * VLL_RATED) / S_RATED)

    /* H/D/kDroop are the DLL's real Parameters (see gfm_battery_dll.c) -- a
     * host can override them per-instance. */
    void app_task_init(double H, double D, double kDroop);
    void app_task_5kHz(double v_bridge[3], bool* pwm_enable, // outputs
                       const double v_ac[3], const double i_L[3], double v_dc, double P_request, double Q_request, bool enable, double P_lim_high, double P_lim_low); // inputs

#endif  // BATTERY_APP_RUNNER_H
