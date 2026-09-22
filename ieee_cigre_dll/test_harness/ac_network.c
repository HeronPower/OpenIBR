#define _USE_MATH_DEFINES
#include "ac_network.h"

#include <math.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* VLL_NOM/F_NOM/NETWORK_S_BASE are declared in ac_network.h -- see the
 * comments there. */
#define W_NOM    (2.0 * M_PI * F_NOM)
#define Z_BASE   ((VLL_NOM * VLL_NOM) / NETWORK_S_BASE)
#define L_BASE   (Z_BASE / W_NOM)

/* Weak-grid Thevenin/Norton branch: each of two independent grid
 * connections is an RL source behind an impedance sized by its own SCR
 * (short-circuit ratio) -- a higher SCR is a stronger/stiffer grid.
 * Forward-Euler translation of a weak-grid network block diagram. */
#define R_GRID      (0.02 * Z_BASE)
#define OVERL_BASE  (1.0 / L_BASE)

/* Fault injection: each fault type is either a conductance (resistive
 * faults) or an RL branch (the "soft" 3-phase fault), gated on/off by a
 * boolean and summed into a Norton current injection at the PCC. */
#define G_FAULT_LL_HARD      (1.0 / (0.03 * Z_BASE))
#define G_FAULT_LL_SOFT      (1.0 / (0.4  * Z_BASE))
#define G_FAULT_1PHG         (1.0 / (0.03 * Z_BASE))
#define G_FAULT_2PHG         (1.0 / (0.03 * Z_BASE))
#define G_FAULT_3PHG_BOLTED  (1.0 / (0.03 * Z_BASE))
#define OVER_LFAULT          (1.0 / (0.4  * L_BASE))
#define R_SERIES_FAULT       (0.03 * Z_BASE)

/* A large virtual shunt resistance plus a fast first-order low-pass
 * filter turns the net injected current into a PCC voltage without an
 * algebraic (zero-delay) loop between the two -- at the cost of resolving
 * that loop one filter time constant late. */
#define R_SHUNT           (30.0 * Z_BASE)
#define SOLVER_LPF_OMEGA  (1.0 / 400e-6)

static const double MASK_1PHG[3] = { 0.0, 0.0, 1.0 }; /* phase C */
static const double MASK_2PHG[3] = { 0.0, 1.0, 1.0 }; /* phases B-C */
static const double MASK_3PHG[3] = { 1.0, 1.0, 1.0 }; /* all phases */

/* Scenario: one function per grid/fault input, argument is simulation
 * time [s]. Edit these to change source strength, connection timing, or
 * fault timing -- everything else in this file is fixed circuit math.
 * Note: a weak grid (SCR ~1-2) combined with this controller's current
 * limiter can produce a sustained oscillation rather than settling. */

static double scenario_vll(double t) { (void)t; return VLL_NOM; }
static double scenario_f(double t)   { (void)t; return F_NOM; }

static double scenario_SCR1(double t)     { (void)t; return 1.0; }
static bool   scenario_connect1(double t) { (void)t; return t<25.0; }
static double scenario_SCR2(double t)     { (void)t; return 1.5; }
static bool   scenario_connect2(double t) { (void)t; return t<15.0; }

static bool scenario_LL_soft_fault(double t)     { return (t >= 5.0 && t < 5.2) || (t >= 17.0 && t < 17.2); }
static bool scenario_LL_hard_fault(double t)     { return (t >= 6.0 && t < 6.2) || (t >= 18.0 && t < 18.2); }
static bool scenario_fault_1PHG(double t)        { return (t >= 7.0 && t < 7.2) || (t >= 19.0 && t < 19.2); }
static bool scenario_fault_2PHG(double t)        { return (t >= 8.0 && t < 8.2) || (t >= 20.0 && t < 20.2); }
static bool scenario_fault_3PHG_soft(double t)   { return (t >= 9.0 && t < 9.2) || (t >= 21.0 && t < 21.2); }
static bool scenario_fault_3PHG_bolted(double t) { return (t >= 10.0 && t < 10.2) || (t >= 22.0 && t < 22.2); }

/* Sums the six fault types into a per-phase Norton current injection,
 * evaluated against the previous step's PCC voltage (v). i_rl is the
 * "soft" (inductive) 3-phase-to-ground fault's own integrator state. */
