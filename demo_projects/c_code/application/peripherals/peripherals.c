//
// Included Files
//
#include "driverlib.h"
#include "device.h"
#include "peripherals.h"

// HARDWARE ABSTRACTION
// Sensor hal
#define ADC_SCALING_12B (1.0f / 4096.0f) // 12-bit ADC range: 0-4095
void get_sensor_values(float *v_dc, float v_ac[3], float i_L[3])
{
    unsigned int i0_adc = ADC_readResult(ADCBRESULT_BASE, ADC_SOC_NUMBER0);
    unsigned int i1_adc = ADC_readResult(ADCBRESULT_BASE, ADC_SOC_NUMBER1);
    unsigned int i2_adc = ADC_readResult(ADCBRESULT_BASE, ADC_SOC_NUMBER2);
    unsigned int vdc_adc = ADC_readResult(ADCBRESULT_BASE, ADC_SOC_NUMBER3);

    unsigned int v0_adc = ADC_readResult(ADCCRESULT_BASE, ADC_SOC_NUMBER0);
    unsigned int v1_adc = ADC_readResult(ADCCRESULT_BASE, ADC_SOC_NUMBER1);
    unsigned int v2_adc = ADC_readResult(ADCCRESULT_BASE, ADC_SOC_NUMBER2);
    unsigned int vmid_adc = ADC_readResult(ADCCRESULT_BASE, ADC_SOC_NUMBER3);

    // Current measurements
    i_L[0] = (((float)i0_adc - vmid_adc) * 2.0f * ADC_SCALING_12B) * IAC_ADC_RANGE;
    i_L[1] = (((float)i1_adc - vmid_adc) * 2.0f * ADC_SCALING_12B) * IAC_ADC_RANGE;
    i_L[2] = (((float)i2_adc - vmid_adc) * 2.0f * ADC_SCALING_12B) * IAC_ADC_RANGE;

    // AC voltage measurements
    v_ac[0] = (((float)v0_adc - vmid_adc) * 2.0f * ADC_SCALING_12B) * VAC_ADC_RANGE;
    v_ac[1] = (((float)v1_adc - vmid_adc) * 2.0f * ADC_SCALING_12B) * VAC_ADC_RANGE;
    v_ac[2] = (((float)v2_adc - vmid_adc) * 2.0f * ADC_SCALING_12B) * VAC_ADC_RANGE;

    // DC voltage measurement
    *v_dc = (float)vdc_adc * ADC_SCALING_12B * VDC_ADC_RANGE;
}

// PWM/Actuator hal
void enable_pwm(bool enable)
{
    // use rising-edge logic to avoid the glitch that was occuring when toggling GPIO
    static bool last_enable = false;

    if (enable)
    {
        if(!last_enable)
        {
            // Switch pins back to PWM mode
            GPIO_setPinConfig(GPIO_8_EPWM5_A);
            GPIO_setPinConfig(GPIO_9_EPWM5_B);
            GPIO_setPinConfig(GPIO_6_EPWM4_A);
            GPIO_setPinConfig(GPIO_7_EPWM4_B);
            GPIO_setPinConfig(GPIO_10_EPWM6_A);
            GPIO_setPinConfig(GPIO_11_EPWM6_B);
        }
    }
    else
    {
        // Switch pins to GPIO mode and force them low
        GPIO_setPinConfig(GPIO_6_GPIO6);
        GPIO_writePin(6, 0);
        GPIO_setPinConfig(GPIO_7_GPIO7);
        GPIO_writePin(7, 0);
        GPIO_setPinConfig(GPIO_8_GPIO8);
        GPIO_writePin(8, 0);
        GPIO_setPinConfig(GPIO_9_GPIO9);
        GPIO_writePin(9, 0);
        GPIO_setPinConfig(GPIO_10_GPIO10);
        GPIO_writePin(10, 0);
        GPIO_setPinConfig(GPIO_11_GPIO11);
        GPIO_writePin(11, 0);
    }

    last_enable = enable;
}

void contactor_enable(bool en)
{
    GPIO_writePin(75, en);
}

