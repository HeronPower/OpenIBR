#ifndef CONVERTER_SIM_H
#define CONVERTER_SIM_H

#include <stdbool.h>

/* Converter output filter: the physical inductor+resistance between the
 * bridge and the AC terminals, one series RL branch per phase. v_bridge
 * and v_ac in, i_ac out. Forward-Euler, state kept as static locals.
 * When pwm_enable is false, i_ac is forced to 0 (open switches) rather
 * than running the RL equation.
 *
 * v_dc is a fixed stub for now (see converter_sim.c), returned here
 * rather than via a separate function since it will eventually come from
 * a real DC-bus model driven by these same inputs. */
void converter_sim_step(const double v_bridge[3],
                        const double v_ac[3],
                        bool pwm_enable,
                        double dt,
                        double i_ac[3],
                        double *v_dc);

#endif /* CONVERTER_SIM_H */
