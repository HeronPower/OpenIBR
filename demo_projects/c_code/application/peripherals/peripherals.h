#ifndef PERIPHERALS_H
    #define PERIPHERALS_H

    #include <stdbool.h>

    #include "../../../c_language_library/helper_lib/helper_macros.h"
    #include "../parameter_definitions.h"

    struct external_commands_t
    {
        bool enable;
        float P_request;
        float Q_request;
        float V_ref;
        float dF_ref;

        // Configurable debug DAC channels (see configurable_dac.c)
        float scale_dac1;
        float offset_dac1;
        int signal_ID_dac1;
        float scale_dac2;
        float offset_dac2;
        int signal_ID_dac2;
    };

    void buildPeripherals(void);

    // ADC end-of-conversion ISR (defined in main.c, registered in initADC)
    __interrupt void adcBISR(void);

    void update_duty(float duty[3], bool enable);
    void contactor_enable(bool en);

    void get_sensor_values(float *v_dc, float v_ac[3], float i_L[3]);

    void receive_uart_packet(struct external_commands_t *values);
    void writeSerialByte(char in);

    // DAC control functions
    void debugDac1(float value);
    void debugDac2(float value);

#endif  // PERIPHERALS_H
