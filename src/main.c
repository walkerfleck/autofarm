// TODO:  
// 1. save profile太慢了, 要把device, devtask 分開寫,只要4bytes為單位就可, 寫時也要重寫入cksum  .. ok, need to test.
// 2. 定時polling device, 還沒MQTT連線
// 3. 加上MQTT, 直接連server, 如果Server未註冊就不理會它
// 4. 設計server MQTT protocol
// 5. 直接對device下命令

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
#include "serial_api.h"

//========================================================================================================
void  save_profile_to_flash();      // net_util.c
void  save_device_to_flash(uint8_t devid);
//========================================================================================================
const char dev_version[]=DEV_VERSION;
//========================================================================================================
extern sys_profile_t sys_profile;    // from net_util.c
//========================================================================================================
extern u32 last_alive_time;    // 
extern u32 last_req_time;    // WDT timer of req_thread
extern u32 last_cmd_time;    // WDT timer of cmd_thread
//========================================================================================================
extern uint8_t g_NetState;
//========================================================================================================
//QueueHandle_t pMotorCmdQueue = NULL;     // motor command queue, created by main.c
QueueHandle_t pSystemQueue = NULL;       // system messqge queue, created by main.c
QueueHandle_t pCloudCmdQueue = NULL;     // command issued by cloud server, created by main.c
//========================================================================================================
uint8_t gIgnoreNetStateMachine;
//========================================================================================================
//SemaphoreHandle_t xMutexAD;
SemaphoreHandle_t xMutexUARTCmd;
//========================================================================================================
extern void console_init(void);
extern void net_main(void);         // net_util.c
extern uint8_t identify_url(int *url_start_pos, char *url); // net_util.c
//========================================================================================================
//========================================================================================================
//gtimer_t global_timer;
//volatile uint32_t global_tick=0;
//========================================================================================================
uint8_t global_stop=0;
//========================================================================================================
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
#define UART_TX    PA_7	//PB_5
#define UART_RX    PA_6	//PB_4    
#define SRX_BUF_SZ 40
volatile serial_t    sobj;
uint8_t rx_buf[SRX_BUF_SZ]={0};
volatile int idx_rx=0;
extern struct sFlags sysFlags;
char rx_pkt[20]={0};
//========================================================================================================
unsigned char x2i(char c) {  // from HEX to decimal
    if (c>='0' && c<='9')
        return c-'0';
    else if (c>='a' && c<='f')
        return c-'a'+10;
    else if (c>='A' && c<='F')
        return c-'A'+10;
}
//========================================================================================================
unsigned char HEX_BYTE_2_DEC(char *h) {
    return x2i(h[0]) * 16 + x2i(h[1]);
}
//========================================================================================================
// 檢查是否uid重覆
// return
//      1 - 合法的新增
//      0 - 不合法
uint8_t sysprofile_IsNewDevidValid(char *uid) {
    int i;
    // 搜尋dev庫, 找到devid不連續的號碼, 或者是最後一號加1
    for(i=0;i<MAX_DEVICE_NUM;i++) {
        if (sys_profile.dev[i].devid!=0 && memcmp(sys_profile.dev[i].uid, uid, 12)==0) {
            return 0;
        }
    }
    return 1;
}
//========================================================================================================
// system profile routines
//  回傳 最新的, 空的devid
uint8_t sysprofile_NewDevid() {
    int i;
    uint8_t newdevid=0;
    // 搜尋dev庫, 找到devid不連續的號碼, 或者是最後一號加1
    for(i=0;i<MAX_DEVICE_NUM;i++) {
        if (sys_profile.dev[i].devid==0) {
            return i+1;
        }
    }
}
//========================================================================================================
uint8_t sysprofile_InsertDevid(uint8_t devid, uint8_t devtype, uint8_t *uid) {
    sys_profile.dev[devid-1].devid=devid;
    sys_profile.dev[devid-1].dev_type=devtype;
    sys_profile.dev[devid-1].dev_state=3;
    memcpy(sys_profile.dev[devid-1].uid, uid, 12);
}
//========================================================================================================
#define DEVTYPE_UNKNOWN     0
#define DEVTYPE_WATERGATE   1
#define DEVTYPE_SMARTGATE   2
#define DEVTYPE_BTNSENSOR   3
#define DEVTYPE_RELAY       4
static char *device_types[]={"UNKNOWN","WATERGATE", "SMARTGATE", "BTNSENSOR", "RELAY"};

