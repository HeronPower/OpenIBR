#include "machineModel_A_private_autogen.h"

// custom libraries
#include "../../helper_lib/cmplx.h"
#include "../../helper_lib/helper_macros.h"

// std libraries
#include <stdint.h>

//#include "../application/parameter_definitions.h"


struct
{
    float over_VllRated;
    float over_SRated;
    float IRated;
    float Zbase;
    float kInertia;
    float over_Ls;
} derived_config =
    {
        .over_VllRated = 1.0f,
        .over_SRated = 1.0f,
        .IRated = OVER_SQRT_3,
        .Zbase = 1.0f,
        .kInertia = 15.0f,
        .over_Ls = 1.885e3f
    };

void machineModel_update_derived_config(void)
{
    derived_config.over_VllRated = 1.0f/fmaxf(machineModel_GENERAL_CFG.VllRated, 1e-3f);
    derived_config.over_SRated = 1.0f/fmaxf(machineModel_GENERAL_CFG.SRated, 1e-3f);
    derived_config.IRated = machineModel_GENERAL_CFG.SRated * derived_config.over_VllRated * OVER_SQRT_3;
    derived_config.kInertia = 0.5f * machineModel_GENERAL_CFG.nominalFrequency * (1.0f/fmaxf(machineModel_MECHANICAL_CFG.InertialConstant, 1e-3f)) * derived_config.over_SRated;

    float Y_base = machineModel_GENERAL_CFG.SRated * (derived_config.over_VllRated * derived_config.over_VllRated);
    derived_config.Zbase = (machineModel_GENERAL_CFG.VllRated * machineModel_GENERAL_CFG.VllRated) * derived_config.over_SRated;

    derived_config.over_Ls = 2.0f * PI * machineModel_GENERAL_CFG.nominalFrequency * Y_base * 1.0f/fmaxf(machineModel_ELECTRICAL_CFG.Xs, 1e-3f);
}

void machineModel_init(void)
{

}

void machineModel_isr(float t_step)
{
    (void) t_step;
}

void machineModel_1kHz(float t_step)
{
    (void) t_step;
}

cmplx_t voltage_analyzer(float v_ac[3], cmplx_t osc[3], float t_step)
{
    uint16_t i;

    // analyzer for voltage
    cmplx_t Vph_sum = {0.0f, 0.0f};
    static cmplx_t Vpve = {0.0f, 0.0f};
    for (i = 0; i < 3; i++)
    {
        // extract fundamentals against rotor oscillator
        Vph_sum = cmplx_add(Vph_sum, cmplx_scale( cmplx_conjugate(osc[i]), v_ac[i] * SQRT_2));

    }
    // Calculate & LPF positive sequence voltage
    Vpve = cmplx_add(    cmplx_scale(    cmplx_sub( cmplx_scale(Vph_sum, OVER_SQRT_3), Vpve ),
                                            machineModel_ELECTRICAL_CFG.wVoltageAnalyzer * t_step    ),
                            Vpve     );

    return Vpve;
}

cmplx_t power_analyzer(float v_ac[3], float i_ac[3])
{
    cmplx_t S = {0.0f, 0.0f};

    S.real =  v_ac[0] * i_ac[0] 
            + v_ac[1] * i_ac[1] 
            + v_ac[2] * i_ac[2];

    S.imag = OVER_SQRT_3 * ( i_ac[0] * (v_ac[1] - v_ac[2]) 
                           + i_ac[1] * (v_ac[2] - v_ac[0]) 
                           + i_ac[2] * (v_ac[0] - v_ac[1]));
    return S;
}

