/*
 *  Routines to access motor / button / mechanical sensors
 *
 *  Copyright (c) 2022 Smartway Solutions Inc.
 *
 */

#include "FreeRTOS.h"
#include "task.h"
#include "diag.h"
#include "device.h"
#include "timer_api.h"
#include "gpio_api.h"   // mbed
//#include "pwmout_api.h"   // mbed
#include <queue.h>
#include "main.h"
#include "apdef.h"
#include "semphr.h"

//========================================================================================================
gpio_t gpio_user_button;
gpio_t gpio_jc1278_set;
gpio_t gpio_test_set;
//========================================================================================================

//#define GPIO_PWM_INDICATOR        PC_1          // [O]
//#define GPIO_PWM_LOCK             PC_2          // [O]
//#define GPIO_P_SENSOR             PB_4          // [I] 
//#define GPIO_V_SENSOR             PB_5          // [I]
#define GPIO_JC1278_SET             PB_4          // [O]
#define GPIO_USER_BUTTON            PA_2          // [I]
#define GPIO_TEST                   PB_5          // [I]
//#define GPIO_RADOR_DETECT         PA_3
//#define GPIO_SOLENOID_LOCK        PA_5          // [O] lock the gate when closed

// 不知道為什麼, compiler會把前幾行放至gpio_motor_init_2()裡面, 所以在這裡留空白行
// 這是個嚴重的問題, 不知為何之前沒發現















//========================================================================================================
void gpio_motor_init_2() {

    rtl_printf("\n\rGPIO init... 1\n\r");
    // ----------------------------------------------------
    // Init GPIO control pin


    rtl_printf("\n\rGPIO init... 3\n\r");
    gpio_init(&gpio_user_button, GPIO_USER_BUTTON);
    gpio_dir(&gpio_user_button, PIN_INPUT);    // Direction: Output
    gpio_mode(&gpio_user_button, PullUp);     // pull up
    gpio_write(&gpio_user_button, ~0);

    gpio_init(&gpio_jc1278_set, GPIO_JC1278_SET);
    gpio_dir(&gpio_jc1278_set, PIN_OUTPUT);    // Direction: Output
    gpio_mode(&gpio_jc1278_set, PullDown);     // pull up

    gpio_init(&gpio_test_set, GPIO_TEST);
    gpio_dir(&gpio_test_set, PIN_OUTPUT);    // Direction: Output
    gpio_mode(&gpio_test_set, PullDown);     // pull up

    rtl_printf("\n\rGPIO init... End\n\r");
    gpio_write(&gpio_jc1278_set, 0);
    gpio_write(&gpio_test_set, 0);

    /*
    xMutexGlobal = xSemaphoreCreateMutex();
*/

    //gtimer_init(&timer_global, TIMER3);
    //gtimer_start_periodical(&timer_global, 1000, (void*)global_timer_timeout_handler, &lstPWM);

    //if(xTaskCreate(motor_thread, ((const char*)"motor"), 256, NULL, tskIDLE_PRIORITY + 3 + PRIORITIE_OFFSET, NULL) != pdPASS)
    //    rtl_printf("\n\r%s xTaskCreate(motor_thread) failed", __FUNCTION__);

    //rtl_printf("\n\rMonitoring input/output...\n\r");

}



