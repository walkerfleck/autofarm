
/*
 *  Routines to access hardware
 *
 *  Copyright (c) 2013 Realtek Semiconductor Corp.
 *
 *  This module is a confidential and proprietary property of RealTek and
 *  possession or use of this module requires written permission of RealTek.
 */
#include "FreeRTOS.h"
#include "task.h"
#include "diag.h"
#include "device.h"
#include "timer_api.h"
#include "main.h"
#include <lwip_netconf.h>
#include "wifi_constants.h"
#include "wifi_structures.h"
#include "lwip_netconf.h"
#include <httpc/httpc.h>
#include "LiquidCrystal_I2C.h"
#include <queue.h>
#include "at_cmd/log_service.h"
#include "apdef.h"
#include <ota_8195a.h>
#include "wdt_api.h"


#ifdef __PARKEY_VER_2__
//========================================================================================================
extern const char dev_version[];
//========================================================================================================
extern sys_profile_t sys_profile;    // from net_util.c
//========================================================================================================
extern QueueHandle_t pMotorCmdQueue;     // motor command queue, created by main.c
extern QueueHandle_t pCloudCmdQueue;     // command issued by cloud server, created by main.c
extern QueueHandle_t pSystemQueue;       // system messqge queue, created by main.c

//========================================================================================================
extern gpio_t gpio_user_button;
extern gpio_t gpio_p_sensor;
extern gpio_t gpio_v_sensor;

