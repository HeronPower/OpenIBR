#ifndef INVERTER_REQUESTS_H
#define INVERTER_REQUESTS_H

/* Host-side inverter setpoints vs time. Not network events — those live
 * in ac_network.c. Edit inverter_requests.c to change the dispatch
 * sequence; the harness just applies the returned struct each step. */

typedef struct
{
    double enable;
    double P_request;
    double Q_request;
    double P_lim_high;
    double P_lim_low;
} InverterRequests;

InverterRequests inverter_requests(double t);

#endif /* INVERTER_REQUESTS_H */
