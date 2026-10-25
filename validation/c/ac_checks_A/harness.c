/* AC qualification and trip regression; run each scenario in a fresh process. */
#include "acChecks_A_autogen.h"
#include "timers.h"
#include "helper_macros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures, tick;
static const float dt = 0.001f;

static void check(bool condition, const char *description)
{
    checks++;
    if (!condition) {
        failures++;
        fprintf(stderr, "FAIL tick %u: %s\n", tick, description);
    }
}

static void near(float actual, float expected, const char *description)
{
    check(fabsf(actual - expected) <= 1e-7f, description);
}

static unsigned voltage_trips(void)
{
    return (acChecks_TRIP_STATE.underVoltage1State ? 1u : 0u)
         | (acChecks_TRIP_STATE.overVoltage1State ? 2u : 0u)
         | (acChecks_TRIP_STATE.underVoltage2State ? 4u : 0u)
         | (acChecks_TRIP_STATE.overVoltage2State ? 8u : 0u);
}

static unsigned frequency_trips(void)
{
    return (acChecks_TRIP_STATE.underFrequency1State ? 1u : 0u)
         | (acChecks_TRIP_STATE.overFrequency1State ? 2u : 0u)
         | (acChecks_TRIP_STATE.underFrequency2State ? 4u : 0u)
         | (acChecks_TRIP_STATE.overFrequency2State ? 8u : 0u);
}

static void step(void)
{
    acChecks_1kHz(dt);
    tick++;
    printf("{\"event\":\"step\",\"tick\":%u,\"ac_ok\":%s,"
           "\"voltage_inrange\":%s,\"frequency_inrange\":%s,\"imbalance_inrange\":%s,"
           "\"voltage_trips\":%u,\"frequency_trips\":%u,\"qualifier_s\":%.9g,"
           "\"uv1_timer_s\":%.9g,\"ov1_timer_s\":%.9g,\"uv2_timer_s\":%.9g,\"ov2_timer_s\":%.9g}\n",
           tick, acChecks_OUT.acOk ? "true" : "false",
           acChecks_QUALIFY_STATE.voltageInrange ? "true" : "false",
           acChecks_QUALIFY_STATE.freqInrange ? "true" : "false",
           acChecks_QUALIFY_STATE.vimbalanceInrange ? "true" : "false",
           voltage_trips(), frequency_trips(), (double)acChecks_QUALIFY_STATE.timer,
           (double)acChecks_TRIP_STATE.underVoltage1Timer,
           (double)acChecks_TRIP_STATE.overVoltage1Timer,
           (double)acChecks_TRIP_STATE.underVoltage2Timer,
           (double)acChecks_TRIP_STATE.overVoltage2Timer);
}

static void steps(unsigned n) { while (n--) step(); }

static void phases(float a, float b, float c)
{
    float scale = fmaxf(acChecks_GENERAL_CFG.VllRated, 1e-3f) / SQRT_3;
    acChecks_IN.Vph[0] = a * scale;
    acChecks_IN.Vph[1] = b * scale;
    acChecks_IN.Vph[2] = c * scale;
}

/* Unit conversion for float-adjacent boundary fixtures, not an extrema oracle. */
static float normalized(unsigned phase)
{
    return acChecks_IN.Vph[phase]
         * (1.0f / fmaxf(acChecks_GENERAL_CFG.VllRated, 1e-3f)) * SQRT_3;
}

