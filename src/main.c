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
#include "analogin_api.h"
#include "wdt_api.h"

#define MBED_ADC_EXAMPLE_PIN_1    AD_1	// no pin out
#define MBED_ADC_EXAMPLE_PIN_2    AD_2	// HDK, A1
#define MBED_ADC_EXAMPLE_PIN_3    AD_3	// HDK, A2


//========================================================================================================
const char dev_version[]=DEV_VERSION;
//========================================================================================================
extern struct sys_profile_t sys_profile;    // from net_util.c
//========================================================================================================
extern u32 last_alive_time;    // 
extern u32 last_req_time;    // WDT timer of req_thread
extern u32 last_cmd_time;    // WDT timer of cmd_thread
//========================================================================================================
extern uint8_t g_NetState;
//========================================================================================================
QueueHandle_t pMotorCmdQueue = NULL;     // motor command queue, created by main.c
QueueHandle_t pSystemQueue = NULL;       // system messqge queue, created by main.c

//========================================================================================================
SemaphoreHandle_t xMutexAD;
//========================================================================================================
extern void console_init(void);
extern void gpio_motor_init();      // motor_control.c
extern void net_main(void);         // net_util.c
extern uint8_t identify_url(int *url_start_pos, char *url); // net_util.c
//========================================================================================================
//========================================================================================================
//gtimer_t global_timer;
//volatile uint32_t global_tick=0;
//========================================================================================================
uint8_t global_stop=0;
//========================================================================================================
extern gpio_t gpio_p_sensor;
extern gpio_t gpio_v_sensor;
//========================================================================================================
//void global_timer_timeout_handler(uint32_t id) {
//    global_tick++;
//}
volatile uint8_t dev_state=10, dev_err_counter=0, detection_counter=0;
volatile u32 state_time=0, AD_time=0, detection_time=0;
//========================================================================================================
extern TaskHandle_t hReqThread, hSysThread, hCmdThread;
TaskHandle_t hMainThread;
//========================================================================================================
// ADC definitions

#define AD_OFFSET 		0x3da							
#define AD_GAIN_DIV	0x3b9							
#define AD2MV(ad,offset,gain) (((ad>>4)-offset)*1000/gain)	