//========================================================================================================
uint8_t sysprofile_RemoveDevid(uint8_t devid) {
    int i;
    // 搜尋dev庫, 找到devid, 把這個slot填0
    for(i=0;i<MAX_DEVICE_NUM;i++) {
        if (sys_profile.dev[i].devid!=0) {
            if (sys_profile.dev[i].devid==devid) {
                memset(&(sys_profile.dev[i]), 0, sizeof(sDevice));
                save_profile_to_flash();
                rtl_printf("Device ID=%u has been removed!\n",devid);
                break;
            }
        }
    }
}
//========================================================================================================
uint8_t sysprofile_ListDevid() {
    int i;
    // 搜尋dev庫, 找到devid不連續的號碼, 或者是最後一號加1
    rtl_printf("=============================================================\n");
    rtl_printf("devid\ttype\t\tstate\tuid\n");
    for(i=0;i<MAX_DEVICE_NUM;i++) {
        if (sys_profile.dev[i].devid!=0) {
            rtl_printf("%u\t%s\t%u\t", sys_profile.dev[i].devid, device_types[sys_profile.dev[i].dev_type], sys_profile.dev[i].dev_state);
            int j;
            for(j=0;j<12;j++) {
                rtl_printf("%02X",sys_profile.dev[i].uid[j]);
            }
            rtl_printf("\n");
        }
    }
}