#define INVERTER_PWM_PERIOD         2500    // up-down count PWM period for 20kHz at 100MHz clock
#define DEADTIME_CYCLES    30
void update_duty(float duty[3], bool enable)
{
    // Update PWM duty cycles for all 3 phases
    // duty[0] -> PWM5 (GPIO8/GPIO9)
    // duty[1] -> PWM4 (GPIO6/GPIO7)
    // duty[2] -> PWM6 (GPIO10/GPIO11)

    // Convert duty cycle (0.0 to 1.0) to compare value
    // For up-down mode: Compare A = duty * Period
    // Period = 2499, so Compare A range = 0 to 2499
    int32_t compare_5 = (int32_t)(duty[0] * INVERTER_PWM_PERIOD);
    int32_t compare_4 = (int32_t)(duty[1] * INVERTER_PWM_PERIOD);
    int32_t compare_6 = (int32_t)(duty[2] * INVERTER_PWM_PERIOD);

    uint16_t compare_a_5 = (uint16_t)clamp(compare_5, 0, INVERTER_PWM_PERIOD);
    uint16_t compare_a_4 = (uint16_t)clamp(compare_4, 0, INVERTER_PWM_PERIOD);
    uint16_t compare_a_6 = (uint16_t)clamp(compare_6, 0, INVERTER_PWM_PERIOD);

    // Set the compare values for each PWM module
    EPWM_setCounterCompareValue(EPWM4_BASE, EPWM_COUNTER_COMPARE_A, compare_a_4);
    EPWM_setCounterCompareValue(EPWM5_BASE, EPWM_COUNTER_COMPARE_A, compare_a_5);
    EPWM_setCounterCompareValue(EPWM6_BASE, EPWM_COUNTER_COMPARE_A, compare_a_6);

    // Enable/disable PWM outputs
    enable_pwm(enable);
}

// Debug hal
void debugDac1(float value)
{
    value = clamp(value, 0.0, 3.0f);

    // Map float value from 0 to 3 to DAC range 0-4095
    unsigned int dac_value = (unsigned int)((value * 0.333f) * 4095.0f);
    DAC_setShadowValue(DACA_BASE, dac_value);
}

void debugDac2(float value)
{
    value = clamp(value, 0.0, 3.0f);

    // Map float value from 0 to 3 to DAC range 0-4095
    unsigned int dac_value = (unsigned int)((value * 0.333f) * 4095.0f);
    DAC_setShadowValue(DACC_BASE, dac_value);
}

// Comms interface hal
#define MAX_MESSAGE_SIZE 30
static uint8_t message_buffer[MAX_MESSAGE_SIZE];
static unsigned int buffer_index = 0;
static unsigned int expected_message_size = 30; // 5 values * 2 bytes each, plus 2 configurable-DAC channels * (scale + offset + signal_ID) = 10 + 20 = 30

static float bytesToFloatLE(const uint8_t *buf)
{
    union { uint32_t u; float f; } converter;
    converter.u = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    return converter.f;
}