float governor(float Prequest, float Fref, float f_rotor, float t_step)
{
    static float p_mechanical = 0.0f;

    float Fref_clamped = clamp( Fref,
                                machineModel_MECHANICAL_CFG.deltaFRefMin + machineModel_GENERAL_CFG.nominalFrequency,
                                machineModel_MECHANICAL_CFG.deltaFRefMax + machineModel_GENERAL_CFG.nominalFrequency);

    // Power request and droop power are initially clamped to rated power
    float Prequest_clamped = clamp(Prequest, -1.0f * machineModel_GENERAL_CFG.SRated, machineModel_GENERAL_CFG.SRated);
    float droop_power = (Fref_clamped - f_rotor) * machineModel_MECHANICAL_CFG.kDroop * machineModel_GENERAL_CFG.SRated;
    droop_power = clamp(droop_power, -1.0f * machineModel_GENERAL_CFG.SRated, machineModel_GENERAL_CFG.SRated);

    float power_target =    Prequest_clamped + droop_power;

    // LPF and clamp to configured limits
    p_mechanical += ( power_target - p_mechanical ) * machineModel_MECHANICAL_CFG.wDamperDecoupler * t_step;
    p_mechanical = clamp(  p_mechanical,
                    machineModel_MECHANICAL_CFG.mechPowerMin * machineModel_GENERAL_CFG.SRated,
                    machineModel_MECHANICAL_CFG.mechPowerMax * machineModel_GENERAL_CFG.SRated  );

    return p_mechanical;
}

float synchronizer(float Vq, float VdConditioned, bool machine_enabled, float t_step)
{
    static float Vq_lpf = 0.0f;
    Vq_lpf += (Vq - Vq_lpf) * machineModel_SYNC_CFG.wFastSync * t_step; 
    float Vq_hpf = (Vq - Vq_lpf);

    float pSync_pu = derived_config.over_VllRated * ( machineModel_SYNC_CFG.kpSync * Vq + machineModel_SYNC_CFG.kFastSync * Vq_hpf );
    
    float sync_scale_factor = clamp( (machineModel_SYNC_CFG.syncFadeThreshold - VdConditioned * derived_config.over_VllRated) * machineModel_SYNC_CFG.syncFadeGain, 0.0f, 1.0f );
    sync_scale_factor = ( machine_enabled ? sync_scale_factor : 1.0f );     // always sync if disabled

    float pSync = clamp(pSync_pu, -machineModel_SYNC_CFG.pSyncLimit, machineModel_SYNC_CFG.pSyncLimit)
                        * machineModel_GENERAL_CFG.SRated
                        * sync_scale_factor;

    return pSync;
}

float rotor_model(cmplx_t rotor_osc[3], float p_mechanical, float p_sync, float p_electrical, float t_step)
{
    static float df_rotor = 0.0f, df_rotor_lpf=0.0f;
    static float rotor_phase_cycles = 0.0f;

    // mechanical damper
    df_rotor_lpf += (df_rotor - df_rotor_lpf) * machineModel_MECHANICAL_CFG.wDamperDecoupler * t_step;

    float p_damper = (df_rotor - df_rotor_lpf) * machineModel_MECHANICAL_CFG.kDamper * machineModel_GENERAL_CFG.SRated;

    // update frequency - note math is done in terms of delta-f in order to simplify initialization, and minimize float accuracy effects
    df_rotor += (p_mechanical + p_sync - p_electrical - p_damper) * derived_config.kInertia * t_step;
    df_rotor = clamp(   df_rotor,
                        machineModel_MECHANICAL_CFG.rotorDeltaFreqMin,
                        machineModel_MECHANICAL_CFG.rotorDeltaFreqMax
                    );

    float f_rotor = df_rotor + machineModel_GENERAL_CFG.nominalFrequency;

    // rotor oscillator
    rotor_phase_cycles += f_rotor * t_step;
    rotor_phase_cycles -= floorf(rotor_phase_cycles);   // wrap around, stay within [0.0, 1.0)

    // translate to sine waves
    uint16_t i;
    for(i = 0; i < 3; i++)
    {
        float angle =  2.0f * PI * (rotor_phase_cycles - ONE_THIRD * i);
        rotor_osc[i].real = cosf(angle);
        rotor_osc[i].imag = sinf(angle);
    }

    return f_rotor;
}

