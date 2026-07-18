#include "acChecks_A_private_autogen.h"
#include "../../helper_lib/helper_macros.h"

#include "../../helper_lib/timers.h"
#include "../../helper_lib/cmplx.h"

static struct
{
    float over_VllRated;
} derived_config =
    {
        .over_VllRated = 1.0f,
    };

void acChecks_update_derived_config(void)
{
    derived_config.over_VllRated = 1.0f/fmaxf(acChecks_GENERAL_CFG.VllRated, 1e-3f);
}


void acChecks_1kHz(float t_step)
{
    int i;
    float Vph_pu[3]; 
    for (i = 0; i < 3; i++) {
        Vph_pu[i] = acChecks_IN.Vph[i] * derived_config.over_VllRated * SQRT_3;
    }
    float Vnve_mag_pu = cmplx_mag(acChecks_IN.Vnve) * derived_config.over_VllRated;

    bool Vph_min_pu = fminf(Vph_pu[0], fminf(Vph_pu[1], Vph_pu[2]));
    bool Vph_max_pu = fmaxf(Vph_pu[0], fmaxf(Vph_pu[1], Vph_pu[2]));

    float relative_frequency = acChecks_IN.freq - acChecks_GENERAL_CFG.nominalFrequency;

    // Qualify ac is in tight range - bypassed when grid is already qualified
    static float voltage_inrange_clear_timer = 0.0f;
    static float vimbalance_low_clear_timer = 0.0f;
    static float frequency_inrange_clear_timer = 0.0f;
    static float qualifier_timer = 0.0f;

    bool voltage_inrange = clear_timer( &voltage_inrange_clear_timer, acChecks_QUALIFY_CFG.tDisqualVoltage, t_step,
                                        (Vph_min_pu >= acChecks_QUALIFY_CFG.vMin) && (Vph_max_pu <= acChecks_QUALIFY_CFG.vMax) );

    bool vimbalance_low = clear_timer( &vimbalance_low_clear_timer, acChecks_QUALIFY_CFG.tDisqualVnve, t_step,
                                       Vnve_mag_pu <= acChecks_QUALIFY_CFG.vNveLim  );

    bool frequency_inrange = clear_timer( &frequency_inrange_clear_timer, acChecks_QUALIFY_CFG.tDisqualFreq, t_step,
                                            (relative_frequency >= acChecks_QUALIFY_CFG.fMin) && (relative_frequency <= acChecks_QUALIFY_CFG.fMax) );
    
    bool in_range = (voltage_inrange && vimbalance_low && frequency_inrange)
                    || acChecks_OUT.acOk;      // bypass qualification if already in range

    bool qualified = assert_timer( &qualifier_timer, acChecks_QUALIFY_CFG.tRequalify, t_step,
                                    in_range );

    // underfreq trips
    static float underfrequency_clear_timer_1 = 0.0f;
    static float underfrequency_clear_timer_2 = 0.0f;
    static float underfrequency_trip_timer_1 = 0.0f;
    static float underfrequency_trip_timer_2 = 0.0f;
    bool underfreq_excursion1 = clear_timer(&underfrequency_clear_timer_1, acChecks_TRIPS_CFG.clearingTimeout, t_step,
                                         relative_frequency < acChecks_TRIPS_CFG.underFrequencyThreshold1);
    bool underfreq_trip1 = assert_timer(&underfrequency_trip_timer_1, acChecks_TRIPS_CFG.underFrequencyTimeout1, t_step,
                                             underfreq_excursion1);

    bool underfreq_excursion2 = clear_timer(&underfrequency_clear_timer_2, acChecks_TRIPS_CFG.clearingTimeout, t_step,
                                         relative_frequency < acChecks_TRIPS_CFG.underFrequencyThreshold2);
    bool underfreq_trip2 = assert_timer(&underfrequency_trip_timer_2, acChecks_TRIPS_CFG.underFrequencyTimeout2, t_step,
                                             underfreq_excursion2);

    // overfreq trips
    static float overfrequency_clear_timer_1 = 0.0f;
    static float overfrequency_clear_timer_2 = 0.0f;
    static float overfrequency_trip_timer_1 = 0.0f;
    static float overfrequency_trip_timer_2 = 0.0f;
    bool overfreq_excursion1 = clear_timer(&overfrequency_clear_timer_1, acChecks_TRIPS_CFG.clearingTimeout, t_step,
                                         relative_frequency > acChecks_TRIPS_CFG.overFrequencyThreshold1);
    bool overfreq_trip1 = assert_timer(&overfrequency_trip_timer_1, acChecks_TRIPS_CFG.overFrequencyTimeout1, t_step,
                                             overfreq_excursion1);

    bool overfreq_excursion2 = clear_timer(&overfrequency_clear_timer_2, acChecks_TRIPS_CFG.clearingTimeout, t_step,
                                         relative_frequency > acChecks_TRIPS_CFG.overFrequencyThreshold2);
    bool overfreq_trip2 = assert_timer(&overfrequency_trip_timer_2, acChecks_TRIPS_CFG.overFrequencyTimeout2, t_step,
                                             overfreq_excursion2);

    // undervoltage trips
    static float undervoltage_clear_timer_1 = 0.0f;
    static float undervoltage_clear_timer_2 = 0.0f;
    static float undervoltage_trip_timer_1 = 0.0f;
    static float undervoltage_trip_timer_2 = 0.0f;
    bool undervolt_excursion1 = clear_timer(&undervoltage_clear_timer_1, acChecks_TRIPS_CFG.clearingTimeout, t_step,
                                            Vph_min_pu < acChecks_TRIPS_CFG.underVoltageThreshold1);
    bool undervolt_trip1 = assert_timer(&undervoltage_trip_timer_1, acChecks_TRIPS_CFG.underVoltageTimeout1, t_step,
                                        undervolt_excursion1);

    bool undervolt_excursion2 = clear_timer(&undervoltage_clear_timer_2, acChecks_TRIPS_CFG.clearingTimeout, t_step,
                                            Vph_min_pu < acChecks_TRIPS_CFG.underVoltageThreshold2);
    bool undervolt_trip2 = assert_timer(&undervoltage_trip_timer_2, acChecks_TRIPS_CFG.underVoltageTimeout2, t_step,
                                        undervolt_excursion2);

    // overvoltage trips
    static float overvoltage_clear_timer_1 = 0.0f;
    static float overvoltage_clear_timer_2 = 0.0f;
    static float overvoltage_trip_timer_1 = 0.0f;
    static float overvoltage_trip_timer_2 = 0.0f;

    bool overvolt_excursion1 = clear_timer(&overvoltage_clear_timer_1, acChecks_TRIPS_CFG.clearingTimeout, t_step,
                                           Vph_max_pu > acChecks_TRIPS_CFG.overVoltageThreshold1);
    bool overvolt_trip1 = assert_timer(&overvoltage_trip_timer_1, acChecks_TRIPS_CFG.overVoltageTimeout1, t_step,
                                        overvolt_excursion1);

    bool overvolt_excursion2 = clear_timer(&overvoltage_clear_timer_2, acChecks_TRIPS_CFG.clearingTimeout, t_step,
                                           Vph_max_pu > acChecks_TRIPS_CFG.overVoltageThreshold2);
    bool overvolt_trip2 = assert_timer(&overvoltage_trip_timer_2, acChecks_TRIPS_CFG.overVoltageTimeout2, t_step,
                                        overvolt_excursion2);

    // Final AC OK is qualified AND no trips
    bool tripped = underfreq_trip1 || underfreq_trip2 || overfreq_trip1 || overfreq_trip2 ||
                   undervolt_trip1 || undervolt_trip2 || overvolt_trip1 || overvolt_trip2;
    acChecks_OUT.acOk = qualified && !tripped;

    // qualification state variables
    acChecks_QUALIFY_STATE.timer = qualifier_timer;
    acChecks_QUALIFY_STATE.freqInrange = frequency_inrange;
    for (i = 0; i < 3; i++) {
        acChecks_QUALIFY_STATE.voltageInrange = voltage_inrange;
    }
    acChecks_QUALIFY_STATE.vimbalanceInrange = vimbalance_low;

    // frequency trips
    acChecks_TRIP_STATE.underFrequency1State = underfreq_trip1;
    acChecks_TRIP_STATE.overFrequency1State = overfreq_trip1;
    acChecks_TRIP_STATE.underFrequency2State = underfreq_trip2;
    acChecks_TRIP_STATE.overFrequency2State = overfreq_trip2;
    
    acChecks_TRIP_STATE.underFrequency1Timer = underfrequency_trip_timer_1;
    acChecks_TRIP_STATE.overFrequency1Timer = overfrequency_trip_timer_1;
    acChecks_TRIP_STATE.underFrequency2Timer = underfrequency_trip_timer_2;
    acChecks_TRIP_STATE.overFrequency2Timer = overfrequency_trip_timer_2;

    // trip state
    acChecks_TRIP_STATE.underVoltage1State = undervolt_trip1;
    acChecks_TRIP_STATE.overVoltage1State = overvolt_trip1;
    acChecks_TRIP_STATE.underVoltage2State = undervolt_trip2;
    acChecks_TRIP_STATE.overVoltage2State = overvolt_trip2;

    // trip timer
    acChecks_TRIP_STATE.underVoltage1Timer = undervoltage_trip_timer_1;
    acChecks_TRIP_STATE.overVoltage1Timer = overvoltage_trip_timer_1;
    acChecks_TRIP_STATE.underVoltage2Timer = undervoltage_trip_timer_2;
    acChecks_TRIP_STATE.overVoltage2Timer = overvoltage_trip_timer_2;
}
