#include "converter_sim.h"

/* Converter output filter */
#define L_FILTER 30e-6
#define R_FILTER 2e-3

/* Stub DC bus voltage -- stand-in for a real (sag-capable) DC-bus model,
 * until one exists. A fixed, stiff bus for a 480 Vac system. */
#define V_DC_STIFF 900.0

void converter_sim_step(const double v_bridge[3],
                        const double v_ac[3],
                        bool pwm_enable,
                        double dt,
                        double i_ac[3],
                        double *v_dc)
{
    static double i[3] = { 0.0, 0.0, 0.0 };
    int k;

    for (k = 0; k < 3; k++)
    {
        i[k] += dt * (v_bridge[k] - R_FILTER * i[k] - v_ac[k]) * (1.0 / L_FILTER);
        i[k] = pwm_enable ? i[k] : 0.0;
        i_ac[k] = i[k];
    }

    *v_dc = V_DC_STIFF;
}
