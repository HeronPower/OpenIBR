#ifndef AC_NETWORK_H
#define AC_NETWORK_H

/* Host-side AC network: always i_ac[3] in, v_ac[3] out.
 *
 * A weak-grid model: two independent Thevenin/Norton-equivalent grid
 * sources (each an RL branch sized by its own short-circuit ratio) plus
 * six fault types (line-line, 1/2-phase-to-ground, 3-phase bolted/soft),
 * injected as currents at the PCC. Voltage/frequency/grid-strength/fault
 * vs time all live in ac_network.c (the scenario functions of t) -- the
 * harness just calls ac_network_step() each step. */

/* Voltage/frequency must track the inverter's own terminal rating (see
 * VLL_RATED/F_NOM_RATED in gfm_battery_app.h -- this header's own F_NOM is
 * a separate, same-valued constant; see gfm_battery_app.h's comment on
 * F_NOM_RATED) -- there's no transformer modeled here, so the PCC is
 * directly at the inverter's terminals. NETWORK_S_BASE is deliberately
 * separate from the inverter's own S_RATED: SCR (short-circuit ratio) is
 * defined relative to this fixed network base, so "SCR=20" means the same
 * absolute grid impedance no matter which inverter (1 MVA, 2 MVA, ...) is
 * under test. */
#define VLL_NOM         480.0
#define F_NOM           60.0
#define NETWORK_S_BASE  1.0e6

void ac_network_step(double t, double dt,
                     const double i_ac[3],
                     double v_ac[3]);

#endif /* AC_NETWORK_H */