//========================================================================================================
// Packet routines
//
//========================================================================================================
uint16_t NextRand16bits(uint16_t seed) {
    uint32_t seed32 = seed * 0x12F;
    seed32 = (0xF121 * seed32 + 0x90F3); 

    return seed32  % 0x10000;
}
//========================================================================================================
uint8_t GenCheckCode(uint8_t *buf, int len) {
    int i;
    uint16_t cksum=0;
    for(i=0;i<len;i++) {
        cksum += buf[i];
    }
    return NextRand16bits(cksum) & 0x0ff;
}
//========================================================================================================
uint8_t *Packet_Match(uint8_t *buf, uint8_t *uid) {
    // 已在profile的uid就不成立
    if (sysprofile_IsNewDevidValid(uid)==0)
        return 255;
    // create match command to match
    struct sPacket_Match *pkt = (struct sPacket_Match *)buf;
    pkt->head='P';
    pkt->id=sysprofile_NewDevid();
    pkt->cmd='G';
    pkt->length=17;
    memcpy(pkt->uid, uid, 12);
    pkt->cksum=GenCheckCode(buf, 16);
    if (pkt->id==0)
        return NULL;
    return 17;
}
//========================================================================================================
uint8_t *Packet_Query(uint8_t *buf, uint8_t devid) {
    // create match command to match
    struct sPacket_Query *pkt = (struct sPacket_Query *)buf;
    pkt->head='P';
    pkt->id=devid;
    pkt->cmd='Q';
    pkt->length=5;
    pkt->cksum=GenCheckCode(buf, 4);
    return 5;
}
//========================================================================================================
int8_t UARTSendBuf(uint8_t *buf_in, uint8_t *buf_out, int len) {
    xSemaphoreTake(xMutexUARTCmd, portMAX_DELAY);
    int counter=0;
    int i;
    // 送出command, 等待回應
    sysFlags.Packet_received=0;
    uart_send_string(&sobj, buf_out, len);
    // 等待約6秒鐘
    while (sysFlags.Packet_received==0 && counter<(1000/200)*6) {
        vTaskDelay(200);
        counter++;
    }
    // 檢查是否收到回應
    if (sysFlags.Packet_received==0) {
        rtl_printf("UARTSendBuf : Remote device not response!\n");
        xSemaphoreGive(xMutexUARTCmd);
        return -2;
    }
    else {
        // 回傳回應
        // 馬上把buffer copy至buf, 才不會被其它封包影響
        memcpy(buf_in, rx_pkt, len);
        xSemaphoreGive(xMutexUARTCmd);

        return 0;
    }
    xSemaphoreGive(xMutexUARTCmd);
}
//========================================================================================================
// 
// 回傳:    0 - 成功
//          -1 - 
//          -2 - device沒回應
//          -3 - 回傳錯誤
//          -4 - 
int8_t SendQueryDevice(uint8_t devid, uint8_t *data) {
    //rtl_printf("\nSendQueryDevice() begin\n");
    // 這裡的uid是binary型態, 長度為12
    // create match command to match
    uint8_t buf_out[20], buf_in[20];
    int len;
    len=Packet_Query(buf_out, devid);
    //rtl_printf("\nSendQueryDevice() packet made.\n");

    int8_t ret = UARTSendBuf(buf_in, buf_out, len);
    //rtl_printf("\nSendQueryDevice() Buffer Sent.\n");
    if (ret==0) {
        // 之前已檢查cksum, 現在只需檢查回傳內容
        if (buf_in[2]!='q') {
            //rtl_printf("\nSendQueryDevice() cksum error.\n");
            // 張冠李戴
            return -3;
        }
        struct sPacket_Query_Ack *pkt=(struct sPacket_Query_Ack *)buf_in;

        memcpy(data, pkt->data, 1);

        //rtl_printf("\nSendQueryDevice() end.\n");

        return 0;
    }
    else
        return ret;
}
//========================================================================================================
// 針對uid的device傳送match command, 等待回傳
// uid is binary, length is 12
// 回傳成功, 就把device加入dev list
// 呼叫後會blocking
// 回傳:    0 - 成功
//          -1 - device沒空間
//          -2 - device沒回應
//          -3 - 回傳錯誤
//          -4 - uid重複
int8_t SendMatch(uint8_t *uid) {
    // 這裡的uid是binary型態, 長度為12
    // create match command to match
    uint8_t buf_out[20], buf_in[20];
    int len;
    if ((len=Packet_Match(buf_out, uid))==NULL) {
        rtl_printf("\nInsufficient Device slot!\n");
        return -1;
    }
    else if (len==255) {
        rtl_printf("\nDuplicate Device uid!\n");
        return -4;
    }
    else {
        int8_t ret = UARTSendBuf(buf_in, buf_out, len);
        if (ret==0) {
            // 之前已檢查cksum, 現在只需檢查回傳內容
            if (buf_in[2]!='g') {
                // 張冠李戴
                return -3;
            }
            uint8_t devid = buf_in[1];
            // 建立一個新的device
            // 前面呼叫PacketMatch()中會呼叫sysprofile_NewDevid()填入device slot的devid, 我們用這個buf_in[1]來當做devid
            sysprofile_InsertDevid(buf_in[1], buf_in[4], uid);
            // 需要存至eeprom
            //save_device_to_flash(devid);
            save_profile_to_flash();
            rtl_printf("\nDevice Added: id=%d, devtype=%d!\n", buf_in[1], buf_in[4]);
            return 0;
        }
        else
            return ret;
    }
}
//========================================================================================================
// parse command from UART (LoRa)
uint8_t ParseCmd() {
    uint8_t ret=0;
    //rtl_printf("*");
    if (rx_buf[0]=='P' && idx_rx==rx_buf[3]) {
        //rtl_printf("checksum:%02X , %02X\n", GenCheckCode(rx_buf, rx_buf[3]-1), rx_buf[idx_rx-1]);
        // 收滿了bytes, 檢查cksum
        if (GenCheckCode(rx_buf, rx_buf[3]-1)==rx_buf[idx_rx-1]) {
            ret = rx_buf[1];
            // 複製到rx_pkt;
            memcpy(rx_pkt+1, rx_buf+1, idx_rx-1);
            // 最後再設定flag, 讓其它檢查buffer的thread安全
            sysFlags.Packet_received=1;
            /*
            switch(rx_buf[0]) {
                case 'g':           // match
                    break;
                case 'k':           // kill match
                    break;
                case 'q':           // query status
                    break;
                case 's':           // set parameters
                    break;
            }
            */
        }
        else {      // cksum不正確
            sysFlags.Packet_received=0;
            idx_rx=0;
        }
    }
    else  {     // 長度不正確
        //idx_rx=0;
    }
    return ret;
}

