/*
 * Host-side tests for the ac_checks_A blackstart permissive (issue #11).
 *
 * ac_checks_A keeps its timers in function-level statics, so every scenario
 * runs in a fresh process: `test_blackstart <scenario>`. run_tests.py builds
 * this file together with ac_checks_A.c and runs each scenario in turn.
 *
 * All scenarios step acChecks_1kHz() at 1 ms on a 480 V, 60 Hz system.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../companion_algorithms/ac_checks_A/acChecks_A_autogen.h"

#define DT          1e-3f
#define VLL_RATED   480.0f
#define F_NOM       60.0f

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                                                  \
    do {                                                                  \
        checks++;                                                         \
        if (!(cond)) {                                                    \
            failures++;                                                   \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                 \
            printf(__VA_ARGS__);                                          \
            printf("\n");                                                 \
        }                                                                 \
    } while (0)

static float sim_t = 0.0f;
static long sim_k = 0;

static void setup(void)
{
    set_acChecks_GENERAL_CFG_VllRated(VLL_RATED);
    set_acChecks_GENERAL_CFG_nominalFrequency(F_NOM);
    acChecks_update_derived_config();
}

/* Apply a balanced per-unit voltage, a frequency and the blackstart command,
 * then run one 1 ms step. */
static void step(float v_pu, float freq, bool bs_cmd)
{
    int i;
    for (i = 0; i < 3; i++) {
        acChecks_IN.Vph[i] = v_pu * VLL_RATED / sqrtf(3.0f);
    }
    acChecks_IN.Vnve.real = 0.0f;
    acChecks_IN.Vnve.imag = 0.0f;
    acChecks_IN.freq = freq;
    acChecks_IN.bsCmd = bs_cmd;
    acChecks_1kHz(DT);
    sim_t += DT;
    sim_k++;

    /* Invariant on every step: enable is exactly acOk OR the latch. */
    CHECK(acChecks_OUT.enable == (acChecks_OUT.acOk || acChecks_BLACKSTART_STATE.active),
          "enable != acOk || active at t=%.3f", sim_t);
}

static void run(float seconds, float v_pu, float freq, bool bs_cmd)
{
    long n = lroundf(seconds / DT);
    long i;
    for (i = 0; i < n; i++) {
        step(v_pu, freq, bs_cmd);
    }
}

/* One-step rising edge on bsCmd. */
static void pulse(float v_pu, float freq)
{
    step(v_pu, freq, true);
    step(v_pu, freq, false);
}

/* Count how long the latch stays open with the given inputs held. */
static float measure_window(float v_pu, float freq, float limit)
{
    float open_time = 0.0f;
    long n = lroundf(limit / DT);
    long i;
    for (i = 0; i < n && acChecks_BLACKSTART_STATE.active; i++) {
        step(v_pu, freq, false);
        open_time += DT;
    }
    return open_time;
}

/* ------------------------------------------------------------------------ */

static void live_bus_ignores_command(void)
{
    run(7.0f, 1.0f, F_NOM, false);
    CHECK(acChecks_OUT.acOk, "acOk should qualify on a healthy bus");
    CHECK(!acChecks_BLACKSTART_STATE.deadBus, "healthy bus must not read as dead");
    pulse(1.0f, F_NOM);
    run(1.0f, 1.0f, F_NOM, false);
    CHECK(!acChecks_BLACKSTART_STATE.active, "blackstart must not latch on a live bus");
}

static void partially_energized_bus_ignores_command(void)
{
    /* 0.3 pu is neither dead (<= 0.1 pu) nor qualified. */
    run(3.0f, 0.3f, F_NOM, false);
    pulse(0.3f, F_NOM);
    CHECK(!acChecks_BLACKSTART_STATE.active, "0.3 pu bus is not dead, no blackstart");
    CHECK(!acChecks_OUT.enable, "no permissive without qualification or blackstart");
}

