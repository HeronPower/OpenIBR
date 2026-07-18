#include "configurable_dac.h"
#include "configurable_dac_switch_autogen.h"
#include "../peripherals/peripherals.h"

void configurable_dac_update(float scale_dac1, float offset_dac1, int signal_ID_dac1,
                             float scale_dac2, float offset_dac2, int signal_ID_dac2)
{
    float signal1 = configurable_dac_get_signal_value(signal_ID_dac1);
    float signal2 = configurable_dac_get_signal_value(signal_ID_dac2);

    float dac1 = scale_dac1 * signal1 + offset_dac1;
    float dac2 = scale_dac2 * signal2 + offset_dac2;

    debugDac1(dac1);
    debugDac2(dac2);
}