void receive_uart_packet(struct external_commands_t *values)
{
    // Check how many bytes are available in the receive FIFO
    SCI_RxFIFOLevel fifoLevel = SCI_getRxFIFOStatus(SCIA_BASE);
    unsigned int bytesReceived = 0;
    
    // Read all available bytes from the FIFO
    while (fifoLevel != SCI_FIFO_RX0)
    {
        uint16_t receivedChar = SCI_readCharNonBlocking(SCIA_BASE);
        bytesReceived++;
        
        // Add byte to message buffer if there's space
        if (buffer_index < MAX_MESSAGE_SIZE)
        {
            message_buffer[buffer_index] = (uint8_t)receivedChar;
            buffer_index++;
        }
        
        // Update FIFO level after reading each character
        fifoLevel = SCI_getRxFIFOStatus(SCIA_BASE);
    }

    static unsigned int message_counter = 0;
    message_counter++;

    // Process complete message if we have received the expected number of bytes
    if (buffer_index >= expected_message_size)
    {
        // Parse the structured message (little-endian format: HhhHh)
        uint16_t enable = (uint16_t)message_buffer[0] | ((uint16_t)message_buffer[1] << 8);
        int16_t P_request_raw = (int16_t)((uint16_t)message_buffer[2] | ((uint16_t)message_buffer[3] << 8));
        int16_t Q_request_raw = (int16_t)((uint16_t)message_buffer[4] | ((uint16_t)message_buffer[5] << 8));
        uint16_t V_ref_raw = (uint16_t)message_buffer[6] | ((uint16_t)message_buffer[7] << 8);
        int16_t F_delta_mHz_raw = (int16_t)((uint16_t)message_buffer[8] | ((uint16_t)message_buffer[9] << 8));
        
        // Convert values to floating point using multiplication instead of division
        values->enable = enable > 0;
        values->P_request = (float)P_request_raw * 1e3f;  // Convert kW to W
        values->Q_request = (float)Q_request_raw * 1e3f;  // Convert kVAR to VAR
        values->V_ref = (float)V_ref_raw;  // Direct voltage in V
        values->dF_ref = (float)F_delta_mHz_raw * 0.001f;  // Convert mHz to Hz delta

        // Configurable DAC channel 1: scale(f), offset(f), signal_ID(H)
        values->scale_dac1 = bytesToFloatLE(&message_buffer[10]);
        values->offset_dac1 = bytesToFloatLE(&message_buffer[14]);
        values->signal_ID_dac1 = (int)((uint16_t)message_buffer[18] | ((uint16_t)message_buffer[19] << 8));

        // Configurable DAC channel 2: scale(f), offset(f), signal_ID(H)
        values->scale_dac2 = bytesToFloatLE(&message_buffer[20]);
        values->offset_dac2 = bytesToFloatLE(&message_buffer[24]);
        values->signal_ID_dac2 = (int)((uint16_t)message_buffer[28] | ((uint16_t)message_buffer[29] << 8));

        // Reset buffer for next message
        buffer_index = 0;
        
        // Send acknowledgment
        writeSerialByte('M'); // Message received
        writeSerialByte('S'); // Success
        writeSerialByte('\n');
    }

    // Clear the interrupt flag
    SCI_clearInterruptStatus(SCIA_BASE, SCI_INT_RXFF);
    
    // Acknowledge the interrupt
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP9);
}

void writeSerialByte(char in)
{
    SCI_writeCharNonBlocking(SCIA_BASE, in);
}