static void configure(void)
{
    acChecks_GENERAL_CFG.VllRated = 480.0f;
    acChecks_update_derived_config();
    acChecks_IN.freq = 60.0f;
    /* Positive short test delays; thresholds retain schema defaults. */
    acChecks_QUALIFY_CFG.tRequalify = 0.0035f;
    acChecks_QUALIFY_CFG.tDisqualVoltage = 0.0025f;
    acChecks_QUALIFY_CFG.tDisqualFreq = 0.0025f;
    acChecks_QUALIFY_CFG.tDisqualVnve = 0.0025f;
    acChecks_TRIPS_CFG.clearingTimeout = 0.0025f;
    acChecks_TRIPS_CFG.underVoltageTimeout1 = 0.0035f;
    acChecks_TRIPS_CFG.overVoltageTimeout1 = 0.0035f;
    acChecks_TRIPS_CFG.underVoltageTimeout2 = 0.0035f;
    acChecks_TRIPS_CFG.overVoltageTimeout2 = 0.0035f;
    acChecks_TRIPS_CFG.underFrequencyTimeout1 = 0.0035f;
    acChecks_TRIPS_CFG.overFrequencyTimeout1 = 0.0035f;
    acChecks_TRIPS_CFG.underFrequencyTimeout2 = 0.0035f;
    acChecks_TRIPS_CFG.overFrequencyTimeout2 = 0.0035f;
    phases(0.96f, 1.02f, 1.08f);
}

static void healthy(void)
{
    for (unsigned i = 0; i < 3; i++) {
        step(); check(!acChecks_OUT.acOk, "qualification must wait for its delay");
    }
    step(); check(acChecks_OUT.acOk, "healthy unequal phases must qualify");
    check(voltage_trips() == 0 && frequency_trips() == 0, "healthy inputs must not trip");
    near(acChecks_QUALIFY_STATE.timer, 0.0035f, "qualification timer saturates");
}

static void excursion(const char *name)
{
    unsigned phase = (unsigned)(name[strlen(name) - 1] - '0');
    unsigned expected;
    float value;
    if (!strncmp(name, "uv1_", 4)) { value = 0.6f; expected = 1; }
    else if (!strncmp(name, "uv2_", 4)) { value = 0.3f; expected = 5; }
    else if (!strncmp(name, "ov1_", 4)) { value = 1.15f; expected = 2; }
    else { value = 1.3f; expected = 10; }
    phases(1.0f, 1.0f, 1.0f);
    acChecks_IN.Vph[phase] *= value;
    steps(3); check(voltage_trips() == 0, "voltage trip waits for assertion delay");
    step(); check(voltage_trips() == expected, "numeric phase extrema select the correct voltage tiers");
    check(!acChecks_OUT.acOk, "a voltage trip deasserts acOk");
    check(frequency_trips() == 0, "voltage excursion must not cause frequency trips");
}

static void voltage_boundary(const char *name)
{
    bool under = name[0] == 'u';
    unsigned tier = (unsigned)(name[2] - '0');
    phases(under ? 0.3f : 1.3f, 1.0f, 1.0f);
    float input = normalized(0);
    const char *relation = name + 4;
    float threshold = input;
    if (!strcmp(relation, "below")) threshold = nextafterf(input, -INFINITY);
    if (!strcmp(relation, "above")) threshold = nextafterf(input, INFINITY);
    /* Isolate one comparator without zero-delay pseudo-disabling. */
    acChecks_TRIPS_CFG.underVoltageThreshold1 = -10.0f;
    acChecks_TRIPS_CFG.underVoltageThreshold2 = -10.0f;
    acChecks_TRIPS_CFG.overVoltageThreshold1 = 10.0f;
    acChecks_TRIPS_CFG.overVoltageThreshold2 = 10.0f;
    float *target = under
        ? (tier == 1 ? &acChecks_TRIPS_CFG.underVoltageThreshold1 : &acChecks_TRIPS_CFG.underVoltageThreshold2)
        : (tier == 1 ? &acChecks_TRIPS_CFG.overVoltageThreshold1 : &acChecks_TRIPS_CFG.overVoltageThreshold2);
    *target = threshold;
    bool trip = under ? !strcmp(relation, "above") : !strcmp(relation, "below");
    unsigned expected = trip ? (under ? (tier == 1 ? 1u : 4u) : (tier == 1 ? 2u : 8u)) : 0;
    steps(4);
    check(voltage_trips() == expected, "UV/OV comparisons are strict at adjacent float thresholds");
}

