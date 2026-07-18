#ifndef PV_PLUS_SIL_RUNNER_H
    #define PV_PLUS_SIL_RUNNER_H

    #include "stdbool.h"

    void app_task_init(double vll_rated, double s_rated, double f_nom, double i_rated, double z_base);
    void app_task_5kHz(double i_ref[3], bool* pwm_enable, // outputs
     	               const double v_ac[3], double P_request, double Q_request, bool enable, double P_lim_high, double P_lim_low, double v_dc); // inputs

#endif  // PV_PLUS_SIL_RUNNER_H