static void requires_dead_time(void)
{
    run(0.5f, 0.0f, 0.0f, false);                 /* dead for 0.5 s < tDeadBus */
    pulse(0.0f, 0.0f);
    CHECK(!acChecks_BLACKSTART_STATE.active, "command before tDeadBus must be ignored");
    run(0.6f, 0.0f, 0.0f, false);                 /* now dead for > 1 s */
    CHECK(acChecks_BLACKSTART_STATE.deadBus, "bus should be qualified dead after 1 s");
    pulse(0.0f, 0.0f);
    CHECK(acChecks_BLACKSTART_STATE.active, "command after tDeadBus must latch");
    CHECK(acChecks_OUT.enable, "permissive must be on during the window");
    CHECK(!acChecks_OUT.acOk, "acOk itself stays false on the dead bus");
}

static void window_expires_without_voltage(void)
{
    float window;
    run(2.0f, 0.0f, 0.0f, false);
    pulse(0.0f, 0.0f);
    CHECK(acChecks_BLACKSTART_STATE.active, "should latch");
    /* one step of the window has already elapsed inside pulse() */
    window = measure_window(0.0f, 0.0f, 20.0f) + DT;
    CHECK(fabsf(window - acChecks_BLACKSTART_CFG.tBlackstart) <= 2.0f * DT,
          "window %.3f s, expected %.3f s", window, acChecks_BLACKSTART_CFG.tBlackstart);
    CHECK(!acChecks_OUT.enable, "permissive must drop when the window closes on a dead bus");
    /* still dead, no new edge: must stay off */
    run(10.0f, 0.0f, 0.0f, false);
    CHECK(!acChecks_BLACKSTART_STATE.active, "no restart without a new rising edge");
}

static void held_command_does_not_retrigger(void)
{
    run(2.0f, 0.0f, 0.0f, false);
    step(0.0f, 0.0f, true);                       /* rising edge, then hold */
    CHECK(acChecks_BLACKSTART_STATE.active, "should latch on the edge");
    run(acChecks_BLACKSTART_CFG.tBlackstart + 0.1f, 0.0f, 0.0f, true);
    CHECK(!acChecks_BLACKSTART_STATE.active, "window must close even with bsCmd held high");
    run(10.0f, 0.0f, 0.0f, true);
    CHECK(!acChecks_BLACKSTART_STATE.active, "a held command is not a new trigger");
    step(0.0f, 0.0f, false);
    step(0.0f, 0.0f, true);                       /* new edge */
    CHECK(acChecks_BLACKSTART_STATE.active, "a fresh rising edge starts a new window");
}

static void command_held_since_before_dead_bus_is_ignored(void)
{
    /* Operator holds bsCmd high while the bus is still alive, then it dies. */
    run(1.0f, 1.0f, F_NOM, true);
    run(3.0f, 0.0f, 0.0f, true);
    CHECK(acChecks_BLACKSTART_STATE.deadBus, "bus is dead");
    CHECK(!acChecks_BLACKSTART_STATE.active, "edge happened on a live bus, so no trigger");
}

static void formed_voltage_qualifies_without_gap(void)
{
    long k;
    bool dropped = false;
    float t_acok = -1.0f;
    float t_start;

    set_acChecks_QUALIFY_CFG_tRequalify(2.0f);    /* as in the SIL/DLL apps */
    set_acChecks_BLACKSTART_CFG_tBlackstart(3.0f);/* tRequalify + 1 s */

    run(2.0f, 0.0f, 0.0f, false);
    pulse(0.0f, 0.0f);
    t_start = sim_t;
    /* inverter ramps the bus to 1 pu over 0.5 s, then holds it */
    for (k = 0; k < 6000; k++) {
        float tau = sim_t - t_start;
        float v = tau < 0.5f ? tau / 0.5f : 1.0f;
        step(v, F_NOM, false);
        if (acChecks_OUT.acOk && t_acok < 0.0f) {
            t_acok = sim_t - t_start;
        }
        if (!acChecks_OUT.enable) {
            dropped = true;
        }
    }
    CHECK(t_acok > 0.0f && t_acok < 3.0f, "acOk qualified at %.3f s, inside the window", t_acok);
    CHECK(!acChecks_BLACKSTART_STATE.active, "the window itself still times out");
    CHECK(acChecks_OUT.acOk && acChecks_OUT.enable, "unit keeps running on qualified ac");
    CHECK(!dropped, "permissive must not blink off between blackstart and qualification");
}

