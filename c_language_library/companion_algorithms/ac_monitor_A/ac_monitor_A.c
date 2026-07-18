#include "acMonitor_A_private_autogen.h"

#include "../../helper_lib/helper_macros.h"
#include <stdint.h>

struct pll_3ph_state {
    float error_lpf;
    float integrator;
    float frequency;
    float cycles;
    cmplx_t osc[3];
};

void pll_3ph_update(struct pll_3ph_state* pll, float input[3], float t_step);

static struct
{
    float over_VllRated;
} derived_config =
    {
        .over_VllRated = 1.0f,
    };

void acMonitor_init(void)
{

}

void acMonitor_isr(float t_step)
{

}

void acMonitor_1kHz(float t_step)
{

}

void acMonitor_update_derived_config(void)
{
    derived_config.over_VllRated = 1.0f/fmaxf(acMonitor_GENERAL_CFG.VllRated, 1e-3f);
}

void acMonitor_5kHz(float t_step)
{    
    // PLL state structure
    static struct pll_3ph_state pll_pve;          // positive sequence PLL (there can be another PLL for negative sequence in the future)

    // init
    uint16_t i;
    
    float pll_ref[3];

    for(i=0; i<3; i++)
    {
        pll_ref[i] = acMonitor_IN.v_ac[i] * derived_config.over_VllRated;
    }

    // pll update
    pll_3ph_update(&pll_pve, pll_ref, t_step);
	
    // analyzer for voltage
    static cmplx_t Vrotated_lpf[3];
    for (i = 0; i < 3; i++)
    {
        // rotation onto pll frame, relative to phase A
        cmplx_t Vrotated = cmplx_scale(pll_pve.osc[0], acMonitor_IN.v_ac[i] * SQRT_2);

        // lpf
        Vrotated_lpf[i].real += (Vrotated.real - Vrotated_lpf[i].real) * acMonitor_GENERAL_CFG.wAnalyzer * t_step;
        Vrotated_lpf[i].imag += (-Vrotated.imag - Vrotated_lpf[i].imag) * acMonitor_GENERAL_CFG.wAnalyzer * t_step;
        acMonitor_STATE.Vrotated[i] = Vrotated_lpf[i];
    }

    const cmplx_t rotation_120deg = {-0.5f, 0.5f*SQRT_3};
    const cmplx_t rotation_240deg = {-0.5f, -0.5f*SQRT_3};

    cmplx_t Vzero = cmplx_scale(   cmplx_add(   cmplx_add(  Vrotated_lpf[0], Vrotated_lpf[1]), Vrotated_lpf[2] ),  OVER_SQRT_3);

    cmplx_t Vpve = cmplx_scale( cmplx_add(  cmplx_add(  Vrotated_lpf[0], cmplx_multiply(Vrotated_lpf[1], rotation_120deg)  ),
                                            cmplx_multiply(Vrotated_lpf[2], rotation_240deg)         ),
                                OVER_SQRT_3    );
    
    cmplx_t Vnve = cmplx_scale( cmplx_add(  cmplx_add(  Vrotated_lpf[0], cmplx_multiply(Vrotated_lpf[1], rotation_240deg)  ),
                                            cmplx_multiply(Vrotated_lpf[2], rotation_120deg)         ),
                                OVER_SQRT_3    );


    static float Vph_squared_lpf[3] = {0.0f, 0.0f, 0.0f};
    for(i = 0; i < 3; i++)
	{	
        Vph_squared_lpf[i] += (acMonitor_IN.v_ac[i] * acMonitor_IN.v_ac[i] - Vph_squared_lpf[i]) * acMonitor_GENERAL_CFG.wAnalyzer * t_step;
        acMonitor_OUT.Vph[i] = sqrtf(Vph_squared_lpf[i]);
	}

    // update outputs
    for (i = 0; i < 3; i++)
    {
        acMonitor_OUT.pll_osc[i] = pll_pve.osc[i];
        acMonitor_STATE.Vrotated[i] = Vrotated_lpf[i];
    }
    acMonitor_OUT.frequency = pll_pve.frequency;
    acMonitor_OUT.Vpve = Vpve;
    acMonitor_OUT.Vnve = Vnve;
    acMonitor_OUT.Vzero = Vzero;
} 

void pll_3ph_update(struct pll_3ph_state* pll, float input[3], float t_step)
{
    unsigned int i; // for the for loops

    for(i = 0; i < 3; i++)
    {
        pll->error_lpf -= (input[i] * pll->osc[i].imag) * acMonitor_PLL_CFG.wPhaseDetector * t_step;
    }
    pll->error_lpf -= pll->error_lpf * acMonitor_PLL_CFG.wPhaseDetector * t_step;        // leak term

    // PI with a leaky integrator
    pll->integrator += ( acMonitor_PLL_CFG.kI * pll->error_lpf - pll->integrator * acMonitor_PLL_CFG.wIntegratorLeak ) * t_step;

    pll->integrator = clamp(pll->integrator, -acMonitor_PLL_CFG.integratorLimit, acMonitor_PLL_CFG.integratorLimit);

    pll->frequency =        acMonitor_GENERAL_CFG.nominalFrequency
                        +   pll->integrator
                        +   clamp(   acMonitor_PLL_CFG.kP * pll->error_lpf, 
                                        -acMonitor_PLL_CFG.proportionalLimit, 
                                        acMonitor_PLL_CFG.proportionalLimit );

    // update angle
    pll->cycles += pll->frequency * t_step;
    // wrap around
    pll->cycles -= floorf(pll->cycles);


    // update oscillators
    for(i = 0; i < 3; i++)
    {
        pll->osc[i].real = cos( (pll->cycles - 0.33333f*i) * 2.0f*PI );
        pll->osc[i].imag = sin( (pll->cycles - 0.33333f*i) * 2.0f*PI );
    }
}