static void qualification_boundary(const char *name)
{
    phases(0.9f, 1.0f, 1.1f);
    acChecks_QUALIFY_CFG.vMin = normalized(0);
    acChecks_QUALIFY_CFG.vMax = normalized(2);
    acChecks_TRIPS_CFG.underVoltageThreshold1 = 0.5f;
    acChecks_TRIPS_CFG.overVoltageThreshold1 = 1.5f;
    bool inside = !strcmp(name, "qualify_edges_inclusive");
    if (!strcmp(name, "qualify_below_lower"))
        acChecks_QUALIFY_CFG.vMin = nextafterf(normalized(0), INFINITY);
    if (!strcmp(name, "qualify_above_upper"))
        acChecks_QUALIFY_CFG.vMax = nextafterf(normalized(2), -INFINITY);
    steps(4);
    check(acChecks_QUALIFY_STATE.voltageInrange == inside, "qualification voltage bounds are inclusive");
    check(acChecks_OUT.acOk == inside, "cold qualification uses actual voltage extrema");
}

static void delay_recovery(void)
{
    healthy(); phases(0.6f, 1.0f, 1.0f);
    steps(3); check(voltage_trips() == 0, "no early undervoltage assertion");
    step(); check(voltage_trips() == 1, "undervoltage asserts after four 1-ms calls");
    step(); check(!acChecks_OUT.acOk, "sustained trip keeps acOk false");
    near(acChecks_QUALIFY_STATE.timer, 0, "qualification resets after sustained trip");
    /* Derived-config update is not a hidden static-state reset. */
    acChecks_update_derived_config();
    phases(1.0f, 1.0f, 1.0f);
    for (unsigned i = 0; i < 2; i++) {
        step(); check(voltage_trips() == 1, "clearing grace retains the voltage trip");
        check(!acChecks_OUT.acOk, "trip inhibits output during clearing");
    }
    step(); check(voltage_trips() == 0, "trip clears when excursion clearing timer expires");
    near(acChecks_TRIP_STATE.underVoltage1Timer, 0, "assertion timer resets when clearing output becomes false");
    check(!acChecks_OUT.acOk, "requalification still waits for its delay");
    step(); check(acChecks_OUT.acOk, "healthy requalification completes");
}

static void pulse(bool frequency, bool reset_only)
{
    healthy();
    if (frequency) acChecks_IN.freq = 56.5f;
    else phases(0.6f, 1.0f, 1.0f);
    steps(reset_only ? 1 : 2);
    phases(1.0f, 1.0f, 1.0f); acChecks_IN.freq = 60.0f;
    step(); check(voltage_trips() == 0 && frequency_trips() == 0, "pulse has not yet reached assertion delay");
    step();
    if (!reset_only) {
        check((frequency ? frequency_trips() : voltage_trips()) == 1,
              "clearing memory may assert a trip after raw input recovers");
        check(!acChecks_OUT.acOk, "trip during clearing inhibits output");
    } else {
        check(voltage_trips() == 0, "short pulse plus clearing remains below assertion delay");
    }
    step(); check(voltage_trips() == 0 && frequency_trips() == 0, "pulse trip clears and assertion state resets");
    if (reset_only) {
        near(acChecks_TRIP_STATE.underVoltage1Timer, 0, "short pulse assertion timer resets");
        phases(0.6f, 1.0f, 1.0f); step();
        near(acChecks_TRIP_STATE.underVoltage1Timer, dt, "a later pulse starts a new assertion interval");
    }
}

