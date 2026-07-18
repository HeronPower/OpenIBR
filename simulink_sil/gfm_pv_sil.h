#ifndef PV_SIL_RUNNER_H
    #define PV_SIL_RUNNER_H

    #include "stdbool.h"

    void app_task_init(double vll_rated, double s_rated, double f_nom, double i_rated, double z_base);
    void app_task_5kHz(double i_ref[3], bool* pwm_enable, // outputs
                       const double v_ac[3], double Q_request, bool enable, double P_lim_high, double P_lim_low, double v_dc, double v_dc_ref); // inputs

#endif  // PV_SIL_RUNNER_H
