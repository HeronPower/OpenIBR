#ifndef TIMERS_H
#define TIMERS_H

    #include <stdbool.h>

    bool assert_timer(float *timer, float delay, float dt, bool in);
    bool clear_timer(float *timer, float delay, float dt, bool in);

#endif // TIMERS_H