uint32_t GetADValue() {
    analogin_t   adc0;
    uint16_t adcdat0    = 0;
    int32_t v_mv0;
    uint32_t v_mv_sum = 0;
    int i;

    xSemaphoreTake(xMutexAD, portMAX_DELAY);
    //uint16_t offset, gain;
    analogin_init(&adc0, MBED_ADC_EXAMPLE_PIN_3);	// no pinout on HDK board
    /*
    sys_adc_calibration(0, &offset, &gain);
    //printf("ADC:offset = 0x%x, gain = 0x%x\n", offset, gain);
    if((offset==0xFFFF) || (gain==0xFFFF))
    {
        offset = AD_OFFSET;
        gain = AD_GAIN_DIV;
        //printf("ADC:offset = 0x%x, gain = 0x%x\n", offset, gain);
    }
    */
    for (i=0;i<4;i++) {
        adcdat0 = analogin_read_u16(&adc0);
        v_mv0 = AD2MV(adcdat0, AD_OFFSET, AD_GAIN_DIV);
        vTaskDelay(50);
        v_mv_sum += v_mv0;
    }
    v_mv_sum = v_mv_sum>>2;
    //printf("AD0:%x = %d mv\n", adcdat0, v_mv_sum); 
    analogin_deinit(&adc0);

    xSemaphoreGive(xMutexAD);

    return v_mv_sum;
}
//========================================================================================================
void apSendMsg(QueueHandle_t q, unsigned char id, unsigned char param) {
    struct tMessage msg;
    int retry=0;
    msg = (struct tMessage) {.ucMsg=id, .ucParam=param};
    while (xQueueSend(q, ( void * ) &msg, ( TickType_t ) 0 )!=pdTRUE) {
        vTaskDelay(20);
        retry++;
        // blocking
        if (retry>50) {
            // if the error occured in pSystemQueue, don't care and exit immediately
            if (id!=MSG_EVENT_SYS_UNSTABLE)
                apSendMsg(pSystemQueue, MSG_EVENT_SYS_UNSTABLE, param);
            break;
        }
    }
}
//========================================================================================================
uint8_t state_machine(unsigned char idMsg, unsigned char ucParam, void *data);  // from state_parkey2.c or sate_parkey3.c
//========================================================================================================
void state_thread(void* in_id);
//========================================================================================================
// fmt :    0 yyyymmddhhmmssZZZ
//          1 yyyy/mm/dd hh:mm:ss.ZZZ
char *GetSystemTimestamp(char *buf, int fmt) {
    time_t tmt = rtc_read();
    struct tm *timeinfo = localtime(&tmt);
    if (fmt==0) {
        sprintf(buf, "%d%02d%02d%02d%02d%02d000", timeinfo->tm_year+1900, timeinfo->tm_mon+1, 
                timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    }
    else {
        sprintf(buf, "%d/%02d/%02d %02d:%02d:%02d", timeinfo->tm_year+1900, timeinfo->tm_mon+1, 
                timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    }
    return buf;
}
//========================================================================================================


//========================================================================================================
// *******************************************************************************************************
//
// User Defined log_service commands
// use for debug / monitor the firmware state
//
void ccmd_query(void *arg){
    char tbuf[30];
    rtl_printf("\n\r\x1b[32mQuery firmware status\x1b[0m\r\n");
    rtl_printf("=======================\r\n");
    /*
    rtl_printf("Device Id : %s\r\n", sys_profile.devid);
    rtl_printf("\x1b[33m-----------------------\x1b[0m\r\n");
    rtl_printf("System Version : %s\r\n", dev_version);
    rtl_printf("System Build Time : %s %s\r\n", __DATE__, __TIME__);
    rtl_printf("System Time : %s\r\n", GetSystemTimestamp(tbuf, 1));
    rtl_printf("\x1b[33m-----------------------\x1b[0m\r\n");
    rtl_printf("WIFI ssid : %s\r\n", sys_profile.dev_ssid);
    rtl_printf("WIFI pass : %s\r\n", sys_profile.dev_pass);
    rtl_printf("\x1b[33m-----------------------\x1b[0m\r\n");
    rtl_printf("Device State : \x1b[33m%d\x1b[0m\r\n", dev_state);
    rtl_printf("Device err_counter : \x1b[31m%d\x1b[0m\r\n", dev_err_counter);
    rtl_printf("State timer : %u\r\n", rtw_get_passing_time_ms(state_time));
    rtl_printf("AD timer : %u\r\n", rtw_get_passing_time_ms(AD_time));
    rtl_printf("\x1b[33m-----------------------\x1b[0m\r\n");
    rtl_printf("last_alive_time : %u\r\n", rtw_get_passing_time_ms(last_alive_time));
    rtl_printf("last_req_time : %u\r\n", rtw_get_passing_time_ms(last_req_time));
    rtl_printf("last_cmd_time : %u\r\n", rtw_get_passing_time_ms(last_cmd_time));
    rtl_printf("\x1b[33m-----------------------\x1b[0m\r\n");
    rtl_printf("P Sensor : %s\r\n", gpio_read(&gpio_p_sensor)==0 ? "ON" : "OFF");
    rtl_printf("V Sensor : %s\r\n", gpio_read(&gpio_v_sensor)==0 ? "ON" : "OFF");
    rtl_printf("\x1b[33m-----------------------\x1b[0m\r\n");
    rtl_printf("Network State : \x1b[33m%d\x1b[0m\r\n", g_NetState);
    rtl_printf("\x1b[33m-----------------------\x1b[0m\r\n");
    rtl_printf("Request Server URL : %s\r\n", sys_profile.req_url);
    rtl_printf("Request Server Port : %d\r\n", sys_profile.req_port);
    rtl_printf("OTA Server URL : %s\r\n", sys_profile.ota_url);
    rtl_printf("OTA Server Port : %d\r\n", sys_profile.ota_port);
    rtl_printf("OTA Filename : %s\r\n", sys_profile.ota_file);
    rtl_printf("Push Server URL : %s\r\n", sys_profile.alive_url);
    rtl_printf("Push Server Port : %d\r\n", sys_profile.alive_port);

    // the variable is defined in project/realtek_ameba1_va0_example/inc/FreeRTOSConfig.h
#if defined(INCLUDE_uxTaskGetStackHighWaterMark) && (INCLUDE_uxTaskGetStackHighWaterMark == 1)    
    rtl_printf("\x1b[33m-----------------------\x1b[0m\r\n");
    rtl_printf("Stack Water Marks : Main=%u\r\n", uxTaskGetStackHighWaterMark(hMainThread));
    rtl_printf("Stack Water Marks : Net_Sys=%u, Net_Cmd=%u, Net_Req=%u\r\n", 
            uxTaskGetStackHighWaterMark(hSysThread), uxTaskGetStackHighWaterMark(hCmdThread), 
            uxTaskGetStackHighWaterMark(hReqThread));
#endif
*/
    rtl_printf("=======================\r\n");
    
}
//========================================================================================================
void ccmd_reboot(void *arg){
    rtl_printf("\n\r\x1b[32mReboot the system\x1b[0m\r\n");
    rtl_printf("=======================\r\n");
    apSendMsg(pSystemQueue, MSG_EVENT_REBOOT, 0);
    sys_reset();
}
//========================================================================================================
void ccmd_setrtc(void *arg){
    rtl_printf("\n\r\x1b[32mSet RTC Clock\x1b[0m\r\n");
    rtl_printf("=======================\r\n");
    char *argv[8] = {0};
    int argc, errorno=0;
    struct tm timeinfo;
    time_t tt;
    
    if (arg){
        argc = parse_param(arg, argv);
        for (int i=0;i<argc;i++) {
            rtl_printf("arg[%d]=%s\r\n", i, argv[i]);
        }
        if (argv[1]!=NULL && strlen(argv[1])==4)
            timeinfo.tm_year = atoi(argv[1])-1900;
        else
            errorno=1;
        if (errorno>0)
            rtl_printf("errorno=%d\r\n", errorno);
        if (argv[2]!=NULL && strlen(argv[2])>=1 && strlen(argv[2])<=2)
            timeinfo.tm_mon = atoi(argv[2])-1;
        else
            errorno=2;
        if (errorno>0)
            rtl_printf("errorno=%d\r\n", errorno);
        if (argv[3]!=NULL && strlen(argv[3])>=1 && strlen(argv[3])<=2)
            timeinfo.tm_mday = atoi(argv[3]);
        else
            errorno=3;
        if (errorno>0)
            rtl_printf("errorno=%d\r\n", errorno);
        if (argv[4]!=NULL && strlen(argv[4])>=1 && strlen(argv[4])<=2)
            timeinfo.tm_hour = atoi(argv[4]);
        else
            errorno=4;
        if (errorno>0)
            rtl_printf("errorno=%d\r\n", errorno);
        if (argv[5]!=NULL && strlen(argv[5])>=1 && strlen(argv[5])<=2)
            timeinfo.tm_min = atoi(argv[5]);
        else
            errorno=5;
        if (errorno>0)
            rtl_printf("errorno=%d\r\n", errorno);
        if (argv[6]!=NULL && strlen(argv[6])>=1 && strlen(argv[6])<=2)
            timeinfo.tm_sec = atoi(argv[6]);
        else
            errorno=6;
        if (errorno>0)
            rtl_printf("errorno=%d\r\n", errorno);
        timeinfo.tm_isdst=0;

        if (errorno==0) {
            tt = mktime(&timeinfo);
            rtl_printf("tt=%d\r\n", tt);
            rtc_write(tt);
        }
        rtl_printf("\n\rSet RTC : %d/%02d/%02d %02d:%02d:%02d\r\n", timeinfo.tm_year+1970, timeinfo.tm_mon+1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
    else {
        rtl_printf("\r\n[frtc] Usage: frtc=<year>,<month>,<day>,<hour>,<min>,<sec>");
    }
}
/*
//========================================================================================================
// define customize commands
// !! Note !!  the length of command should less than 4
log_item_t cust_cmd_items[ ] = {
      {"fc", ccmd_query,},         // query system state
      {"fres", ccmd_reboot,},       // reboot  system
      {"frtc", ccmd_setrtc,},       // set RTC
};
//========================================================================================================
// define the init function, let log_service.c to include our customized commands
void at_app_init(void) {
    // add the customized command
    log_service_add_table(cust_cmd_items, sizeof(cust_cmd_items)/sizeof(cust_cmd_items[0]));
}
//========================================================================================================
log_module_init(at_app_init);
//========================================================================================================
*/
//
//
//
//
//
//========================================================================================================
void main(void) {

    // 開啟httpd之後, resource 變得很少, 所以不能開太多threads

    // ----------------------------------------------------
	/* Initialize log uart and at command service */
	//console_init();

    xMutexAD = xSemaphoreCreateMutex();

    // ----------------------------------------------------
    // 1ms timer handler
    //gtimer_init(&global_timer, TIMER1);
    //gtimer_start_periodical(&global_timer, 1000, (void*)global_timer_timeout_handler, NULL);

    // ----------------------------------------------------
    // Initial queues
    pMotorCmdQueue = xQueueCreate( 10, sizeof( struct tMessage ));
    pSystemQueue = xQueueCreate( 10, sizeof( struct tMessage ));

    // initial RTC
    rtc_init();

    // initial the wifi
    net_main();

    // initial watchdog timer
    //watchdog_init(15000);
    //watchdog_start();
	if(xTaskCreate(state_thread, ((const char*)"state_machine"), 512, NULL, tskIDLE_PRIORITY + 3 + PRIORITIE_OFFSET, hMainThread) != pdPASS)
		rtl_printf("\n\r%s xTaskCreate(state_thread) failed", __FUNCTION__);

    // ----------------------------------------------------
    // Initial gpio and motor controllers
    //printf("\n\rcalling gpio init...\n\r");
    gpio_motor_init();


    // ----------------------------------------------------
    // FreeRTOS main loop
#if defined(CONFIG_KERNEL) && !TASK_SCHEDULER_DISABLED
	#ifdef PLATFORM_FREERTOS
	vTaskStartScheduler();
	#endif
#else
	RtlConsolTaskRom(NULL);
#endif
}


