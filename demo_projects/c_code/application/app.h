#ifndef APP_H
    #define APP_H

    #include "parameter_definitions.h"
    #include "device.h"

    void app_task_init(void);
    void app_task_isr(void);
    void app_task_background_loop(void);
    
#endif  // APP_H