//========================================================================================================
extern void console_init(void);
extern void net_main(void);         // net_util.c
extern uint8_t identify_url(int *url_start_pos, char *url); // net_util.c
extern unsigned char enable_detection_emulation;    // motor_control.c
//========================================================================================================
extern struct sFlags sysFlags;
//gtimer_t global_timer;
//volatile uint32_t global_tick=0;
//========================================================================================================
extern uint8_t global_stop;
//========================================================================================================
//void global_timer_timeout_handler(uint32_t id) {
//    global_tick++;
//}
extern volatile uint8_t dev_state, dev_err_counter, detection_counter;
extern volatile u32 state_time, AD_time, detection_time;
volatile u32 polling_time;
//========================================================================================================
extern TaskHandle_t hReqThread, hSysThread, hCmdThread;
//========================================================================================================
void ccmd_ota(void *arg);
//========================================================================================================
void apSendMsg(QueueHandle_t q, unsigned char id, unsigned char param);
//========================================================================================================
uint16_t GetADValue();  // from main.c
//========================================================================================================
int8_t SendQueryDevice(uint8_t devid, uint8_t *data);   // from main.c
//========================================================================================================
//uint8_t __attribute__((section(".ram_image2.text"))) state_machine(unsigned char idMsg, unsigned char ucParam, void *data)  {
uint8_t state_machine(unsigned char idMsg, unsigned char ucParam, void *data)  {
    //struct tMessage msg;
    unsigned char last_dev_state = dev_state;  // save last state id, if changed, reset state_time
    /*
    switch(dev_state) {
        case 0:
            // P active, V inactive
            if (gpio_read(&gpio_p_sensor)==0 && gpio_read(&gpio_v_sensor)!=0) {
                dev_state=1;
            }
            // v active持續5分鐘(每秒偵測)
            else if (rtw_get_passing_time_ms(detection_time)>1000) {
                detection_time=rtw_get_current_time();
                if (gpio_read(&gpio_v_sensor)==0) {
                    detection_counter++;
                    if (detection_counter>=5*60) {
                        apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_ERROR, ERROR_OBJECT_DETECTION_ERROR);
                        detection_counter=0;
                        dev_state=0;
                    }
                }
                else {
                    detection_counter=0;
                }
            }
            break;
        case 1:
            // V active 
            if (gpio_read(&gpio_v_sensor)==0) {
                dev_state=2;
            }
            // P inactive
            else if (gpio_read(&gpio_v_sensor)!=0) {
                dev_state=3;
            }
            break;
        case 2:
            // v inactive
            if (gpio_read(&gpio_v_sensor)!=0) {
                dev_state=1;
            }
            //  持續 10秒
            else if (rtw_get_passing_time_ms(state_time)>10*1000) {
                // 傳送BikeStatus 有車 request至cloud
                apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_BIKESTATUS, 2);
                apSendMsg(pMotorCmdQueue, MSG_GPIO_LOCK, 0);
                vTaskDelay(2000);
                apSendMsg(pMotorCmdQueue, MSG_GPIO_INDICATOR_ON, 0);
                dev_state=100;
            }
            break;
        case 3:
            // v active
            if (gpio_read(&gpio_v_sensor)==0) {
                dev_state=2;
            }
            // 3分鐘timeout
            else if (rtw_get_passing_time_ms(state_time)>3*60*1000) {
                state_time = rtw_get_current_time();
                apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_ERROR, ERROR_OBJECT_DETECTION_ERROR);
                dev_state=3;
            }
            break;
        case 10:
            // v active
            if (gpio_read(&gpio_v_sensor)==0) {
                rtl_printf("************RESET Lock********************");
                // 傳送BikeStatus 有車 request至cloud
                apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_BIKESTATUS, 5);
                apSendMsg(pMotorCmdQueue, MSG_GPIO_LOCK, 0);
                vTaskDelay(2000);
                apSendMsg(pMotorCmdQueue, MSG_GPIO_INDICATOR_ON, 0);
                dev_state=100;
            }
            // v inactive
            else if (gpio_read(&gpio_v_sensor)!=0) {
                rtl_printf("************RESET UnLock********************");
                apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_BIKESTATUS, 6);
                apSendMsg(pMotorCmdQueue, MSG_GPIO_UNLOCK, 0);
                vTaskDelay(2000);
                apSendMsg(pMotorCmdQueue, MSG_GPIO_INDICATOR_OFF, 0);
                dev_state=0;
            }
            break;
        case 100:
            if (idMsg==MSG_EVENT_TRANSACTION_OK_OPEN) {
                apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_BIKESTATUS, 3);
                apSendMsg(pMotorCmdQueue, MSG_GPIO_UNLOCK, 0);
                vTaskDelay(2000);
                apSendMsg(pMotorCmdQueue, MSG_GPIO_INDICATOR_OFF, 0);
                dev_state=101;
            }
            // v inactive
            else if (gpio_read(&gpio_v_sensor)!=0) {
                apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_ERROR, ERROR_BIKEOUT_ERROR);
                apSendMsg(pMotorCmdQueue, MSG_GPIO_UNLOCK, 0);
                vTaskDelay(2000);
                apSendMsg(pMotorCmdQueue, MSG_GPIO_INDICATOR_OFF, 0);
                dev_state=0;
            }
            // user button clicked, just for demostration
            else if (gpio_read(&gpio_user_button)==0) {
                apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_BIKESTATUS, 3);
                apSendMsg(pMotorCmdQueue, MSG_GPIO_UNLOCK, 0);
                vTaskDelay(2000);
                apSendMsg(pMotorCmdQueue, MSG_GPIO_INDICATOR_OFF, 0);
                dev_state=101;
            }
            break;
        case 101:
            // v inactive
            if (gpio_read(&gpio_v_sensor)!=0) {
                dev_state=102;
            }
            //  持續 3分鐘timeout
            else if (rtw_get_passing_time_ms(state_time)>3*60*1000) {
                // 傳送BikeStatus 有車 request至cloud
                apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_BIKESTATUS, 2);
                apSendMsg(pMotorCmdQueue, MSG_GPIO_LOCK, 0);
                vTaskDelay(2000);
                apSendMsg(pMotorCmdQueue, MSG_GPIO_INDICATOR_ON, 0);
                dev_state=100;
            }
            break;
        case 102:
            // v active
            if (gpio_read(&gpio_v_sensor)==0) {
                dev_state=101;
            }
            // p active
            else if (gpio_read(&gpio_p_sensor)==0) {
                dev_state=103;
            }
            // 3分鐘timeout
            else if (rtw_get_passing_time_ms(state_time)>3*60*1000) {
                state_time = rtw_get_current_time();
                apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_ERROR, ERROR_BIKEOUT_ERROR);
                dev_state=102;
            }
            break;
        case 103:
            // v active
            if (gpio_read(&gpio_v_sensor)==0) {
                dev_state=101;
            }
            // p inactive
            else if (gpio_read(&gpio_p_sensor)!=0) {
                dev_state=104;
            }
            // 3分鐘timeout
            else if (rtw_get_passing_time_ms(state_time)>3*60*1000) {
                state_time = rtw_get_current_time();
                apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_ERROR, ERROR_BIKEOUT_ERROR);
                dev_state=103;
            }
            break;
        case 104:
            // v active
            if (gpio_read(&gpio_v_sensor)==0) {
                dev_state=101;
            }
            // p active
            else if (gpio_read(&gpio_p_sensor)==0) {
                dev_state=1;
            }
            // 2秒
            else if (rtw_get_passing_time_ms(state_time)>2*1000) {
                state_time = rtw_get_current_time();
                dev_state=0;
            }
            break;
        default:
            break;
    }
*/

    if (last_dev_state!=dev_state) {
        state_time = rtw_get_current_time();
        detection_counter=0;
    }
}
//========================================================================================================
// fmt :    0 yyyymmddhhmmssZZZ
//          1 yyyy/mm/dd hh:mm:ss.ZZZ
char *GetSystemTimestamp(char *buf, int fmt);
//========================================================================================================
extern gpio_t gpio_jc1278_set;
//========================================================================================================
//void __attribute__((section(".ram_image2.text"))) state_thread(void* in_id) {
void state_thread(void* in_id) {
    struct tMessage sMsg;
    char tbuf[30];

    rtl_printf("State Machine Initializing\n");
    polling_time = rtw_get_current_time();

    // reading message queue
    // react state according to messge
    // generate response
    while (global_stop==0) {
        // message income, move the state to next state
        if( xQueueReceive( pSystemQueue, &( sMsg ), ( TickType_t ) 1 ) == pdPASS )
        {
            rtl_printf("System Messge Received : %x\r\n", sMsg.ucMsg);
            switch (sMsg.ucMsg) {
                /*
                case MSG_EVENT_TRANSACTION_OK_OPEN:
                    state_machine(sMsg.ucMsg, sMsg.ucParam, NULL);
                    break;
                case MSG_EVENT_SHOW_TIME:
                    rtl_printf("System Tick : %s\r\n", GetSystemTimestamp(tbuf, 1));
                    break;

                case MSG_EVENT_SYSTEM_ERROR:
                    break;

                case MSG_EVENT_CHANGE_STATE:
                    // change device state
                    dev_state = sMsg.ucParam;
                    // reset the error counter and state timer
                    dev_err_counter=0;
                    state_time = rtw_get_current_time();
                    rtl_printf("\n\rState Changed to %d\n\r", dev_state);
                    break;
*/
                case MSG_EVENT_REBOOT:
                    rtl_printf("\n\rReboot the system\n\r");
                    sys_reset();
                    break;
/*                    
                case MSG_EVENT_UPGRADE_FIRMWARE:
                    rtl_printf("\n\rOTA Firmware\n\r");
                    sprintf(tbuf, "%d", sMsg.ucParam);
                    ccmd_ota(tbuf);
*/                    break;

                default:
                    // message not recognized, feed last state to state machine
                    state_machine(0, 0, NULL);
                    break;
            }
        }
        else {
            // no message input, feed last state to state machine, messge 0 is dummy message
            state_machine(0, 0, NULL);
        }
        vTaskDelay(50);
        //watchdog_refresh();
        //
        // 每隔一段時間polling一次所有device
        if (rtw_get_passing_time_ms(polling_time)>10*1000) {
            polling_time = rtw_get_current_time();
            int i;
            // 搜尋dev庫, 找到devid不連續的號碼, 或者是最後一號加1
            
            for(i=0;i<MAX_DEVICE_NUM;i++) {
                if (sys_profile.dev[i].devid!=0) {
                    uint8_t data[10];
                    // send query, return 1 byte data
                    if (SendQueryDevice(sys_profile.dev[i].devid, data)==0) {
                        // 傳送成功, MQTT回報device state
                    }
                }
            }
            
            // polling each device by SendQueryDevice(),
            // send back the state to cloud
            //
        }
    }
    
	vTaskDelete(NULL);
}

#endif


