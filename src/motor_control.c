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
extern QueueHandle_t pSystemQueue;       // sensor detection queue, created by main.c
extern QueueHandle_t pMotorCmdQueue;     // motor command queue, created by main.c
//========================================================================================================
gpio_t gpio_pwm_indicator;
gpio_t gpio_pwm_lock;
gpio_t gpio_user_button;
gpio_t gpio_p_sensor;
gpio_t gpio_v_sensor;
//========================================================================================================
// *******************************************************************************************************
// *
// *    PARKEY 3 I/O Controller
// *
// *
// *******************************************************************************************************
#ifdef __PARKEY_VER_2__

//========================================================================================================
SemaphoreHandle_t xMutexPWM;
//========================================================================================================
// Emulate PWM generator
gtimer_t timer_pwm;
//========================================================================================================
struct sPWM {
    gpio_t *handle;
    uint32_t period;
    uint32_t period_counter;
    uint32_t width;
};
struct sPWM lstPWM[5];

//========================================================================================================
void SetPWM(gpio_t *h, uint32_t period, uint32_t width) {
    int i;
    xSemaphoreTake(xMutexPWM, portMAX_DELAY);
    // 重複的gpio就蓋過去
    for(i=0;i<4;i++) {
        if (lstPWM[i].handle==h)
            break;
    }
    if (i>=4) {
        for(i=0;i<4;i++) {
            if (lstPWM[i].handle==NULL || lstPWM[i].handle==h)
                break;
        }
    }
    lstPWM[i].period = period/100;
    lstPWM[i].width = width/100;
    lstPWM[i].handle  = h;
    lstPWM[i].period_counter  = 0;
    gpio_write(lstPWM[i].handle, ~0);
    xSemaphoreGive(xMutexPWM);
}
//========================================================================================================
void pwm_timer_timeout_handler(uint32_t id) {
    int i;
    //gpio_write(&gpio_user_button, !gpio_read(&gpio_user_button));
    xSemaphoreTakeFromISR(xMutexPWM, pdTRUE);
    //            gpio_write(lstPWM[0].handle, !gpio_read(lstPWM[0].handle));
    //            gpio_write(lstPWM[1].handle, !gpio_read(lstPWM[1].handle));
    for(i=0;i<4;i++) {
        if (lstPWM[i].handle!=NULL) {
            lstPWM[i].period_counter++;
            // period到了反轉
            if (lstPWM[i].period_counter==lstPWM[i].period) {
                gpio_write(lstPWM[i].handle, !gpio_read(lstPWM[i].handle));
                lstPWM[i].period_counter=0;
            }
            if (lstPWM[i].period_counter==lstPWM[i].width) {
                gpio_write(lstPWM[i].handle, !gpio_read(lstPWM[i].handle));
            }
        }
        else
            break;
    }
    xSemaphoreGiveFromISR(xMutexPWM, pdTRUE);
}
//========================================================================================================
//GPIO definitions

#define GPIO_PWM_INDICATOR        PC_1          // [O]
#define GPIO_PWM_LOCK             PC_2          // [O]
#define GPIO_P_SENSOR             PB_4          // [I] 
#define GPIO_V_SENSOR             PB_5          // [I]
#define GPIO_USER_BUTTON          PA_2          // [I]
//#define GPIO_RADOR_DETECT         PA_3
//#define GPIO_SOLENOID_LOCK        PA_5          // [O] lock the gate when closed



//========================================================================================================
// time tick start from 0 in ms, U32 can store 49 days
// we need to force reboot for a few days to ensure the tick work in normal state
//
//u32 last_down_time_od=0;  // store the last down time
//u32 last_down_time_cd=0;
//u32 last_down_time_ub=0;
//u32 last_down_time_rd=0;
//u32 solenoid_time=0;


//pwmout_t pwm_indicator;
//pwmout_t pwm_lock;

