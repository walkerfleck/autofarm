
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
#include "gpio_api.h"   // mbed
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


#ifdef __PARKEY_VER_3__
//========================================================================================================
extern const char dev_version[];
//========================================================================================================
extern struct sys_profile_t sys_profile;    // from net_util.c
//========================================================================================================
extern QueueHandle_t pMotorCmdQueue;     // motor command queue, created by main.c
extern QueueHandle_t pCloudReqQueue;     // cloud request queue, created by main.c
extern QueueHandle_t pCloudCmdQueue;     // command issued by cloud server, created by main.c
extern QueueHandle_t pSystemQueue;       // system messqge queue, created by main.c

//========================================================================================================
extern gpio_t gpio_motor_open_detect;
extern gpio_t gpio_motor_close_detect;
//========================================================================================================
extern void console_init(void);
extern void gpio_motor_init();      // motor_control.c
extern void net_main(void);         // net_util.c
extern uint8_t identify_url(int *url_start_pos, char *url); // net_util.c
extern unsigned char enable_detection_emulation;    // motor_control.c
//========================================================================================================
//gtimer_t global_timer;
//volatile uint32_t global_tick=0;
//========================================================================================================
extern uint8_t global_stop;
//========================================================================================================
//void global_timer_timeout_handler(uint32_t id) {
//    global_tick++;
//}
extern volatile uint8_t dev_state, dev_err_counter, retry_close;
extern volatile uint16_t obj_detection_counter;
extern volatile u32 state_time, AD_time;
//========================================================================================================
extern TaskHandle_t hReqThread, hSysThread, hCmdThread;
//========================================================================================================
void ccmd_ota(void *arg);
//========================================================================================================
void apSendMsg(QueueHandle_t q, unsigned char id, unsigned char param);
//========================================================================================================
uint16_t GetADValue();  // from main.c
//========================================================================================================
//uint8_t __attribute__((section(".ram_image2.text"))) state_machine(unsigned char idMsg, unsigned char ucParam, void *data)  {
uint8_t state_machine(unsigned char idMsg, unsigned char ucParam, void *data)  {
    //struct tMessage msg;
    unsigned char last_dev_state = dev_state;  // save last state id, if changed, reset state_time
    switch(dev_state) {
        case 0:
            // AD 電壓>900mv, 物體偵測
            if (GetADValue() > OBJ_EXIST_VALUE) {
                dev_state=1;
            }
            break;
        case 1:
            // 無偵測物體
            if (GetADValue() <= OBJ_EXIST_VALUE) {
                dev_state=0;
                obj_detection_counter=0;
            }
            // 每秒檢查AD, 持續5分鐘物體偵測
            else if (rtw_get_passing_time_ms(AD_time)>1000) {
                // 重設一次AD_time, 因為每秒要檢查AD converter
                AD_time = rtw_get_current_time();
                // AD 電壓>900mv, 且持續5分鐘
                if (GetADValue() > OBJ_EXIST_VALUE) {
                    obj_detection_counter++;
                    if (obj_detection_counter>OBJ_EXIST_TIME) {
                        // 傳送BikeStatus 進車成功 request至cloud
                        apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_BIKESTATUS, 2);

                        // 吸起電磁閥, 等待50ms
                        apSendMsg(pMotorCmdQueue, MSG_GPIO_SOLENOID_UNLOCK, 0);
                        vTaskDelay(50);
                        // 馬達反轉關門
                        apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_BACKWARD, 0);
                        dev_state=2;
                    }
                }
                // 只要有一次物體無偵測就把obj_detection_counter歸0
                else {
                    obj_detection_counter=0;
                }
            }
            break;
        case 2:
            // 到達cd點
            if (idMsg==MSG_EVENT_MOTOR_CLOSE_POS || gpio_read(&gpio_motor_close_detect)==0) {
                // 馬達停止
                apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_STOP, 0);
                retry_close=0;
                dev_state=100;
                dev_err_counter=0;
            }
            // 4秒timeout
            else if (rtw_get_passing_time_ms(state_time)>MOTOR_MAX_MSEC) {
                apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_ERROR, ERROR_PARKIN_GATE_NOT_CLOSED);
                // 馬達停止
                apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_STOP, 0);
                retry_close=0;
                dev_state=100;
            }
            // 每秒檢查AD, 持續5分鐘物體偵測不到
            else if (rtw_get_passing_time_ms(AD_time)>1000) {
                // 重設一次AD_time, 因為每秒要檢查AD converter
                AD_time = rtw_get_current_time();
                // AD 電壓<=900mv, 且持續5分鐘, 物體消失, 
                if (GetADValue() <= OBJ_EXIST_VALUE) {
                    obj_detection_counter++;
                    if (obj_detection_counter>OBJ_EXIST_TIME) {
                        // 傳送BikeStatus 出車 request至cloud
                        apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_BIKESTATUS, 3);
                        // 傳送異常狀態
                        apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_ERROR, ERROR_OBJECT_DETECTION_ERROR);
                        // 吸起電磁閥, 等待50ms
                        apSendMsg(pMotorCmdQueue, MSG_GPIO_SOLENOID_UNLOCK, 0);
                        vTaskDelay(50);
                        // 馬達正轉開門
                        apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_FORWARD, 0);
                        dev_state=101;
                    }
                }
                // 只要有一次物體無偵測就把obj_detection_counter歸0
                else {
                    obj_detection_counter=0;
                }
            }
            break;
        case 100:
            if (idMsg==MSG_EVENT_TRANSACTION_OK_OPEN) {
                // 吸起電磁閥, 等待50ms
                apSendMsg(pMotorCmdQueue, MSG_GPIO_SOLENOID_UNLOCK, 0);
                vTaskDelay(50);
                apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_FORWARD, 0);
                // 傳送BikeStatus request至cloud, 解除車位
                apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_BIKESTATUS, 3);
                dev_err_counter=0;
                dev_state=101;
            }
            // 每秒檢查AD
            else if (rtw_get_passing_time_ms(AD_time)>1000) {
                // 重設一次AD_time, 因為每秒要檢查AD converter
                AD_time = rtw_get_current_time();
                // AD 電壓<=900mv, 且持續5分鐘, 物體消失, 
                if (GetADValue() <= OBJ_EXIST_VALUE) {
                    obj_detection_counter++;
                    if (obj_detection_counter>OBJ_EXIST_TIME) {
                        // 傳送BikeStatus 出車 request至cloud
                        apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_BIKESTATUS, 3);
                        // 傳送異常狀態
                        apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_ERROR, ERROR_OBJECT_DISAPPEARED);
                        // 吸起電磁閥, 等待50ms
                        apSendMsg(pMotorCmdQueue, MSG_GPIO_SOLENOID_UNLOCK, 0);
                        vTaskDelay(50);
                        // 馬達正轉開門
                        apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_FORWARD, 0);
                        dev_err_counter=0;
                        dev_state=101;
                    }
                }
                // 只要有一次物體無偵測就把obj_detection_counter歸0
                else {
                    obj_detection_counter=0;
                    // retry_close小於6, cd點沒偵測到, emulation時就不檢查
                    if (retry_close<6 && gpio_read(&gpio_motor_close_detect)!=0 && enable_detection_emulation==0) {
                        // 吸起電磁閥, 等待50ms
                        apSendMsg(pMotorCmdQueue, MSG_GPIO_SOLENOID_UNLOCK, 0);
                        vTaskDelay(50);
                        // 馬達反轉, 重試關門
                        apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_BACKWARD, 0);
                        retry_close++;
                        dev_state=105;
                    }

                }
            }

            break;
        case 101:
            // 到達od點
            if (idMsg==MSG_EVENT_MOTOR_OPEN_POS || gpio_read(&gpio_motor_open_detect)==0) {
                // 馬達停止
                apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_STOP, 0);
                dev_state=102;
                dev_err_counter=0;
            }
            // 障礙物
            else if (idMsg==MSG_EVENT_GATE_BLOCKED) {
                // 馬達停止, 反轉
                apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_STOP, 0);
                dev_state=103;
            }
            // 4秒timeout
            else if (rtw_get_passing_time_ms(state_time)>MOTOR_MAX_MSEC) {
                // 馬達停止, 反轉
                apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_STOP, 0);
                dev_state=103;
            }
            break;
        case 102:
            // AD 電壓<=900mv, 物體消失 
            if (GetADValue() <= OBJ_EXIST_VALUE) {
                dev_state=0;
            }
            // 每秒檢查AD, 持續5分鐘物體偵測
            else if (rtw_get_passing_time_ms(AD_time)>1000) {
                // 重設一次AD_time, 因為每秒要檢查AD converter
                AD_time = rtw_get_current_time();
                // AD 電壓>900mv, 且持續5分鐘
                if (GetADValue() > OBJ_EXIST_VALUE) {
                    obj_detection_counter++;
                    if (obj_detection_counter>OBJ_EXIST_TIME) {
                        // 傳送BikeStatus 進車成功 request至cloud
                        apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_BIKESTATUS, 2);
                        // 吸起電磁閥, 等待50ms
                        apSendMsg(pMotorCmdQueue, MSG_GPIO_SOLENOID_UNLOCK, 0);
                        vTaskDelay(50);
                        // 馬達反轉關門
                        apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_BACKWARD, 0);
                        dev_state=2;
                    }
                }
                // 只要有一次物體無偵測就把obj_detection_counter歸0
                else {
                    obj_detection_counter=0;
                }
            }
            break;
        case 103:
            // 2秒timeout
            if (rtw_get_passing_time_ms(state_time)>1000) {
                dev_err_counter++;
                // 吸起電磁閥, 等待50ms
                apSendMsg(pMotorCmdQueue, MSG_GPIO_SOLENOID_UNLOCK, 0);
                vTaskDelay(50);
                // 馬達正轉, 重試開門
                apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_FORWARD, 0);
                dev_state=101;
            }
            else if (dev_err_counter>=6) {
                // 馬達停止
                apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_STOP, 0);
                apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_ERROR, ERROR_MOTOR_FORWARD_ERROR);
                dev_state=104;
            }
            break;
        case 104:
            break;
        case 105:
            // 到達cd點
            if (idMsg==MSG_EVENT_MOTOR_CLOSE_POS || gpio_read(&gpio_motor_close_detect)==0) {
                // 馬達停止
                apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_STOP, 0);
                retry_close=0;
                dev_state=100;
                dev_err_counter=0;
            }
            // 4秒timeout
            else if (rtw_get_passing_time_ms(state_time)>MOTOR_MAX_MSEC) {
                // 馬達停止
                apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_STOP, 0);
                dev_state=100;
            }
            else if (idMsg==MSG_EVENT_TRANSACTION_OK_OPEN) {
                // 吸起電磁閥, 等待50ms
                apSendMsg(pMotorCmdQueue, MSG_GPIO_SOLENOID_UNLOCK, 0);
                vTaskDelay(50);
                // 馬達正轉開門
                apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_FORWARD, 0);
                // 傳送BikeStatus request至cloud, 解除車位
                apSendMsg(pCloudCmdQueue, MSG_EVENT_CLOUD_REQUEST_BIKESTATUS, 3);
                dev_err_counter=0;
                dev_state=101;
            }
            
            break;
        case 200:
            // AD 電壓>900mv, 有物體偵測
            if (GetADValue() > OBJ_EXIST_VALUE) {
                // 到達cd點
                if (idMsg==MSG_EVENT_MOTOR_CLOSE_POS || gpio_read(&gpio_motor_close_detect)==0) {
                    dev_state=2;
                }
                // 無到達cd點
                else {
                    // 吸起電磁閥, 等待50ms
                    apSendMsg(pMotorCmdQueue, MSG_GPIO_SOLENOID_UNLOCK, 0);
                    vTaskDelay(50);
                    // 馬達反轉關門
                    apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_BACKWARD, 0);
                    dev_state=2;
                }
            }
            // 無物體偵測
            else {
                // 到達od點
                if (idMsg==MSG_EVENT_MOTOR_OPEN_POS || gpio_read(&gpio_motor_open_detect)==0) {
                    dev_state=0;
                }
                // 無到達od點
                else {
                    // 吸起電磁閥, 等待50ms
                    apSendMsg(pMotorCmdQueue, MSG_GPIO_SOLENOID_UNLOCK, 0);
                    vTaskDelay(50);
                    // 馬達正轉, 重試開門
                    apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_FORWARD, 0);
                    dev_state=201;
                }
                
            }
            
            break;
        case 201:
            // 到達od點
            if (idMsg==MSG_EVENT_MOTOR_OPEN_POS || gpio_read(&gpio_motor_open_detect)==0) {
                // 馬達停止
                apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_STOP, 0);
                dev_state=0;
            }
            // 4秒timeout
            else if (rtw_get_passing_time_ms(state_time)>MOTOR_MAX_MSEC) {
                // 馬達停止
                apSendMsg(pMotorCmdQueue, MSG_GPIO_MOTOR_STOP, 0);
                dev_state=103;
            }
            break;
        default:
            break;
    }

    if (last_dev_state!=dev_state) {
        obj_detection_counter=0;
        state_time = rtw_get_current_time();
    }
}
//========================================================================================================
// fmt :    0 yyyymmddhhmmssZZZ
//          1 yyyy/mm/dd hh:mm:ss.ZZZ
char *GetSystemTimestamp(char *buf, int fmt);
//========================================================================================================
//void __attribute__((section(".ram_image2.text"))) state_thread(void* in_id) {
void state_thread(void* in_id) {
    struct tMessage sMsg;
    char tbuf[30];

    rtl_printf("State Machine Initializing\n");
    vTaskDelay(2000);
    rtl_printf("State Machine Started\n");
    // reading message queue
    // react state according to messge
    // generate response
    while (global_stop==0) {
        // message income, move the state to next state
        if( xQueueReceive( pSystemQueue, &( sMsg ), ( TickType_t ) 1 ) == pdPASS )
        {
            rtl_printf("System Messge Received : %x\r\n", sMsg.ucMsg);
            switch (sMsg.ucMsg) {
                case MSG_EVENT_USER_BTN:
                case MSG_EVENT_MOTOR_OPEN_POS:
                case MSG_EVENT_MOTOR_CLOSE_POS:
                case MSG_EVENT_RADAR_DETECTED:
                case MSG_EVENT_TRANSACTION_OK_OPEN:
                case MSG_EVENT_GATE_BLOCKED:
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

                case MSG_EVENT_REBOOT:
                    rtl_printf("\n\rReboot the system\n\r");
                    sys_reset();
                    break;
                    
                case MSG_EVENT_UPGRADE_FIRMWARE:
                    rtl_printf("\n\rOTA Firmware\n\r");
                    sprintf(tbuf, "%d", sMsg.ucParam);
                    ccmd_ota(tbuf);
                    break;

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
    }
	/* Kill init thread after all init tasks done */
	vTaskDelete(NULL);
}

#endif