static void timer_helpers(void)
{
    float t = 0;
    for (unsigned i = 1; i <= 5; i++) {
        bool output = assert_timer(&t, 0.5f, 0.125f, true);
        check(output == (i >= 4), "assert_timer includes the exact delay boundary");
        near(t, i < 4 ? (float)i * 0.125f : 0.5f, "assert_timer saturates at delay");
    }
    check(!assert_timer(&t, 0.5f, 0.125f, false), "assert_timer clears immediately on false input");
    near(t, 0, "assert_timer false input resets state");
    check(clear_timer(&t, 0.5f, 0.125f, true), "clear_timer true input refreshes the full delay");
    for (unsigned i = 1; i <= 4; i++) {
        check(clear_timer(&t, 0.5f, 0.125f, false) == (i < 4), "clear_timer clears at exactly zero");
        near(t, 0.5f - (float)i * 0.125f, "clear_timer countdown is unchanged");
    }
}

struct scenario { const char *name; bool original_should_fail; };
static const struct scenario scenarios[] = {
    {"nominal_unequal", false}, {"qualify_low", true}, {"qualify_high", true},
    {"uv1_phase0", true}, {"uv1_phase1", true}, {"uv1_phase2", true},
    {"uv2_phase0", true}, {"uv2_phase1", true}, {"uv2_phase2", true},
    {"ov1_phase0", true}, {"ov1_phase1", true}, {"ov1_phase2", true},
    {"ov2_phase0", true}, {"ov2_phase1", true}, {"ov2_phase2", true},
    {"uv1_equal", false}, {"uv1_below", false}, {"uv1_above", true},
    {"uv2_equal", false}, {"uv2_below", false}, {"uv2_above", true},
    {"ov1_equal", false}, {"ov1_below", true}, {"ov1_above", false},
    {"ov2_equal", false}, {"ov2_below", true}, {"ov2_above", false},
    {"qualify_edges_inclusive", false}, {"qualify_below_lower", true}, {"qualify_above_upper", true},
    {"uv2_equal_uv1_active", true}, {"ov2_equal_ov1_active", true},
    {"zero_all", false}, {"zero_one_phase", false}, {"signed_negative", true},
    {"signed_zero", false}, {"nominal_35kV", false}, {"rating_floor", false},
    {"frequency_under1", false}, {"frequency_under2", false},
    {"frequency_over1", false}, {"frequency_over2", false},
    {"imbalance_reject", false}, {"imbalance_inclusive", false},
    {"qualification_bypass", false}, {"voltage_delay_recovery", true},
    {"voltage_pulse_clear_memory", true}, {"voltage_pulse_reset", true},
    {"frequency_pulse_clear_memory", false}, {"timer_helpers", false}
};

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "--list")) {
        for (unsigned i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++)
            printf("{\"name\":\"%s\",\"original_should_fail\":%s}\n", scenarios[i].name,
                   scenarios[i].original_should_fail ? "true" : "false");
        return 0;
    }
    const char *name = argv[1];
    bool known = false;
    for (unsigned i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++)
        known |= !strcmp(name, scenarios[i].name);
    if (!known) return 2;
    configure();
    if (!strcmp(name, "nominal_unequal")) healthy();
    else if ((name[0] == 'u' || name[0] == 'o') && strstr(name, "_phase")) excursion(name);
    else if (!strcmp(name, "qualify_low") || !strcmp(name, "qualify_high")) {
        phases(!strcmp(name, "qualify_low") ? 0.8f : 1.3f, 1.0f, 1.0f);
        steps(4); check(!acChecks_QUALIFY_STATE.voltageInrange, "out-of-band nonzero voltage fails cold qualification");
        check(!acChecks_OUT.acOk, "out-of-band cold input cannot qualify acOk");
    } else if (!strncmp(name, "qualify_", 8)) qualification_boundary(name);
    else if (!strcmp(name, "uv2_equal_uv1_active") || !strcmp(name, "ov2_equal_ov1_active")) {
        bool under = name[0] == 'u'; phases(under ? 0.3f : 1.3f, 1.0f, 1.0f);
        if (under) acChecks_TRIPS_CFG.underVoltageThreshold2 = normalized(0);
        else acChecks_TRIPS_CFG.overVoltageThreshold2 = normalized(0);
        steps(4); check(voltage_trips() == (under ? 1u : 2u), "equality at tier2 leaves tier1 independently active");
    } else if (!strncmp(name, "uv", 2) || !strncmp(name, "ov", 2)) voltage_boundary(name);
    else if (!strncmp(name, "zero_", 5) || !strncmp(name, "signed_", 7)) {
        if (!strcmp(name, "zero_all")) phases(0, 0, 0);
        else phases(!strcmp(name, "signed_negative") ? -0.1f : -0.0f, 1.0f, 1.0f);
        steps(4); check(voltage_trips() == 5, "zero/signed arithmetic extrema drive both undervoltage tiers");
        check(!acChecks_OUT.acOk, "zero/signed low input cannot qualify");
    } else if (!strcmp(name, "nominal_35kV") || !strcmp(name, "rating_floor")) {
        acChecks_GENERAL_CFG.VllRated = !strcmp(name, "rating_floor") ? 0.0f : 35000.0f;
        acChecks_update_derived_config(); phases(0.96f, 1.02f, 1.08f); healthy();
    } else if (!strncmp(name, "frequency_", 10) && !strstr(name, "pulse")) {
        unsigned expected;
        if (!strcmp(name, "frequency_under1")) { acChecks_IN.freq = 56.5f; expected = 1; }
        else if (!strcmp(name, "frequency_under2")) { acChecks_IN.freq = 54.0f; expected = 5; }
        else if (!strcmp(name, "frequency_over1")) { acChecks_IN.freq = 62.5f; expected = 2; }
        else { acChecks_IN.freq = 66.0f; expected = 10; }
        steps(3); check(frequency_trips() == 0, "frequency trip waits for its delay");
        step(); check(frequency_trips() == expected, "frequency tiers remain independent of voltage type correction");
        check(voltage_trips() == 0, "healthy phase voltages do not cause voltage trips");
        check(!acChecks_OUT.acOk, "frequency trip inhibits output");
    } else if (!strncmp(name, "imbalance_", 10)) {
        acChecks_IN.Vnve.real = 0.21f * acChecks_GENERAL_CFG.VllRated;
        bool inclusive = !strcmp(name, "imbalance_inclusive");
        if (inclusive) acChecks_QUALIFY_CFG.vNveLim = cmplx_mag(acChecks_IN.Vnve) * (1.0f / acChecks_GENERAL_CFG.VllRated);
        steps(4); check(acChecks_QUALIFY_STATE.vimbalanceInrange == inclusive, "negative-sequence qualification remains inclusive");
        check(acChecks_OUT.acOk == inclusive, "negative-sequence qualification controls cold connection");
    } else if (!strcmp(name, "qualification_bypass")) {
        healthy(); acChecks_QUALIFY_CFG.fMin = 0.1f; acChecks_QUALIFY_CFG.fMax = 0.2f;
        acChecks_IN.Vnve.real = acChecks_GENERAL_CFG.VllRated;
        /* Within trip limits, outside tight qualification bands. */
        phases(0.95f, 1.0f, 1.05f); acChecks_QUALIFY_CFG.vMin = 1.06f;
        for (unsigned i = 0; i < 4; i++) { step(); check(acChecks_OUT.acOk, "already-qualified bypass is preserved without a trip"); }
        check(!acChecks_QUALIFY_STATE.freqInrange && !acChecks_QUALIFY_STATE.vimbalanceInrange,
              "tight frequency/imbalance indicators can clear while already-qualified output stays true");
    } else if (!strcmp(name, "voltage_delay_recovery")) delay_recovery();
    else if (strstr(name, "pulse")) pulse(!strncmp(name, "frequency_", 10), strstr(name, "reset") != NULL);
    else if (!strcmp(name, "timer_helpers")) timer_helpers();
    else return 2;
    printf("{\"event\":\"result\",\"name\":\"%s\",\"checks\":%u,\"failures\":%u}\n", name, checks, failures);
    return failures ? 1 : 0;
}
