#include "powerLimiter_A_private_autogen.h"

// custom libraries
#include "../../helper_lib/helper_macros.h"

static struct
{
    float over_VllRated;
} derived_config =
    {
        .over_VllRated = 1.0f,
    };

void powerLimiter_update_derived_config()
{
    derived_config.over_VllRated = 1.0f/fmaxf(powerLimiter_GENERAL_CFG.VllRated, 1e-3f);
}

void powerLimiter_5kHz(float t_step)
{
    int i;

    float power_inst = 0.0f;
    for (i = 0; i < 3; i++)
    {
        power_inst += powerLimiter_IN.v_ac[i] * powerLimiter_IN.i_ac[i];
    }

    static float power_lpf = 0.0f;
    power_lpf += (power_inst - power_lpf) * powerLimiter_GENERAL_CFG.wPowerSink * t_step;

    float power_excess = power_lpf - clamp(power_lpf, powerLimiter_IN.P_limit_lower, powerLimiter_IN.P_limit_upper);

    powerLimiter_STATE.pInst = power_inst;
    powerLimiter_STATE.pExcess = power_excess;    

    for (i = 0; i < 3; i++)
    {
        powerLimiter_OUT.i_excess[i] = powerLimiter_IN.osc[i].real * power_excess * derived_config.over_VllRated * SQRT_2 * OVER_SQRT_3;
    }
}