//
//========================================================================================================
// 把日期轉成time_t數字
// 01234567890123456
// yyyymmddhhmmssZZZ
//========================================================================================================
time_t Timestamp_to_time_t(char *tm) {
    if (strlen(tm)!=17)
        return 0;
    struct tm timeinfo;
    time_t tt;
    char tmp[6];
    strncpy(tmp, tm, 4); tmp[4]=0;      // year
    timeinfo.tm_year = atoi(tmp)-1900;
    strncpy(tmp, tm+4, 2); tmp[2]=0;      // month
    timeinfo.tm_mon = atoi(tmp)-1;
    strncpy(tmp, tm+6, 2); tmp[2]=0;      // day
    timeinfo.tm_mday = atoi(tmp);
    strncpy(tmp, tm+8, 2); tmp[2]=0;      // hour
    timeinfo.tm_hour = atoi(tmp);
    strncpy(tmp, tm+10, 2); tmp[2]=0;      // min
    timeinfo.tm_min = atoi(tmp);
    strncpy(tmp, tm+12, 2); tmp[2]=0;      // sec
    timeinfo.tm_sec = atoi(tmp);
    timeinfo.tm_isdst=0;
    tt = mktime(&timeinfo);
    return tt;
}
//========================================================================================================
char *time_t_to_Timestamp(char *dst, time_t value) {
    struct tm *timeinfo = localtime(&value);
    sprintf(dst, "%d%02d%02d%02d%02d%02d000", timeinfo->tm_year+1900, timeinfo->tm_mon+1, 
            timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
}
//========================================================================================================
#define GPIO_JC1278_SET             PB_4          // [O]
#define GPIO_USER_BUTTON            PA_2          // [I]
#define GPIO_TEST                   PB_5          // [I]
gpio_t gpio_user_button;
gpio_t gpio_jc1278_set;
gpio_t gpio_test_set;
//========================================================================================================
//   UART Routines
//========================================================================================================
//========================================================================================================
void uart_irq(uint32_t id, SerialIrq event)
{
    serial_t    *sobjt = (void*)id;

    if(event == RxIrq) {
        char rc = serial_getc(sobjt);
        //printf("%02X ", (unsigned char)rc);

        // during JC1278A setting
        if (sysFlags.JC1278A_setting==1) {
            rx_buf[idx_rx]=rc;
            idx_rx++;
            if (idx_rx>=18) {
                idx_rx=0;
                gpio_write(&gpio_jc1278_set, 1);
                sysFlags.JC1278A_setting=0;
                rtl_printf("\nJC1278A configuration finished!\n");
            }
        }
        else {
            rx_buf[idx_rx++]=rc;
            //rtl_printf("$");
            // LoRa protocol
            // header不對, 重新來過
            if (idx_rx==1 && rx_buf[0]!='P') {
                idx_rx=0;
                //rtl_printf("##");
            }
            else if (idx_rx==4) {
                // 長度不正確, 重新來過
                if (rx_buf[3]>18) {
                    idx_rx=0;
                    //rtl_printf("!!");
                }
            }
            else if (idx_rx>4) {
                // header4個bytes, 接收超過header, 就檢查packet
                if (ParseCmd()!=0) {
                    idx_rx=0;       // 接收到完整封包                
                    //rtl_printf("!!");
                }
            }
        }

    }
    if(event == TxIrq){
        //rtl_printf(">>\n");
    }
}

//========================================================================================================
void uart_send_string(serial_t *sobj, char *pstr, int len)
{
    unsigned int i=0;

    //printf("Sending : ");
    while (i<len) {
        //rtl_printf("uart_send_string(%d)\n", i);
        if (serial_writable(sobj)) {
            serial_putc(sobj, *(pstr+i));
            //printf(" %02X", (unsigned char)*(pstr+i));
            i++;
        }
    }
    //printf("\n");
    /*
    strcpy(tx_buf, pstr);
    idx_tx=0;
    if (tx_buf[idx_tx]!=0) {
        serial_putc(sobj, tx_buf[idx_tx]);
        idx_tx++;
    }
    */

    //serial_putc(sobj, '\r');
    //serial_putc(sobj, '\n');
}

//========================================================================================================
int uart_read_string(serial_t *sobj, char *pstr, int len)
{
    int i=0;

    while (*(pstr+i) != 0) {
        if (serial_writable(sobj)) {
            serial_putc(sobj, *(pstr+i));
            i++;
        }
    }
}
//========================================================================================================
// ADC definitions
/*
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
*/
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
//========================================================================================================
void ccmd_test(void *arg) {
    char *argv[8] = {0};
    int argc, errorno=1;
    struct tm timeinfo;
    time_t tt;

    if (arg){
        argc = parse_param(arg, argv);
        if (argv[1]!=NULL) {
            if (argv[1][0]=='l') {
                // output 
                if (argv[2]!=NULL && argv[3]!=NULL) {
                    errorno=0;
                }
                else {
                    errorno=1;
                }
            }
            else if (argv[1][0]=='w') {
                // output 
                if (argv[2]!=NULL && argv[3]!=NULL) {
                    errorno=0;
                }
                else {
                    errorno=1;
                }
            }
        }
    }
    if (errorno!=0) {
        rtl_printf("\r\n[ftst] Usage: fstt=<op>, [ args ]\n");
        rtl_printf("=========================================================================================\n");
        rtl_printf("                 *op=l Login to MTQQ Server\n");
        rtl_printf("                  args : <username> <password> is required for login \n");
        rtl_printf("=========================================================================================\n");
        rtl_printf("                 *op=w connect to wifi\n");
        rtl_printf("                  args : <ssid> <wifi_secret> is required for login \n");
    }
}
//========================================================================================================
// 
void ccmd_match(void *arg) {
    char *argv[8] = {0};
    int argc, errorno=1;
    struct tm timeinfo;

    if (arg){
        argc = parse_param(arg, argv);
        // 12 hex digit, length is 24
        if (argv[1]!=NULL && strlen(argv[1])==24) {
            uint8_t buf[13];
            int i;
            printf("gen : ");
            for(i=0;i<12;i++) {
                buf[i]=HEX_BYTE_2_DEC(&(argv[1][i*2]));
                printf(" %02X", buf[i]);
            }
            printf("\n");
            if (SendMatch(buf)==0)
                errorno=0;
        }
    }
    if (errorno!=0) {
        rtl_printf("\r\n[fmat] Usage: match=<uid>\n");
        rtl_printf("\r\nmatch a device with its devid, the length is 12bytes\n");
        rtl_printf("=========================================================================================\n");
    }
}
//========================================================================================================
// 
void ccmd_remove_device(void *arg) {
    char *argv[8] = {0};
    int argc, errorno=1;
    struct tm timeinfo;

    if (arg){
        argc = parse_param(arg, argv);
        // 12 hex digit, length is 24
        if (argv[1]!=NULL) {
            uint8_t devid = atoi(argv[1]);
            sysprofile_RemoveDevid(devid);
            errorno=0;
        }
    }
    if (errorno!=0) {
        rtl_printf("\r\n[frmd] Usage: frmd=<devid>\n");
        rtl_printf("\r\nremove a device with its devid\n");
        rtl_printf("=========================================================================================\n");
    }
}
//========================================================================================================
// 
void ccmd_query_device(void *arg) {
    char *argv[8] = {0};
    int argc, errorno=1;
    struct tm timeinfo;

    if (arg){
        argc = parse_param(arg, argv);
        // 12 hex digit, length is 24
        if (argv[1]!=NULL) {
            uint8_t devid = atoi(argv[1]);
            int8_t ret;
            uint8_t data[10];
            memset(data, 0, 10);
            // send query, return 1 byte data
            ret = SendQueryDevice(devid, data);
            if(ret==0) {
                // 傳送成功
                printf("Query: the state of device[%d] is :%u\n", devid-1, data[0]);
            }
            else {
                printf("Query: QueryDevice failed : %d\n", ret);
            }
            errorno=0;
        }
    }
    if (errorno!=0) {
        rtl_printf("\r\n[fqd] Usage: fqd=<devid>\n");
        rtl_printf("\r\nquery a device with its devid\n");
        rtl_printf("=========================================================================================\n");
    }
}
//========================================================================================================
// 
void ccmd_save(void *arg) {

    save_profile_to_flash();
    rtl_printf("\r\n[fsav]: profile saved.\n");
}
//========================================================================================================
// 
void ccmd_list(void *arg) {
    rtl_printf("\r\n[fls]: Registered devices : \n");
    sysprofile_ListDevid();
}

//========================================================================================================
// define customize commands
// !! Note !!  the length of command should less than 4
log_item_t cust_cmd_items[ ] = {
      {"fc", ccmd_query,},         // query system state
      {"ftst", ccmd_test,},        // query system state
      {"fmat", ccmd_match,},       // match a device
      {"frmd", ccmd_remove_device,},  // match a device
      {"fqd", ccmd_query_device,}, // query a device
      {"fsav", ccmd_save,},        // save config to eeprom
      {"fls", ccmd_list,}          // list device
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
//
//
//
//
//
//========================================================================================================
void plain_delay(int lp) {
    int i,j;
    for(i=0;i<lp;i++) {
        for(j=0;j<65535;j++)
            asm(" nop");
    }
}

//========================================================================================================
void main(void) {

    // 開啟httpd之後, resource 變得很少, 所以不能開太多threads

    // ----------------------------------------------------
	/* Initialize log uart and at command service */
	console_init();

    // initial received packet buffer
    rx_pkt[0]=0;

    //xMutexAD = xSemaphoreCreateMutex();
    xMutexUARTCmd = xSemaphoreCreateMutex();

    // ----------------------------------------------------
    // 1ms timer handler
    //gtimer_init(&global_timer, TIMER1);
    //gtimer_start_periodical(&global_timer, 1000, (void*)global_timer_timeout_handler, NULL);

    // ----------------------------------------------------
    // Initial queues
    //pMotorCmdQueue = xQueueCreate( 10, sizeof( struct tMessage ));
    pSystemQueue = xQueueCreate( 10, sizeof( struct tMessage ));
    pCloudCmdQueue = xQueueCreate( 10, sizeof( struct tMessage ));


    // initial UART
    serial_init(&sobj,UART_TX,UART_RX);
    serial_baud(&sobj,9600);
    serial_format(&sobj, 8, ParityNone, 1);
    //serial_set_flow_control(&sobj, FlowControlRTSCTS, 0, 0);
    serial_irq_handler(&sobj, uart_irq, (uint32_t)&sobj);
    serial_irq_set(&sobj, RxIrq, 1);

    // initial RTC
    rtc_init();

    // initial the wifi
    net_main();

    // initial watchdog timer
    //watchdog_init(15000);
    //watchdog_start();
	if(xTaskCreate(state_thread, ((const char*)"state_machine"), 512, NULL, tskIDLE_PRIORITY + 3 + PRIORITIE_OFFSET, hMainThread) != pdPASS)
	//	rtl_printf("\n\r%s xTaskCreate(state_thread) failed", __FUNCTION__);

    // ----------------------------------------------------
    // Initial gpio and motor controllers
    // 因為某種原因, gpio在另一個檔案initial的code不會被執行到,
    // motor_control.c的程式在編譯後移位, 所以執行不到
    // 這是個奇怪的現象
    //
    //
    //
    //printf("\n\rcalling gpio init...\n\r");
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

    gpio_write(&gpio_jc1278_set, 0);
    gpio_write(&gpio_test_set, 0);

    // enter JC1278A setup mode
    //gpio_write(&gpio_jc1278_set, 0);
    //gpio_write(&gpio_test_set, 0);

    plain_delay(100);
//    gpio_write(&gpio_test_set, 1);

    sysFlags.JC1278A_setting=1;
    rtl_printf("Configuring JC1278A module\n");
    // initial JC1278A
    uart_send_string(&sobj, "\xAA\x5A\x57\x04\xAE\x08\x00\x0F\x00\x04\x00\x00\x00\x06\x00\x12\x00\x40", 18);

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


