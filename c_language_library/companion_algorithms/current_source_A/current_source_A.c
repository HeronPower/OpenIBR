#include "currentSource_A_private_autogen.h"

#include "../../helper_lib/helper_macros.h"

void currentSource_init(void)
{

}

void currentSource_5kHz(float t_step)
{
    float V_mag = cmplx_mag(currentSource_IN.V_pve);

    float P_cmd = clamp(currentSource_IN.P_request, 
                       -currentSource_GENERAL_CFG.Plimit * currentSource_GENERAL_CFG.SRated,
                        currentSource_GENERAL_CFG.Plimit * currentSource_GENERAL_CFG.SRated);

    float Q_cmd = clamp(currentSource_IN.Q_request,
                       -currentSource_GENERAL_CFG.Qlimit * currentSource_GENERAL_CFG.SRated,
                        currentSource_GENERAL_CFG.Qlimit * currentSource_GENERAL_CFG.SRated);

    unsigned int i;
    for(i=0; i<3; i++)
    {
        currentSource_OUT.i_ref[i] = ((currentSource_IN.pll_osc[i].real * P_cmd + currentSource_IN.pll_osc[i].imag * Q_cmd)  * SQRT_2 * OVER_SQRT_3)
                                      / clamp(V_mag, currentSource_GENERAL_CFG.VllRated * currentSource_GENERAL_CFG.VllMin_pu, 
                                                     currentSource_GENERAL_CFG.VllRated * currentSource_GENERAL_CFG.VllMax_pu);
    }

    currentSource_STATE.P_cmd = P_cmd;
    currentSource_STATE.Q_cmd = Q_cmd;
}

void currentSource_update_derived_config(void)
{
    ;
}
