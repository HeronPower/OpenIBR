#include "timers.h"
#include "helper_macros.h"

bool assert_timer(float *timer, float delay, float dt, bool in)
{
    float timer_now = *timer;
    bool output;

    // update timer value
    if(in)
    {
        timer_now += dt;
    }
    else
    {
        timer_now = 0.0f;
    }

    // Check if timer has reached the delay threshold
    if (timer_now >= delay)
    {
        timer_now = delay;
        output = true;
    }
    else
    {
        output = false;
    }

    // clamp and update state
    *timer = clamp(timer_now, 0.0f, delay);

    return output;
}

bool clear_timer(float *timer, float delay, float dt, bool in)
{
    float timer_now = *timer;
    bool output;

    // update timer value
    if(in)
    {
        timer_now = delay;
    }
    else
    {
        timer_now -= dt;
    }

    // Check if timer has reached the delay threshold
    if (timer_now <= 0.0f)
    {
        timer_now = 0.0f;
        output = false;
    }
    else
    {
        output = true;
    }

    // clamp and update state
    *timer = clamp(timer_now, 0.0f, delay);

    return output;
}