static void window_too_short_drops_unit(void)
{
    /* tBlackstart shorter than tRequalify: the documented misconfiguration. */
    set_acChecks_QUALIFY_CFG_tRequalify(2.0f);
    set_acChecks_BLACKSTART_CFG_tBlackstart(1.0f);
    run(2.0f, 0.0f, 0.0f, false);
    pulse(0.0f, 0.0f);
    run(1.5f, 1.0f, F_NOM, false);
    CHECK(!acChecks_OUT.enable, "window closed before acOk could qualify");
}

static void ov_trip_releases_latch_and_does_not_restart(void)
{
    /* IEEE 1547-style fast OV2 so it can trip inside the window */
    set_acChecks_TRIPS_CFG_overVoltageThreshold2(1.2f);
    set_acChecks_TRIPS_CFG_overVoltageTimeout2(0.16f);

    run(2.0f, 0.0f, 0.0f, false);
    pulse(0.0f, 0.0f);
    run(0.5f, 1.3f, F_NOM, false);                /* bus overshoots to 1.3 pu */
    CHECK(acChecks_TRIP_STATE.overVoltage2State, "OV2 should trip at 1.3 pu");
    CHECK(!acChecks_BLACKSTART_STATE.active, "OV trip must release the latch");
    CHECK(!acChecks_OUT.enable, "permissive off after OV trip");

    /* OV clears well inside what would have been the 6 s window */
    run(0.2f, 0.0f, 0.0f, false);
    CHECK(!acChecks_TRIP_STATE.overVoltage2State, "OV trip cleared");
    run(3.0f, 0.0f, 0.0f, false);
    CHECK(!acChecks_BLACKSTART_STATE.active, "cleared OV must not restart blackstart");
    CHECK(!acChecks_OUT.enable, "permissive stays off until a new command");

    pulse(0.0f, 0.0f);
    CHECK(acChecks_BLACKSTART_STATE.active, "a new command after the OV starts a new window");
}

static void disabled_with_zero_window(void)
{
    set_acChecks_BLACKSTART_CFG_tBlackstart(0.0f);
    run(2.0f, 0.0f, 0.0f, false);
    pulse(0.0f, 0.0f);
    CHECK(!acChecks_BLACKSTART_STATE.active, "tBlackstart = 0 disables blackstart");
    CHECK(!acChecks_OUT.enable, "no permissive");
}

static void voltage_trips_use_real_magnitudes(void)
{
    /* Regression for issue #10: min/max phase voltage were stored as bool, so
     * any nonzero voltage read as 1 pu and voltage trips could never fire. */
    set_acChecks_TRIPS_CFG_overVoltageThreshold2(1.2f);
    set_acChecks_TRIPS_CFG_overVoltageTimeout2(0.16f);
    set_acChecks_TRIPS_CFG_underVoltageThreshold2(0.5f);
    set_acChecks_TRIPS_CFG_underVoltageTimeout2(0.16f);

    run(0.5f, 1.3f, F_NOM, false);
    CHECK(acChecks_TRIP_STATE.overVoltage2State, "1.3 pu must trip OV2");
    run(0.5f, 1.0f, F_NOM, false);
    CHECK(!acChecks_TRIP_STATE.overVoltage2State, "OV2 clears at 1.0 pu");
    run(0.5f, 0.3f, F_NOM, false);
    CHECK(acChecks_TRIP_STATE.underVoltage2State, "0.3 pu must trip UV2");
}

/* Randomized inputs, checked against the rules in issue #11 on every step. */
static unsigned int rng_state = 12345u;
static unsigned int rng(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 8) & 0xFFFFu;
}