// INITIALIZATION
void initPinMux(void)
{
	//
	// ANALOG -> myANALOGPinMux0 Pinmux
	//
	// Analog PinMux for A0/DACA_OUT
	GPIO_setPinConfig(GPIO_227_GPIO227);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(227, GPIO_ANALOG_ENABLED);
	// Analog PinMux for A1
	GPIO_setPinConfig(GPIO_228_GPIO228);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(228, GPIO_ANALOG_ENABLED);
	// Analog PinMux for A10, GPIO213
	GPIO_setPinConfig(GPIO_213_GPIO213);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(213, GPIO_ANALOG_ENABLED);
	// Analog PinMux for A11, GPIO214
	GPIO_setPinConfig(GPIO_214_GPIO214);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(214, GPIO_ANALOG_ENABLED);
	// Analog PinMux for A14/B14/C14
	GPIO_setPinConfig(GPIO_225_GPIO225);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(225, GPIO_ANALOG_ENABLED);
	// Analog PinMux for A15/B15/C15
	GPIO_setPinConfig(GPIO_226_GPIO226);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(226, GPIO_ANALOG_ENABLED);
	// Analog PinMux for A2
	GPIO_setPinConfig(GPIO_229_GPIO229);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(229, GPIO_ANALOG_ENABLED);
	// Analog PinMux for A3
	GPIO_setPinConfig(GPIO_230_GPIO230);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(230, GPIO_ANALOG_ENABLED);
	// Analog PinMux for A4
	GPIO_setPinConfig(GPIO_231_GPIO231);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(231, GPIO_ANALOG_ENABLED);
	// Analog PinMux for A5
	GPIO_setPinConfig(GPIO_232_GPIO232);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(232, GPIO_ANALOG_ENABLED);
	// Analog PinMux for A6, GPIO209
	GPIO_setPinConfig(GPIO_209_GPIO209);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(209, GPIO_ANALOG_ENABLED);
	// Analog PinMux for A7, GPIO210
	GPIO_setPinConfig(GPIO_210_GPIO210);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(210, GPIO_ANALOG_ENABLED);
	// Analog PinMux for A8, GPIO211
	GPIO_setPinConfig(GPIO_211_GPIO211);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(211, GPIO_ANALOG_ENABLED);
	// Analog PinMux for A9, GPIO212
	GPIO_setPinConfig(GPIO_212_GPIO212);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(212, GPIO_ANALOG_ENABLED);
	// Analog PinMux for B0/VDAC
	GPIO_setPinConfig(GPIO_233_GPIO233);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(233, GPIO_ANALOG_ENABLED);
	// Analog PinMux for B1/DACC_OUT
	GPIO_setPinConfig(GPIO_234_GPIO234);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(234, GPIO_ANALOG_ENABLED);
	// Analog PinMux for B10, GPIO219
	GPIO_setPinConfig(GPIO_219_GPIO219);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(219, GPIO_ANALOG_ENABLED);
	// Analog PinMux for B11
	GPIO_setPinConfig(GPIO_240_GPIO240);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(240, GPIO_ANALOG_ENABLED);
	// Analog PinMux for B13
	GPIO_setPinConfig(GPIO_238_GPIO238);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(238, GPIO_ANALOG_ENABLED);
	// Analog PinMux for B2
	GPIO_setPinConfig(GPIO_235_GPIO235);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(235, GPIO_ANALOG_ENABLED);
	// Analog PinMux for B3
	GPIO_setPinConfig(GPIO_236_GPIO236);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(236, GPIO_ANALOG_ENABLED);
	// Analog PinMux for B4, GPIO215
	GPIO_setPinConfig(GPIO_215_GPIO215);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(215, GPIO_ANALOG_ENABLED);
	// Analog PinMux for B5, GPIO216
	GPIO_setPinConfig(GPIO_216_GPIO216);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(216, GPIO_ANALOG_ENABLED);
	// Analog PinMux for B6, GPIO207
	GPIO_setPinConfig(GPIO_207_GPIO207);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(207, GPIO_ANALOG_ENABLED);
	// Analog PinMux for B7, GPIO208
	GPIO_setPinConfig(GPIO_208_GPIO208);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(208, GPIO_ANALOG_ENABLED);
	// Analog PinMux for B8, GPIO217
	GPIO_setPinConfig(GPIO_217_GPIO217);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(217, GPIO_ANALOG_ENABLED);
	// Analog PinMux for B9, GPIO218
	GPIO_setPinConfig(GPIO_218_GPIO218);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(218, GPIO_ANALOG_ENABLED);
	// Analog PinMux for C0, GPIO199
	GPIO_setPinConfig(GPIO_199_GPIO199);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(199, GPIO_ANALOG_ENABLED);
	// Analog PinMux for C10
	GPIO_setPinConfig(GPIO_241_GPIO241);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(241, GPIO_ANALOG_ENABLED);
	// Analog PinMux for C11
	GPIO_setPinConfig(GPIO_242_GPIO242);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(242, GPIO_ANALOG_ENABLED);
	// Analog PinMux for C13
	GPIO_setPinConfig(GPIO_239_GPIO239);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(239, GPIO_ANALOG_ENABLED);
	// Analog PinMux for C2
	GPIO_setPinConfig(GPIO_237_GPIO237);
	// AIO -> Analog mode selected
	GPIO_setAnalogMode(237, GPIO_ANALOG_ENABLED);
	// Analog PinMux for C3, GPIO206
	GPIO_setPinConfig(GPIO_206_GPIO206);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(206, GPIO_ANALOG_ENABLED);
	// Analog PinMux for C4, GPIO205
	GPIO_setPinConfig(GPIO_205_GPIO205);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(205, GPIO_ANALOG_ENABLED);
	// Analog PinMux for C5, GPIO204
	GPIO_setPinConfig(GPIO_204_GPIO204);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(204, GPIO_ANALOG_ENABLED);
	// Analog PinMux for C6, GPIO203
	GPIO_setPinConfig(GPIO_203_GPIO203);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(203, GPIO_ANALOG_ENABLED);
	// Analog PinMux for C1, GPIO200
	GPIO_setPinConfig(GPIO_200_GPIO200);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(200, GPIO_ANALOG_ENABLED);
	// Analog PinMux for C7, GPIO198
	GPIO_setPinConfig(GPIO_198_GPIO198);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(198, GPIO_ANALOG_ENABLED);
	// Analog PinMux for C8, GPIO202
	GPIO_setPinConfig(GPIO_202_GPIO202);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(202, GPIO_ANALOG_ENABLED);
	// Analog PinMux for C9, GPIO201
	GPIO_setPinConfig(GPIO_201_GPIO201);
	// AGPIO -> Analog mode selected
	GPIO_setAnalogMode(201, GPIO_ANALOG_ENABLED);

    //
	// asysctl initialization
	//
	// Disables the temperature sensor output to the ADC.
	//
	ASysCtl_disableTemperatureSensor();
	//
	// Set the analog voltage reference selection to internal.
	//
	ASysCtl_setAnalogReferenceInternal( ASYSCTL_VREFHIA | ASYSCTL_VREFHIB | ASYSCTL_VREFHIC );
	//
	// Set the internal analog voltage reference selection to 1.65V.
	//
	ASysCtl_setAnalogReference1P65( ASYSCTL_VREFHIA | ASYSCTL_VREFHIB | ASYSCTL_VREFHIC );
}

