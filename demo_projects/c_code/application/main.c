#include "driverlib.h"
#include "device.h"
#include "app.h"
#include "peripherals/peripherals.h"

//
// Main
//
void main(void)
{
    //
    // Initialize device clock and peripherals
    //
    Device_init();

    //
    // Disable pin locks and enable internal pullups.
    //
    Device_initGPIO();

    //
    // Initialize PIE and clear PIE registers. Disables CPU interrupts.
    //
    Interrupt_initModule();

    //
    // Initialize the PIE vector table with pointers to the shell Interrupt
    // Service Routines (ISR).
    //
    Interrupt_initVectorTable();

    buildPeripherals();

    //
    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    //
    EINT;
    ERTM;


    // initialize the app
    app_task_init();

    // PWM runs continuously, ADC samples automatically
    // Application processing happens in ISR by calling app_task_isr() from app.c

    // Main loop - No software intervention in sampling process
    while(1)
    {
        app_task_background_loop();
    }
}

__interrupt void adcBISR(void)
{
    //
    // Read the ADC results and process them immediately
    // All 7 channels (A4, B4, C4, A10, B7, C7, C6) are now available
    //
    app_task_isr();

    //
    // Clear the interrupt flag
    //
    ADC_clearInterruptStatus(ADCB_BASE, ADC_INT_NUMBER1);

    //
    // Acknowledge the interrupt
    //
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

