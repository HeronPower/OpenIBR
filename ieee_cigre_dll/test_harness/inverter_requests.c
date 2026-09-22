#include "inverter_requests.h"

InverterRequests inverter_requests(double t)
{
    InverterRequests r;

    (void)t;

    /* Enabled throughout: dispatch 500 kW from t=3s (after the fault
     * scenario in ac_network.c starts, see there for exact timing) to
     * t=26s, then reverse to -500 kW after t=28s to exercise power-flow
     * reversal, zero otherwise. */
    r.enable     = 1.0;
    r.P_request  = (t>3.0  && t<26.0 ? 500e3 : 0.0) + (t>28.0 ? -500e3 : 0.0);
    r.Q_request  = 0.0;
    r.P_lim_high = 1.0e6;
    r.P_lim_low  = -1.0e6;

    return r;
}