void initPins(void)
{
    // PWM pins
    GPIO_setPinConfig(GPIO_8_EPWM5_A);
    GPIO_setDirectionMode(8, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(8, GPIO_PIN_TYPE_STD);
    
    GPIO_setPinConfig(GPIO_9_EPWM5_B);
    GPIO_setDirectionMode(9, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(9, GPIO_PIN_TYPE_STD);
    
    GPIO_setPinConfig(GPIO_6_EPWM4_A);
    GPIO_setDirectionMode(6, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(6, GPIO_PIN_TYPE_STD);
    
    GPIO_setPinConfig(GPIO_7_EPWM4_B);
    GPIO_setDirectionMode(7, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(7, GPIO_PIN_TYPE_STD);
    
    GPIO_setPinConfig(GPIO_10_EPWM6_A);
    GPIO_setDirectionMode(10, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(10, GPIO_PIN_TYPE_STD);
    
    GPIO_setPinConfig(GPIO_11_EPWM6_B);
    GPIO_setDirectionMode(11, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(11, GPIO_PIN_TYPE_STD);

    // Contactor pin
    GPIO_setPinConfig(GPIO_75_GPIO75);
    GPIO_setPadConfig(75, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(75, GPIO_DIR_MODE_OUT);
    GPIO_writePin(75, 0);

    // Timing pin
	GPIO_setPinConfig(GPIO_82_GPIO82);
	GPIO_setPadConfig(82, GPIO_PIN_TYPE_STD);
	GPIO_setQualificationMode(82, GPIO_QUAL_SYNC);
	GPIO_setDirectionMode(82, GPIO_DIR_MODE_OUT);
	GPIO_setControllerCore(82, GPIO_CORE_CPU1);

    // SCI pins
    GPIO_setPinConfig(GPIO_43_SCIA_RX);
	GPIO_setPadConfig(43, GPIO_PIN_TYPE_STD | GPIO_PIN_TYPE_PULLUP);
	GPIO_setQualificationMode(43, GPIO_QUAL_ASYNC);

	GPIO_setPinConfig(GPIO_42_SCIA_TX);
	GPIO_setPadConfig(42, GPIO_PIN_TYPE_STD | GPIO_PIN_TYPE_PULLUP);
	GPIO_setQualificationMode(42, GPIO_QUAL_ASYNC);
}

#define ADC_ACQ_TIME 50U
void initADC(void)
{
    EPWM_setADCTriggerSource(EPWM5_BASE, EPWM_SOC_A, EPWM_SOC_TBCTR_PERIOD);
    EPWM_setADCTriggerEventPrescale(EPWM5_BASE, EPWM_SOC_A, 1);
    EPWM_enableADCTrigger(EPWM5_BASE, EPWM_SOC_A);

    DEVICE_DELAY_US(1000);

    ADC_setPrescaler(ADCB_BASE, ADC_CLK_DIV_4_0);
    ADC_setPrescaler(ADCC_BASE, ADC_CLK_DIV_4_0);

    ADC_setMode(ADCB_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
    ADC_setMode(ADCC_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);

    ADC_setVREF(ADCB_BASE, ADC_REFERENCE_EXTERNAL, ADC_REFERENCE_VREFHI);
    ADC_setVREF(ADCC_BASE, ADC_REFERENCE_EXTERNAL, ADC_REFERENCE_VREFHI);

    ADC_enableConverter(ADCB_BASE);
    ADC_enableConverter(ADCC_BASE);
    
    DEVICE_DELAY_US(1000);

    ADC_setupSOC(ADCB_BASE, ADC_SOC_NUMBER0, ADC_TRIGGER_EPWM5_SOCA, ADC_CH_ADCIN4,  ADC_ACQ_TIME);
    ADC_setupSOC(ADCB_BASE, ADC_SOC_NUMBER1, ADC_TRIGGER_EPWM5_SOCA, ADC_CH_ADCIN7,  ADC_ACQ_TIME);
    ADC_setupSOC(ADCB_BASE, ADC_SOC_NUMBER2, ADC_TRIGGER_EPWM5_SOCA, ADC_CH_ADCIN6,  ADC_ACQ_TIME);
    ADC_setupSOC(ADCB_BASE, ADC_SOC_NUMBER3, ADC_TRIGGER_EPWM5_SOCA, ADC_CH_ADCIN14, ADC_ACQ_TIME);

    ADC_setupSOC(ADCC_BASE, ADC_SOC_NUMBER0, ADC_TRIGGER_EPWM5_SOCA, ADC_CH_ADCIN4,  ADC_ACQ_TIME);
    ADC_setupSOC(ADCC_BASE, ADC_SOC_NUMBER1, ADC_TRIGGER_EPWM5_SOCA, ADC_CH_ADCIN7,  ADC_ACQ_TIME);
    ADC_setupSOC(ADCC_BASE, ADC_SOC_NUMBER2, ADC_TRIGGER_EPWM5_SOCA, ADC_CH_ADCIN6,  ADC_ACQ_TIME);
    ADC_setupSOC(ADCC_BASE, ADC_SOC_NUMBER3, ADC_TRIGGER_EPWM5_SOCA, ADC_CH_ADCIN15, ADC_ACQ_TIME);

	ADC_enableAltDMATiming(ADCB_BASE);
	ADC_disableBurstMode(ADCB_BASE);
	ADC_setSOCPriority(ADCB_BASE, ADC_PRI_ALL_ROUND_ROBIN);
	ADC_setInterruptSOCTrigger(ADCB_BASE, ADC_SOC_NUMBER0, ADC_INT_SOC_TRIGGER_NONE);
	ADC_enableAltDMATiming(ADCC_BASE);
	ADC_disableBurstMode(ADCC_BASE);
	ADC_setSOCPriority(ADCC_BASE, ADC_PRI_ALL_ROUND_ROBIN);
	ADC_setInterruptSOCTrigger(ADCC_BASE, ADC_SOC_NUMBER0, ADC_INT_SOC_TRIGGER_NONE);

    // set up adc interrupt
    ADC_setInterruptPulseMode(ADCB_BASE, ADC_PULSE_END_OF_CONV);
    ADC_setInterruptSource(ADCB_BASE, ADC_INT_NUMBER1, ADC_SOC_NUMBER3);
    ADC_clearInterruptStatus(ADCB_BASE, ADC_INT_NUMBER1);
	ADC_disableContinuousMode(ADCB_BASE, ADC_INT_NUMBER1);
	ADC_enableInterrupt(ADCB_BASE, ADC_INT_NUMBER1);

    // register the interrupt handler
    Interrupt_register(INT_ADCB1, &adcBISR);
    Interrupt_enable(INT_ADCB1);
}

#define DAC_VDD_HALF       2048
void initDAC(uint32_t base)
{
    DAC_setReferenceVoltage(base, DAC_REF_ADC_VREFHI);
    DAC_setGainMode(base, DAC_GAIN_ONE);
    DAC_setLoadMode(base, DAC_LOAD_SYSCLK);
    DAC_setShadowValue(base, DAC_VDD_HALF);
    DAC_enableOutput(base);
}

void initInverterPWM(uint32_t base)
{
    EPWM_setCounterCompareValue(base, EPWM_COUNTER_COMPARE_A, 1249);
    EPWM_setTimeBasePeriod(base, INVERTER_PWM_PERIOD);
    EPWM_setClockPrescaler(base, EPWM_CLOCK_DIVIDER_1, EPWM_HSCLOCK_DIVIDER_1);
    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_LOW, EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);
    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_HIGH, EPWM_AQ_OUTPUT_ON_TIMEBASE_DOWN_CMPA);
    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_B, EPWM_AQ_OUTPUT_HIGH, EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);
    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_B, EPWM_AQ_OUTPUT_LOW, EPWM_AQ_OUTPUT_ON_TIMEBASE_DOWN_CMPA);
    EPWM_setRisingEdgeDelayCount(base, DEADTIME_CYCLES);
    EPWM_setFallingEdgeDelayCount(base, DEADTIME_CYCLES);
    EPWM_setDeadBandDelayMode(base, EPWM_DB_RED, true);
    EPWM_setDeadBandDelayMode(base, EPWM_DB_FED, true);
    EPWM_setFallingEdgeDeadBandDelayInput(base, EPWM_DB_INPUT_EPWMA);
    EPWM_setDeadBandDelayPolarity(base, EPWM_DB_RED, EPWM_DB_POLARITY_ACTIVE_HIGH);
    EPWM_setDeadBandDelayPolarity(base, EPWM_DB_FED, EPWM_DB_POLARITY_ACTIVE_LOW);
    EPWM_setTimeBaseCounterMode(base, EPWM_COUNTER_MODE_UP_DOWN);
}

void initInverterPWMSync(void)
{
    //
    // EPWM5 is the phase sync master: emit a sync-out pulse every time its
    // counter crosses zero, so EPWM4/EPWM6 (phases B/C) can lock their
    // triangle carriers to the same zero-crossing instead of free-running
    // with whatever arbitrary startup phase offset they'd otherwise have.
    //
    EPWM_enableSyncOutPulseSource(EPWM5_BASE, EPWM_SYNC_OUT_PULSE_ON_CNTR_ZERO);

    //
    // Lock this carrier's phase to EPWM5's (the sync master) so all 3 phases
    // share the same zero-crossing instead of an arbitrary startup offset.
    // Re-synced every EPWM5 zero-crossing, so any drift self-corrects.
    //
    EPWM_setSyncInPulseSource(EPWM4_BASE, EPWM_SYNC_IN_PULSE_SRC_SYNCOUT_EPWM5);
    EPWM_setPhaseShift(EPWM4_BASE, 0);
    EPWM_setCountModeAfterSync(EPWM4_BASE, EPWM_COUNT_MODE_UP_AFTER_SYNC);
    EPWM_enablePhaseShiftLoad(EPWM4_BASE);

    //
    // Lock this carrier's phase to EPWM5's (the sync master) so all 3 phases
    // share the same zero-crossing instead of an arbitrary startup offset.
    // Re-synced every EPWM5 zero-crossing, so any drift self-corrects.
    //
    EPWM_setSyncInPulseSource(EPWM6_BASE, EPWM_SYNC_IN_PULSE_SRC_SYNCOUT_EPWM5);
    EPWM_setPhaseShift(EPWM6_BASE, 0);
    EPWM_setCountModeAfterSync(EPWM6_BASE, EPWM_COUNT_MODE_UP_AFTER_SYNC);
    EPWM_enablePhaseShiftLoad(EPWM6_BASE);
}

void initSCI(){
	SCI_clearInterruptStatus(SCIA_BASE, SCI_INT_RXFF | SCI_INT_TXFF | SCI_INT_FE | SCI_INT_OE | SCI_INT_PE | SCI_INT_RXERR | SCI_INT_RXRDY_BRKDT | SCI_INT_TXRDY);
	SCI_clearOverflowStatus(SCIA_BASE);
	SCI_resetTxFIFO(SCIA_BASE);
	SCI_resetRxFIFO(SCIA_BASE);
	SCI_resetChannels(SCIA_BASE);
	SCI_setConfig(SCIA_BASE, DEVICE_LSPCLK_FREQ, 115200, (SCI_CONFIG_WLEN_8|SCI_CONFIG_STOP_ONE|SCI_CONFIG_PAR_NONE));
	SCI_disableLoopback(SCIA_BASE);
	SCI_performSoftwareReset(SCIA_BASE);
	SCI_enableFIFO(SCIA_BASE);
	SCI_enableModule(SCIA_BASE);
}

void buildPeripherals(void) {
    initPinMux();
    initPins();
    
    // EPWM init
    initInverterPWM(EPWM5_BASE);
    initInverterPWM(EPWM4_BASE);
    initInverterPWM(EPWM6_BASE);
    initInverterPWMSync();

    // ADC init
    initADC();

    // SCI init
    initSCI();

    // DAC init
    initDAC(DACA_BASE);
    initDAC(DACC_BASE);
}