static void compute_fault_current(const double v[3],
                                  bool LL_soft_fault, bool LL_hard_fault,
                                  bool fault_1PHG, bool fault_2PHG,
                                  bool fault_3PHG_soft, bool fault_3PHG_bolted,
                                  double dt, double i_fault[3])
{
    static double i_rl[3] = { 0.0, 0.0, 0.0 };
    double i_ll;
    int k;

    for (k = 0; k < 3; k++)
    {
        double v_hph = v[k] - R_SERIES_FAULT * i_rl[k];
        double g = (fault_1PHG        ? MASK_1PHG[k] * G_FAULT_1PHG        : 0.0)
                 + (fault_2PHG        ? MASK_2PHG[k] * G_FAULT_2PHG        : 0.0)
                 + (fault_3PHG_bolted ? MASK_3PHG[k] * G_FAULT_3PHG_BOLTED : 0.0);

        i_rl[k] = fault_3PHG_soft ? (i_rl[k] + dt * v_hph * OVER_LFAULT) : 0.0;

        i_fault[k] = v_hph * g + i_rl[k];
    }

    i_ll = (v[1] - v[2]) * ((LL_hard_fault ? G_FAULT_LL_HARD : 0.0)
                          + (LL_soft_fault ? G_FAULT_LL_SOFT : 0.0));
    i_fault[1] += i_ll;
    i_fault[2] -= i_ll;
}

void ac_network_step(double t, double dt,
                     const double i_ac[3],
                     double v_ac[3])
{
    /* Per-branch/filter state carried across calls: the two independent
     * grid connections' RL currents, the solver's current-to-voltage
     * filter, the filtered fault current, and the previous step's PCC
     * voltage (fed back to break the algebraic loop -- see the comment
     * on R_SHUNT/SOLVER_LPF_OMEGA above). */
    static double i1[3]          = { 0.0, 0.0, 0.0 };
    static double i2[3]          = { 0.0, 0.0, 0.0 };
    static double solver_lpf[3]  = { 0.0, 0.0, 0.0 };
    static double solver_lpf1[3] = { 0.0, 0.0, 0.0 };
    static double v_pcc_prev[3]  = { 0.0, 0.0, 0.0 };

    double scr1, scr2, v_grid[3], v_pk, wt, i_fault[3];
    bool   connect1, connect2;
    int    k;

    scr1     = scenario_SCR1(t);
    connect1 = scenario_connect1(t);
    scr2     = scenario_SCR2(t);
    connect2 = scenario_connect2(t);

    v_pk = scenario_vll(t) * sqrt(2.0 / 3.0);
    wt   = 2.0 * M_PI * scenario_f(t) * t;
    v_grid[0] = v_pk * sin(wt);
    v_grid[1] = v_pk * sin(wt - 2.0 * M_PI / 3.0);
    v_grid[2] = v_pk * sin(wt + 2.0 * M_PI / 3.0);

    compute_fault_current(v_pcc_prev,
                          scenario_LL_soft_fault(t), scenario_LL_hard_fault(t),
                          scenario_fault_1PHG(t), scenario_fault_2PHG(t),
                          scenario_fault_3PHG_soft(t), scenario_fault_3PHG_bolted(t),
                          dt, i_fault);

    for (k = 0; k < 3; k++)
    {
        double e = v_grid[k] - v_pcc_prev[k];
        double i_net;

        i1[k] += dt * (e - R_GRID * i1[k]) * scr1 * OVERL_BASE;
        i1[k]  = connect1 ? i1[k] : 0.0;

        i2[k] += dt * (e - R_GRID * i2[k]) * scr2 * OVERL_BASE;
        i2[k]  = connect2 ? i2[k] : 0.0;

        solver_lpf1[k] += dt * SOLVER_LPF_OMEGA * (i_fault[k] - solver_lpf1[k]);

        i_net = i1[k] + i2[k] - solver_lpf1[k] + i_ac[k];

        solver_lpf[k] += dt * SOLVER_LPF_OMEGA * (i_net - solver_lpf[k]);

        v_ac[k]       = solver_lpf[k] * R_SHUNT;
        v_pcc_prev[k] = v_ac[k];
    }
}