static void random_invariants(void)
{
    const char *seed_env = getenv("BLACKSTART_SEED");
    long seconds = 600;
    long n, k;
    bool prev_active = false;
    bool prev_cmd = false;
    float open_time = 0.0f;
    long rises = 0, expiries = 0, ov_releases = 0;
    float v = 0.0f;
    bool cmd = false;
    long hold = 0;
    float tbs;

    if (seed_env) {
        rng_state = (unsigned int)strtoul(seed_env, NULL, 10);
    }
    set_acChecks_TRIPS_CFG_overVoltageThreshold2(1.2f);
    set_acChecks_TRIPS_CFG_overVoltageTimeout2(0.16f);
    set_acChecks_QUALIFY_CFG_tRequalify(2.0f);
    set_acChecks_BLACKSTART_CFG_tBlackstart(3.0f);
    tbs = acChecks_BLACKSTART_CFG.tBlackstart;

    n = seconds * 1000;
    for (k = 0; k < n; k++) {
        bool ov_now, edge;
        if (hold-- <= 0) {
            /* new random segment: dead, low, healthy or overvoltage bus */
            unsigned int r = rng() % 4;
            v = (r == 0) ? 0.0f : (r == 1) ? 0.05f + 0.4f * (rng() % 100) / 100.0f
              : (r == 2) ? 1.0f : 1.3f;
            hold = 50 + (long)(rng() % 3000);
        }
        if (rng() % 500 == 0) {
            cmd = !cmd;
        }
        step(v, v > 0.0f ? F_NOM : 0.0f, cmd);

        edge = cmd && !prev_cmd;
        ov_now = acChecks_TRIP_STATE.overVoltage1State || acChecks_TRIP_STATE.overVoltage2State;

        if (acChecks_BLACKSTART_STATE.active && !prev_active) {
            rises++;
            CHECK(edge, "latch rose without a rising edge at t=%.3f", sim_t);
            CHECK(acChecks_BLACKSTART_STATE.deadBus, "latch rose on a bus that is not dead at t=%.3f", sim_t);
            open_time = 0.0f;
        }
        if (acChecks_BLACKSTART_STATE.active) {
            open_time += DT;
            CHECK(open_time <= tbs + 2.0f * DT, "window longer than tBlackstart at t=%.3f", sim_t);
            CHECK(!ov_now, "latch open during an OV trip at t=%.3f", sim_t);
        }
        if (!acChecks_BLACKSTART_STATE.active && prev_active) {
            bool expired = fabsf(open_time + DT - tbs) <= 2.0f * DT;
            CHECK(expired || ov_now, "latch released without expiry or OV at t=%.3f", sim_t);
            if (ov_now) ov_releases++; else expiries++;
        }
        prev_active = acChecks_BLACKSTART_STATE.active;
        prev_cmd = cmd;
    }
    printf("  random_invariants: %ld s simulated, %ld windows opened, %ld expired, %ld released by OV\n",
           seconds, rises, expiries, ov_releases);
    CHECK(rises > 10 && expiries > 0 && ov_releases > 0,
          "random run should exercise every path (rises=%ld expiries=%ld ov=%ld)",
          rises, expiries, ov_releases);
}

struct scenario {
    const char *name;
    void (*fn)(void);
};

static const struct scenario scenarios[] = {
    {"live_bus_ignores_command", live_bus_ignores_command},
    {"partially_energized_bus_ignores_command", partially_energized_bus_ignores_command},
    {"requires_dead_time", requires_dead_time},
    {"window_expires_without_voltage", window_expires_without_voltage},
    {"held_command_does_not_retrigger", held_command_does_not_retrigger},
    {"command_held_since_before_dead_bus_is_ignored", command_held_since_before_dead_bus_is_ignored},
    {"formed_voltage_qualifies_without_gap", formed_voltage_qualifies_without_gap},
    {"window_too_short_drops_unit", window_too_short_drops_unit},
    {"ov_trip_releases_latch_and_does_not_restart", ov_trip_releases_latch_and_does_not_restart},
    {"disabled_with_zero_window", disabled_with_zero_window},
    {"voltage_trips_use_real_magnitudes", voltage_trips_use_real_magnitudes},
    {"random_invariants", random_invariants},
};

int main(int argc, char **argv)
{
    size_t i;
    if (argc != 2) {
        for (i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++) {
            printf("%s\n", scenarios[i].name);
        }
        return 0;
    }
    for (i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++) {
        if (strcmp(argv[1], scenarios[i].name) == 0) {
            setup();
            scenarios[i].fn();
            printf("%s %s (%d checks)\n", failures ? "FAIL" : "PASS", argv[1], checks);
            return failures ? 1 : 0;
        }
    }
    printf("unknown scenario %s\n", argv[1]);
    return 2;
}