void exciter(float VRef, cmplx_t rotor_osc[3], float VdConditioned, float t_step)
{
    float VexciterRequest = clamp( VRef, machineModel_ELECTRICAL_CFG.exciterVMin * machineModel_GENERAL_CFG.VllRated,
                                   machineModel_ELECTRICAL_CFG.exciterVMax * machineModel_GENERAL_CFG.VllRated );
    VexciterRequest = fminf( VexciterRequest, (VdConditioned + (machineModel_ELECTRICAL_CFG.exciterVoltageHeadroom * machineModel_GENERAL_CFG.VllRated)) );
    
    static float Vexciter = 0.0f;
    Vexciter += (VexciterRequest - Vexciter) * machineModel_ELECTRICAL_CFG.wExcitation * t_step;

    uint16_t i;
    for (i = 0; i < 3; i++)
    {
        machineModel_STATE.v_emf[i] = Vexciter * rotor_osc[i].real * OVER_SQRT_3 * SQRT_2;
    }

    machineModel_STATE.VExciter = Vexciter;
}

void impedance_emaulation(float i_machine[3], const float v_emf[3], const float v_ac[3], bool enable, float t_step)
{
    // impedance emulation
    float i_machine_neutral = i_machine[0] + i_machine[1] + i_machine[2];

    uint16_t i;
    for (i = 0; i < 3; i++)
    {
        float v_err = (v_emf[i] - v_ac[i]) * enable;
        i_machine[i] +=     (   v_err   - machineModel_ELECTRICAL_CFG.rN * derived_config.Zbase * i_machine_neutral
                                        - machineModel_ELECTRICAL_CFG.rs * derived_config.Zbase * i_machine[i] )
                        *   derived_config.over_Ls * t_step;
    }
}

void machineModel_5kHz(float t_step)
{
    // Observe Vd and Vq for synch and excitation limits
    cmplx_t Vpve = voltage_analyzer(machineModel_IN.v_ac, machineModel_OUT.rotor_osc, t_step);
    cmplx_t Vpve_pu = cmplx_scale(Vpve, derived_config.over_VllRated);
    float Vd = Vpve.real;
    float Vq = Vpve.imag;

    // Conditioned voltage is used to limit EMF during blackstart and extended LVRTs.
    // It falls slow to avoid interfering with brief LVRT events, but rises fast to manage 
    // startup current in a blackstart (or blackstart-like recovery from an extended fault)
    static float VdConditioned = 0.0f;
    VdConditioned += t_step * clamp( ( Vd - VdConditioned ) * machineModel_SYNC_CFG.wConditioned,
                                        -machineModel_SYNC_CFG.VConditionedDownRateLimit * machineModel_GENERAL_CFG.VllRated,
                                         machineModel_SYNC_CFG.VConditionedUpRateLimit * machineModel_GENERAL_CFG.VllRated );

    // governor
    float pMech = governor(machineModel_IN.PReq, machineModel_IN.FRef, machineModel_OUT.fRotor, t_step);

    // synchronization power
    float pSync = synchronizer(Vq, VdConditioned, machineModel_IN.enable, t_step);

    // estimate electrical power
    cmplx_t S_elec = power_analyzer(machineModel_IN.v_ac, machineModel_OUT.i_ref);
    float pElec = S_elec.real;
    float qElec = S_elec.imag;

    // run rotor
    machineModel_OUT.fRotor = rotor_model(machineModel_OUT.rotor_osc, pMech, pSync, pElec, t_step);

    // run excitation (v_emf is the output)
    exciter(machineModel_IN.VRef, machineModel_OUT.rotor_osc, VdConditioned, t_step); 
    
    // i_ref (the output) is passed back by reference
    impedance_emaulation(machineModel_OUT.i_ref, machineModel_STATE.v_emf, machineModel_IN.v_ac, machineModel_IN.enable, t_step);

    // STATE reporting
    machineModel_STATE.pMech = pMech;
    machineModel_STATE.pElec = pElec;
    machineModel_STATE.qElec = qElec;
    machineModel_STATE.pSync = pSync;
    machineModel_STATE.V_pve_pu = Vpve_pu;
    machineModel_STATE.VdConditioned = VdConditioned;
} 