// input states
//uint8_t od_state=0;
//uint8_t cd_state=0;
//uint8_t ub_state=0;
//uint8_t rd_state=0;
//========================================================================================================
unsigned char enable_detection_emulation=0, last_gpio_cmd=0;
//========================================================================================================
extern uint8_t global_stop;             // from main.c
//========================================================================================================
//========================================================================================================
void Input_Debunce(gpio_t *gpio, u32 *last_down_time, uint8_t *state, uint8_t msg_input_id) {
    //struct tMessage msg;
    if (gpio_read(gpio)==0) {
        // debounce
        if (*last_down_time==0)
            *last_down_time = rtw_get_current_time();
        if (*state==0 && rtw_get_passing_time_ms(*last_down_time) > 50) {
            *state=1;
            // post message out
            //msg = (struct tMessage) {.ucMsg=msg_input_id, .ucParam=0};
            // send it till success
            rtl_printf("Input_Debunce=%d\r\n", msg_input_id);
            apSendMsg(pSystemQueue, msg_input_id, 0);
        }
    }
    else {
        *last_down_time=0;
        *state=0;
    }
}
//========================================================================================================
void motor_thread(void* in_id) {

/*
    vTaskDelay(4000);

    struct tMessage sMsg;
    u32 last_cmd_time;
    //vTaskDelay(4000);
    //rtl_printf("\n\rCreate Semaphore...\n\r");
    //xMutexPWM = xSemaphoreCreateMutex();
    rtl_printf("\n\rZero structures...\n\r");
    memset(lstPWM, 0, sizeof(lstPWM));



    //rtl_printf("\n\rPWM init...\n\r");
    SetPWM(&gpio_pwm_indicator, 20000, 1500);
    SetPWM(&gpio_pwm_lock, 20000, 500);

    rtl_printf("\n\rPWM timer setup...\n\r");
    // 
    gtimer_init(&timer_pwm, TIMER3);
    gtimer_start(&timer_pwm);
    gtimer_start_periodical(&timer_pwm, 1000, (void*)pwm_timer_timeout_handler, &lstPWM);

    gpio_init(&gpio_p_sensor, GPIO_P_SENSOR);
    gpio_dir(&gpio_p_sensor, PIN_INPUT);    // Direction: Input
    gpio_mode(&gpio_p_sensor, PullUp);     // pull up

    gpio_init(&gpio_v_sensor, GPIO_V_SENSOR);
    gpio_dir(&gpio_v_sensor, PIN_INPUT);    // Direction: Input
    gpio_mode(&gpio_v_sensor, PullUp);     // pull up

    gpio_init(&gpio_user_button, GPIO_USER_BUTTON);
    gpio_dir(&gpio_user_button, PIN_INPUT);    // Direction: Input
    gpio_mode(&gpio_user_button, PullUp);     // pull up
*/
    
    // pwm : 500 -> 0 degree,  2500 -> 180 degree

    //pwmout_init(&pwm_indicator, GPIO_PWM_INDICATOR);
    //pwmout_period_us(&pwm_indicator, 20000);
    //pwmout_pulsewidth_us(&pwm_indicator, 2500);
    //vTaskDelay(1500);

    //pwmout_init(&pwm_lock, GPIO_PWM_LOCK);
    //pwmout_period_us(&pwm_lock, 20000);
    //pwmout_pulsewidth_us(&pwm_lock, 1500);

    //rtl_printf("Servo Motor Initialized...\n");
    //uint64_t last_us = gtimer_read_us(&timer_pwm);
    // get system time :  rtw_get_current_time	(	void 		)
    // get passing time in ms from system time start: 	rtw_get_passing_time_ms	(	u32 	start	)	
    // get time interval between two sys times :   rtw_get_time_interval_ms (u32 start, u32 end)
    while (global_stop==0) {
        //gpio_write(lstPWM[0].handle, !gpio_read(lstPWM[0].handle));
        //gpio_write(lstPWM[1].handle, !gpio_read(lstPWM[1].handle));
        /*
        // user button detection
        Input_Debunce(&gpio_user_button, &last_down_time_ub, &ub_state, MSG_EVENT_USER_BTN);
        // motor open position
        Input_Debunce(&gpio_motor_open_detect, &last_down_time_od, &od_state, MSG_EVENT_MOTOR_OPEN_POS);
        // motor close position
        Input_Debunce(&gpio_motor_close_detect, &last_down_time_cd, &cd_state, MSG_EVENT_MOTOR_CLOSE_POS);
        // radar detection
        Input_Debunce(&gpio_radar_detect, &last_down_time_rd, &rd_state, MSG_EVENT_RADAR_DETECTED);
        */
        /*
        if (step>=2500)
            step=2500;
        pwmout_pulsewidth_us(&pwm1, step);
        step+=5;
        */
            /*
        // message income 
        if( xQueueReceive( pMotorCmdQueue, &( sMsg ), ( TickType_t ) 1 ) == pdPASS ) {
            rtl_printf("Motor Messge Received : %x\r\n", sMsg.ucMsg);
            switch (sMsg.ucMsg) {
                case MSG_GPIO_INDICATOR_ON:
                    pwmout_pulsewidth_us(&pwm_indicator, 500);
                    break;
                case MSG_GPIO_INDICATOR_OFF:
                    pwmout_pulsewidth_us(&pwm_indicator, 2500);
                    break;
                case MSG_GPIO_LOCK:
                    pwmout_pulsewidth_us(&pwm_lock, 500);
                    break;
                case MSG_GPIO_UNLOCK:
                    pwmout_pulsewidth_us(&pwm_lock, 1500);
                    break;
                default:
                    break;
            }
            last_gpio_cmd = sMsg.ucMsg;
        }
            */
        // must add this, or block other thread's execution
        rtw_usleep_os(1);
        // receive command from queue
    }
	/* Kill init thread after all init tasks done */
	vTaskDelete(NULL);

}
//========================================================================================================
void gpio_motor_init() {

    rtl_printf("\n\rGPIO init... 1\n\r");
    // ----------------------------------------------------
    // Init GPIO control pin
    gpio_init(&gpio_pwm_indicator, GPIO_PWM_INDICATOR);
    gpio_dir(&gpio_pwm_indicator, PIN_OUTPUT);    // Direction: Output
    gpio_mode(&gpio_pwm_indicator, PullNone);     // pull up
    gpio_write(&gpio_pwm_indicator, ~0);

    rtl_printf("\n\rGPIO init... 2\n\r");
    gpio_init(&gpio_pwm_lock, GPIO_PWM_LOCK);
    gpio_dir(&gpio_pwm_lock, PIN_OUTPUT);    // Direction: Output
    gpio_mode(&gpio_pwm_lock, PullNone);     // pull up
    gpio_write(&gpio_pwm_lock, ~0);

    rtl_printf("\n\rGPIO init... 3\n\r");
    gpio_init(&gpio_user_button, GPIO_USER_BUTTON);
    gpio_dir(&gpio_user_button, PIN_OUTPUT);    // Direction: Output
    gpio_mode(&gpio_user_button, PullNone);     // pull up
    gpio_write(&gpio_user_button, ~0);

    xMutexPWM = xSemaphoreCreateMutex();

    rtl_printf("\n\rZero structures...\n\r");
    memset(lstPWM, 0, sizeof(lstPWM));


    SetPWM(&gpio_pwm_indicator, 20000, 1500);
    SetPWM(&gpio_pwm_lock, 20000, 500);

    gtimer_init(&timer_pwm, TIMER3);
    gtimer_start_periodical(&timer_pwm, 100, (void*)pwm_timer_timeout_handler, &lstPWM);

    //if(xTaskCreate(motor_thread, ((const char*)"motor"), 256, NULL, tskIDLE_PRIORITY + 3 + PRIORITIE_OFFSET, NULL) != pdPASS)
    //    rtl_printf("\n\r%s xTaskCreate(motor_thread) failed", __FUNCTION__);

    //rtl_printf("\n\rMonitoring input/output...\n\r");

}
#endif
//========================================================================================================
// *******************************************************************************************************
// *
// *    PARKEY 3 I/O Controller
// *
// *
// *******************************************************************************************************
#ifdef __PARKEY_VER_3__

#endif



