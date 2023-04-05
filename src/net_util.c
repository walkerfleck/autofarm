/*
 *  
 *
 *  Routines to access hardware
 *
 *  Copyright (c) 2013 Realtek Semiconductor Corp.
 *
 *  This module is a confidential and proprietary property of RealTek and
 *  possession or use of this module requires written permission of RealTek.
 */

// 設計為自動化農業之 IoT gateway
// 設備的configuration, 需要讓它先上網, 需要使用 bluetooth來做第一次設定
// 透過手機, 讓gateway取得wifi連線, 這樣才能跟雲端交換訊息
// 要不然, 只能local端設定
//
// 所有的configuration都是由雲端設定
// 設定好後, 會保存一份備份在flash上面
//
//
#include "FreeRTOS.h"
#include "cmsis_os.h"
#include "task.h"
#include "diag.h"
#include "device.h"
#include "gpio_api.h"   // mbed
#include "timer_api.h"
#include "main.h"
#include "log_uart_api.h"
#include <lwip_netconf.h>
#include "wifi_constants.h"
#include "wifi_structures.h"
#include "lwip_netconf.h"
#include <httpc/httpc.h>
//#include <sntp/sntp.h>
#include "flash_api.h"
//#include "osdep_service.h"
#include "device_lock.h"
#include "apdef.h"
#include <cJSON.h>
#include <httpd/httpd.h>
#include "pages.h"
#include "MQTTClient.h"

//#define __CONCURRENT_MODE__ 1
//========================================================================================================
//  if __CONCURRENT_MODE__ is defined, set the system in STA+AP concurrent mode
//  ### Please note, concurrent mode consume about 36K memory ###
//========================================================================================================
#ifdef __CONCURRENT_MODE__
    #define THIS_WIFI_MODE RTW_MODE_STA_AP
#else
    #define THIS_WIFI_MODE RTW_MODE_STA
#endif
//========================================================================================================
uint8_t g_ReconnectWifi=0;   // 
uint8_t g_NetState=0;   // 0: not connected,  1: local enabled,  2:  wan enabled
//========================================================================================================
#define REQ_THREAD_STACK    400
#define CMD_THREAD_STACK    400
//========================================================================================================
uint8_t JC1278A_Cfg[]= {0xaa, 0x5a, 0x57, 0x04, 0xae, 0x08, 0x00, 0x0f, 0x00, 0x04, 0x00, 0x00, 0x00, 0x06, 0x00, 0x12, 0x00, 0x40} ;
//========================================================================================================
extern const char dev_version[];
TaskHandle_t hReqThread, hSysThread, hCmdThread;
//========================================================================================================
// should save the data to flash memory

sys_profile_t sys_profile;

#define FLASH_APP_BASE   0xF5000
//========================================================================================================
char this_mac[32];  // mac address of this device
struct sFlags sysFlags;
//========================================================================================================
#define g_ReconnectWifi (sysFlags.ReconnectWifi)
#define g_SysTimeCalibrated (sysFlags.SystemTimeCalibrated)
#define g_AliveReceived (sysFlags.AliveReceived)
#define g_ProfileUpdated (sysFlags.ProfileUpdated)
//========================================================================================================
//========================================================================================================
u32 last_alive_time;    // timer for AliveCk
u32 last_req_time;    // timer for command request 
u32 last_cmd_time;    //timer for main loop
u32 MQTT_alive_time;    // timer for MQTT alive response
uint8_t retry_wifi, retry_mqtt, retry_req;
//========================================================================================================
extern QueueHandle_t pSystemQueue;     // cloud request queue, created by main.c
extern QueueHandle_t pCloudCmdQueue;     // command issued by main thread, created by main.c
//extern unsigned int sntp_gen_system_ms(unsigned int *diff, unsigned int *sec, int timezone); // from sntp.c, modified version, return ms tick
extern char *GetSystemTimestamp(char *buf, int fmt);        // main.c
//========================================================================================================
extern void apSendMsg(QueueHandle_t q, unsigned char id, unsigned char param);  // main.c
//========================================================================================================
extern uint8_t global_stop;             // from main.c
//========================================================================================================
time_t Timestamp_to_time_t(char *tm);                   // from main.c
char *time_t_to_Timestamp(char *dst, time_t value);     // from main.c
//========================================================================================================
// retry messqge queue
SemaphoreHandle_t xMutexRetryQueue;
//========================================================================================================
struct sCloudCmdRequest {
    time_t cmd_time;            // 發送command的時間
    u32 retry_time;             // time should retry again
    u32 data;
    uint8_t retry_sec_later;    // retry should be executed after how many seconds
    uint8_t msg;
    uint8_t occupied;
};
//========================================================================================================
#define SIZE_CLOUD_CMD_QUEUE 10
//========================================================================================================
// Allocate 5 elements in retry queue
struct sCloudCmdRequest lstCloudCmdSentQueue[SIZE_CLOUD_CMD_QUEUE];
//========================================================================================================
void SaveCloudQueueRequest(uint8_t msg, u32 data, int retry_sec_later, char *timestamp) {
    int i;
    rtl_printf("Send Clound Queue Command (%u, %s).\n", msg, timestamp);
    xSemaphoreTake(xMutexRetryQueue, portMAX_DELAY);
    // search for duplicate retry Request
    //for (i=0;i<SIZE_CLOUD_CMD_QUEUE;i++) {
    //    if (lstCloudCmdSentQueue[i].occupied==1 && lstCloudCmdSentQueue[i].msg==msg && lstCloudCmdSentQueue[i].data==data) {
    //        xSemaphoreGive(xMutexRetryQueue);
    //        return;
    //    }
    //}
    for (i=0;i<SIZE_CLOUD_CMD_QUEUE;i++) {
        if (lstCloudCmdSentQueue[i].occupied==0)
            break;
    }
    if (i<SIZE_CLOUD_CMD_QUEUE) {
        lstCloudCmdSentQueue[i].cmd_time = Timestamp_to_time_t(timestamp);      // 存下command的時間, 收到時比對
        lstCloudCmdSentQueue[i].retry_time = rtw_get_current_time();
        lstCloudCmdSentQueue[i].retry_sec_later = retry_sec_later;
        lstCloudCmdSentQueue[i].msg = msg;
        lstCloudCmdSentQueue[i].data = data;
        lstCloudCmdSentQueue[i].occupied = 1;
    }
    else {
        rtl_printf("Clound Command Queue overflow (%u, %u)!!!\n", msg, data);
    }
    xSemaphoreGive(xMutexRetryQueue);
}
//========================================================================================================
void CheckCloudQueueTimeout() {
    int i;

    xSemaphoreTake(xMutexRetryQueue, portMAX_DELAY);
    for (i=0;i<SIZE_CLOUD_CMD_QUEUE;i++) {
        if (lstCloudCmdSentQueue[i].occupied==1 && rtw_get_passing_time_ms(lstCloudCmdSentQueue[i].retry_time)>lstCloudCmdSentQueue[i].retry_sec_later*1000) {
            apSendMsg(pCloudCmdQueue, lstCloudCmdSentQueue[i].msg, lstCloudCmdSentQueue[i].data);
            lstCloudCmdSentQueue[i].retry_time = rtw_get_current_time();
            rtl_printf("Clound Command resend (%u, %u)!!!\n", lstCloudCmdSentQueue[i].msg, lstCloudCmdSentQueue[i].data);
            //lstCloudCmdSentQueue[i].occupied = 0;
        }
    }
    xSemaphoreGive(xMutexRetryQueue);
}
//========================================================================================================
void CloudCommandAcked(uint8_t msg, char *timestamp) {
    int i;
    char timebuf[20];
    time_t cmd_time=Timestamp_to_time_t(timestamp);
    // 因為收到的msg ack從大寫變成小寫, 所以要再把msg改成大寫
    msg = msg & (~0x20);
    rtl_printf("Clound Command Queue Acked (%u, %s).\n", msg, timestamp);
    
    xSemaphoreTake(xMutexRetryQueue, portMAX_DELAY);
    for (i=0;i<SIZE_CLOUD_CMD_QUEUE;i++) {
        if (lstCloudCmdSentQueue[i].occupied==1 && lstCloudCmdSentQueue[i].msg==msg && lstCloudCmdSentQueue[i].cmd_time==cmd_time) {
            // 收到Ack, 把該筆註銷
            lstCloudCmdSentQueue[i].occupied = 0;
        }
    }
    xSemaphoreGive(xMutexRetryQueue);
}
//========================================================================================================
//========================================================================================================
//********************************************************************************************************
//  Flash profile routines
//
//
//
//
//********************************************************************************************************
//========================================================================================================
// routines for CRC calculation
uint32_t crc32_for_byte(uint32_t r) {
  for(int j = 0; j < 8; ++j)
    r = (r & 1? 0: (uint32_t)0xEDB88320L) ^ r >> 1;
  return r ^ (uint32_t)0xFF000000L;
}

//========================================================================================================
void crc32(const void *data, size_t n_bytes, uint32_t* crc) {
  //static uint32_t table[0x100];
  //unsigned char *p=(unsigned char *)data;
  *crc=0;
  
  int i;
  unsigned char *p = (unsigned char *)data;
  for (i=0;i<n_bytes;i++,p++) {
      if (i&0x01)
        *crc+=*p;
      else
        *crc+=~(*p);
  }
 /* 
  if(!*table)
    for(size_t i = 0; i < 0x100; ++i)
      table[i] = crc32_for_byte(i);
  for(size_t i = 0; i < n_bytes; ++i)
    *crc = table[(uint8_t)*crc ^ ((uint8_t*)data)[i]] ^ *crc >> 8;
*/    
  rtl_printf("crc32() : Calculated CRC from address:%X,  %d bytes, CRC=%u\r\n", data, n_bytes, *crc);

}
//========================================================================================================
//========================================================================================================
void  save_profile_to_flash() {
    flash_t         flash;
    int loop = 0;
    uint32_t address = FLASH_APP_BASE;
    uint32_t *ptr = (uint32_t *)&sys_profile;
    char *p=(char *)&sys_profile;
    uint32_t crc;
    int total_len=sizeof(sys_profile);

    crc32(p+4, total_len-4, &crc);
    rtl_printf("New CRC=%u!\r\n", crc);
    sys_profile.crc = crc;

    rtl_printf("Write System Profile into Flash!\r\n");
    // erase and write
    //for(loop = 0; loop < total_len/4; loop++)
    //{
    // erase a page, 4096 bytes
        device_mutex_lock(RT_DEV_LOCK_FLASH);
        flash_erase_sector(&flash, address);
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
    //}
    for(loop = 0, ptr = (uint32_t *)&sys_profile; loop < total_len/4; loop++, ptr++)
    {
        device_mutex_lock(RT_DEV_LOCK_FLASH);
        flash_write_word(&flash, address+(loop<<2), *ptr);
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
    }
}
//========================================================================================================
// 之前被更新了device, 所以需要儲存一次, 但是不希望全存, 因為會太久,
// 所以只存入一個device, 要重新算crc
/*
void  save_device_to_flash(uint8_t devid) {
    if (devid>MAX_DEVICE_NUM) {
    rtl_printf("Device[%d] overflow!\r\n", devid-1);
        return;
    }

    flash_t         flash;
    int loop = 0;
    uint32_t address = FLASH_APP_BASE;
    uint32_t *ptr = (uint32_t *)&sys_profile;
    char *p=(char *)&sys_profile;
    uint32_t crc;
    uint32_t dev_offset;
    int total_len=sizeof(sys_profile);

    crc32(p+4, total_len-4, &crc);
    rtl_printf("New CRC=%u!\r\n", crc);
    sys_profile.crc = crc;

    rtl_printf("Write Device[%d] into Flash!\r\n", devid-1);

    dev_offset = 160+sizeof(sDevice)*(devid-1);
    // erase and write first for bytes, it's crc code
    device_mutex_lock(RT_DEV_LOCK_FLASH);
    flash_erase_sector(&flash, address+0);
    flash_write_word(&flash, address+0, *ptr);
    device_mutex_unlock(RT_DEV_LOCK_FLASH);

    // erase and write device slot according to devid
    for(loop = 0; loop < 4; loop++) {
        device_mutex_lock(RT_DEV_LOCK_FLASH);
        flash_erase_sector(&flash, address+dev_offset+(loop<<2));
        flash_write_word(&flash, address+dev_offset+(loop<<2), *(p+dev_offset+(loop<<2)));
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
    }

    rtl_printf("\n===========================================================\n");
    rtl_printf("\nProfile dump (System): \n");
    for (loop=0,p=(char *)&sys_profile;loop<sizeof(sys_profile);loop++, p++) {
        if ((loop&0x0f)==0)
            rtl_printf("\r\n%04X - ", loop);
        rtl_printf("%02X ", *((unsigned char *)p));
    }
    rtl_printf("\r\n");

// test, read again
    for(loop = 0, ptr = (uint32_t *)&sys_profile; loop < sizeof(sys_profile)/4; loop++, ptr++)
    {
		device_mutex_lock(RT_DEV_LOCK_FLASH);
        flash_read_word(&flash, address+(loop<<2), ptr);
		device_mutex_unlock(RT_DEV_LOCK_FLASH);
    }
    rtl_printf("\n===========================================================\n");
    rtl_printf("\nProfile dump (EEPROM): \n");
    for (loop=0,p=(char *)&sys_profile;loop<sizeof(sys_profile);loop++, p++) {
        if ((loop&0x0f)==0)
            rtl_printf("\r\n%04X - ", loop);
        rtl_printf("%02X ", *((unsigned char *)p));
    }
    rtl_printf("\r\n");
    
}
*/
//========================================================================================================
void read_profile_from_flash() {
    flash_t         flash;
    // read 168 bytes = 42 words
    int loop = 0;
    int result = 0;
    uint32_t *ptr = (uint32_t *)&sys_profile;
    uint32_t address = FLASH_APP_BASE;
    char *p=(char *)&sys_profile;

    for(loop = 0, ptr = (uint32_t *)&sys_profile; loop < sizeof(sys_profile)/4; loop++, ptr++)
    {
		device_mutex_lock(RT_DEV_LOCK_FLASH);
        flash_read_word(&flash, address+(loop<<2), ptr);
		device_mutex_unlock(RT_DEV_LOCK_FLASH);
    }

    /*
    rtl_printf("\n===========================================================\n");
    rtl_printf("\nProfile dump (EEPROM read back): \n");
    for (loop=0,p=(char *)&sys_profile;loop<sizeof(sys_profile);loop++, p++) {
        if ((loop&0x0f)==0)
            rtl_printf("\r\n%04X - ", loop);
        rtl_printf("%02X ", *((unsigned char *)p));
    }
    rtl_printf("\r\n");
    */

    uint32_t crc; 
    crc32(((char *)&sys_profile)+4, sizeof(sys_profile)-4, &crc);
    // crc not match, use default settings
    if (sys_profile.crc != crc) {
        rtl_printf("Different CRC : calculated_crc=%u, profile_crc=%u\r\n", crc, sys_profile.crc);
        memset(&sys_profile, 0, sizeof(sys_profile));
        strcpy(sys_profile.dev_ssid,"0000");
        strcpy(sys_profile.dev_pass, "0000");
        strcpy(sys_profile.ota_url, "https//morelohas.com");
        strcpy(sys_profile.ota_file, "/fmupd/ota.bin");
        sys_profile.ota_port=443;     // 443
        memset(&(sys_profile.dev), 0x00, sizeof(sys_profile.dev));
        memset(&(sys_profile.tasks), 0x00, sizeof(sys_profile.tasks));

        // update crc, erase and write
        rtl_printf("Rewrite Flash....\r\n");
        save_profile_to_flash();

        /*
        rtl_printf("\n===========================================================\n");
        rtl_printf("\nProfile dump (After): \n");
        for (loop=0,p=(char *)&sys_profile;loop<sizeof(sys_profile);loop++, p++) {
            if ((loop&0x07)==0)
                rtl_printf("\r\n%04X - ", loop);
            rtl_printf("%02X ", *((unsigned char *)p));
        }
        rtl_printf("\r\n");
        */

        /*
        for(loop = 0; loop < sizeof(sys_profile)/4; loop++)
        {
            device_mutex_lock(RT_DEV_LOCK_FLASH);
            flash_erase_sector(&flash, address+(loop<<2));
            device_mutex_unlock(RT_DEV_LOCK_FLASH);
        }
        for(loop = 0, ptr = (uint32_t *)&sys_profile; loop < sizeof(sys_profile)/4; loop++, ptr++)
        {
            device_mutex_lock(RT_DEV_LOCK_FLASH);
            flash_write_word(&flash, address+(loop<<2), *ptr);
            device_mutex_unlock(RT_DEV_LOCK_FLASH);
        }
        */
        
    }


}

//========================================================================================================
// connect to push server to update the datatime
static void set_sys_time(void)
{
    while (g_NetState<3) {
        // set system time
        MQTT_Publish("{\"c\":\"T\"}");
        //http_request(sys_profile.alive_url, sys_profile.alive_port, "POST", "/GetTime", "", cbGetTime, 0, 3 * 1000 * 60);
        vTaskDelay(2500);
    }
}
//========================================================================================================
//========================================================================================================
// 檢查url開頭
// 如果是http://開頭, 就回傳HTTPC_SECURE_NONE
// 如果是https://開頭, 就回傳HTTPC_SECURE_TLS
// 如果開頭什麼都不是, 就回傳HTTPC_SECURE_NONE
// *url_start_pos 回傳程式使用的url開頭(去掉http的)
uint8_t identify_url(int *url_start_pos, char *url) {
    if (strncmp(url, "http://", 7)==0) {
        *url_start_pos = 7;
        return HTTPC_SECURE_NONE;
    }
    else if (strncmp(url, "https://", 8)==0) {
        *url_start_pos = 8;
        return HTTPC_SECURE_TLS;
    }
    else {
        *url_start_pos = 0;
        return HTTPC_SECURE_NONE;
    }
}


//========================================================================================================
//void start_interactive_mode(void);
//========================================================================================================
//========================================================================================================
void MQTT_Publish(char *msg);
//========================================================================================================
void reactive_wifi() {
    LwIP_DHCP(0, DHCP_STOP);
    vTaskDelay(500);
    wifi_disconnect();
    vTaskDelay(500);
    wifi_off();
    vTaskDelay(500);
    wifi_on(THIS_WIFI_MODE);
}
//========================================================================================================
uint8_t connect_wlan() {
    while (wifi_is_up(RTW_STA_INTERFACE)==0) {
        vTaskDelay(500);
    }
    printf("\n\rConnecting to AP...\n\r");
    wifi_set_autoreconnect(1);

    // default ssid, scan ssid first
    //if (strcmp(sys_profile.dev_ssid,"0000")==0) {
    //    scan_ssid();
    //}
    rtl_printf("Using the Password : %s\n", sys_profile.dev_pass);
    if(wifi_connect(sys_profile.dev_ssid, RTW_SECURITY_WPA2_AES_PSK , sys_profile.dev_pass, strlen(sys_profile.dev_ssid), strlen(sys_profile.dev_pass), -1, NULL) == RTW_SUCCESS)
    {
        printf("\n\rWiFi Connected.\n\r");
        LwIP_DHCP(0, DHCP_START);
    }
    else
    {
        printf("\n\rWiFi Failed.\n\r");
        return 1;
    }
    rtw_wifi_setting_t setting;
    wifi_get_setting(WLAN0_NAME,&setting);
    wifi_show_setting(WLAN0_NAME,&setting);

    wifi_get_mac_address(this_mac);
    printf("\n\rMAC Address is :%s\n\r", this_mac);

    if (wifi_is_ready_to_transceive(RTW_STA_INTERFACE)==RTW_SUCCESS)
        return 0;
    else
        return 2;

}
//========================================================================================================
// MQTT routines
//========================================================================================================
uint8_t global_MQTTMessage[128];
uint8_t MQTT_got_message_size=0;
//========================================================================================================
uint8_t MQTT_connected=0;
MQTTClient mqtt_client;
Network mqtt_network;
MQTTPacket_connectData mqtt_connectData = MQTTPacket_connectData_initializer;
char* sub_topic;
char* pub_topic;
//========================================================================================================
void MQTT_messageArrived(MessageData* data);
//========================================================================================================
uint8_t MQTT_Login()
{
	static unsigned char sendbuf[250], readbuf[250];
    static unsigned char pubtopic[50], subtopic[50];
	int rc = 0, count = 0;
    uint8_t error=0;
	char* address = "35.185.168.76";

    // waiting for wlan_thread to make connection ready
    //while (g_NetState<2) {
    //    vTaskDelay(500);
    //}
    rtl_printf("connecting to MQTT...\n");
    if (MQTT_connected==0) {
        rtl_printf("MQTT broker : %s...\n", address);

        wifi_get_mac_address(this_mac);
        sprintf(subtopic, "PKMQ/V3/%s/Ack", this_mac);  // ack from server
        sprintf(pubtopic, "PKMQ/V3/%s", this_mac);
        sub_topic = subtopic;
        pub_topic = pubtopic;


        NetworkInit(&mqtt_network);
        MQTTClientInit(&mqtt_client, &mqtt_network, 30000, sendbuf, sizeof(sendbuf), readbuf, sizeof(readbuf));

        //mqtt_printf(MQTT_INFO, "Wait Wi-Fi to be connected.");
        //while(wifi_is_ready_to_transceive(RTW_STA_INTERFACE) != RTW_SUCCESS) {
        //    vTaskDelay(2000 / portTICK_PERIOD_MS);
        //}
        //mqtt_printf(MQTT_INFO, "Wi-Fi okay.");
        
        //mqtt_printf(MQTT_INFO, "Connect Network \"%s\"", address);
        mqtt_connectData.username.cstring = "smartway";
        mqtt_connectData.password.cstring = "OraHommn42@";

        if ((rc = NetworkConnect(&mqtt_network, address, 1883)) != 0){
            mqtt_printf(MQTT_INFO, "Failed code from network connect is %d\n", rc);
            //vTaskDelay(2000 / portTICK_PERIOD_MS);
            return 1;
        }
        mqtt_printf(MQTT_INFO, "\"%s\" Connected", address);

        mqtt_connectData.MQTTVersion = 3;
        mqtt_connectData.clientID.cstring = this_mac;

        mqtt_printf(MQTT_INFO, "Start MQTT connection");
        if ((rc = MQTTConnect(&mqtt_client, &mqtt_connectData)) != 0){
            mqtt_printf(MQTT_INFO, "Failed code from MQTT connect is %d\n", rc);
            //vTaskDelay(1000 / portTICK_PERIOD_MS);
            return 2;
        }
        mqtt_printf(MQTT_INFO, "MQTT Connected");

        mqtt_printf(MQTT_INFO, "Subscribe to Topic: %s", sub_topic);
        if ((rc = MQTTSubscribe(&mqtt_client, sub_topic, QOS2, MQTT_messageArrived)) != 0) {
            mqtt_printf(MQTT_INFO, "Failed code from MQTT subscribe is %d\n", rc);
            return 3; //mqtt_printf(MQTT_INFO, "Return code from MQTT subscribe is %d\n", rc);
        }

        //rtl_printf("calibrating system time...  \r\n");
        // set system time
        //MQTT_Publish("{\"c\":\"T\"}");
        //http_request(sys_profile.alive_url, sys_profile.alive_port, "POST", "/GetTime", "", cbGetTime, 0, 3 * 1000 * 60);
        if (error==0) {
            MQTT_connected=1;
        }
    }
    return error;
}

//========================================================================================================
//========================================================================================================
//========================================================================================================
void parseQueryTime(cJSON *IOTJSObject) {
    cJSON *ptimeJSObject;

    ptimeJSObject = cJSON_GetObjectItem(IOTJSObject, "t");
    if(ptimeJSObject && ptimeJSObject->type==cJSON_String) {
        // format good, set time
        if (strlen(ptimeJSObject->valuestring)==17) {
            struct tm timeinfo;
            time_t tt;
            char tmp[6];
            strncpy(tmp, ptimeJSObject->valuestring, 4); tmp[4]=0;      // year
            timeinfo.tm_year = atoi(tmp)-1900;
            strncpy(tmp, ptimeJSObject->valuestring+4, 2); tmp[2]=0;      // month
            timeinfo.tm_mon = atoi(tmp)-1;
            strncpy(tmp, ptimeJSObject->valuestring+6, 2); tmp[2]=0;      // day
            timeinfo.tm_mday = atoi(tmp);
            strncpy(tmp, ptimeJSObject->valuestring+8, 2); tmp[2]=0;      // hour
            timeinfo.tm_hour = atoi(tmp);
            strncpy(tmp, ptimeJSObject->valuestring+10, 2); tmp[2]=0;      // min
            timeinfo.tm_min = atoi(tmp);
            strncpy(tmp, ptimeJSObject->valuestring+12, 2); tmp[2]=0;      // sec
            timeinfo.tm_sec = atoi(tmp);
            timeinfo.tm_isdst=0;
            tt = mktime(&timeinfo);
            rtc_write(tt);
            //g_NetState=3;
            rtl_printf("system time calibrated ...  \r\n");
            g_SysTimeCalibrated=1;
        }   // if (strlen(ptimeJSObject->valuestring)==17)
    }
        
}

//========================================================================================================
// MQTT訊息進來
void MQTT_messageArrived(MessageData* data)
{
    //int len=127;
	//mqtt_printf(MQTT_INFO, "Message arrived on topic %.*s: %.*s\n", data->topicName->lenstring.len, data->topicName->lenstring.data,
	//	data->message->payloadlen, (char*)data->message->payload);

    char *buf = (char*)data->message->payload;
    if (buf==NULL)
        return;
    buf[data->message->payloadlen]=0;
    rtl_printf("Message Got : %s\r\n", buf);

    cJSON_Hooks memoryHook;
    memoryHook.malloc_fn = malloc;
    memoryHook.free_fn = free;
    cJSON_InitHooks(&memoryHook);
    cJSON *IOTJSObject, *cmdJSObject;

    if((IOTJSObject = cJSON_Parse(buf)) != NULL) {
        // get the command
        cmdJSObject = cJSON_GetObjectItem(IOTJSObject, "c");
        if(cmdJSObject && cmdJSObject->type==cJSON_String) {
            //-----------------------------------------------------------------
            // parse command
            switch (cmdJSObject->valuestring[0]) {
                case 'a':
                    // alive response, no need for the future
                    g_AliveReceived = 1;
//                    last_alive_time = rtw_get_current_time();
                    break;
                case 'b':
                    {   // 收到回覆, 要處理Ack, 把queue消去
                        cJSON *timeJSObject = cJSON_GetObjectItem(IOTJSObject, "t");
                        if (timeJSObject && timeJSObject->type==cJSON_String) {
                            CloudCommandAcked(cmdJSObject->valuestring[0], timeJSObject->valuestring);
                        }
                    }
                    break;
                case 'e':
                    {   // 收到回覆, 要處理Ack, 把queue消去
                        cJSON *timeJSObject = cJSON_GetObjectItem(IOTJSObject, "t");
                        if (timeJSObject && timeJSObject->type==cJSON_String) {
                            CloudCommandAcked(cmdJSObject->valuestring[0], timeJSObject->valuestring);
                        }
                    }
                    break;
                case 'o':
                    // Open Door / 開門
                    printf("MSG_EVENT_TRANSACTION_OK_OPEN Received.\r\n");
                    apSendMsg(pSystemQueue, MSG_EVENT_TRANSACTION_OK_OPEN, 0);
                    // 回覆open
                    MQTT_Publish("{\"c\":\"O\"}");
                    break;
                case 'r':
                    // RESET / 重開機
                    printf("MSG_EVENT_REBOOT Received.\r\n");
                    apSendMsg(pSystemQueue, MSG_EVENT_REBOOT, 0);
                    // 回覆
                    MQTT_Publish("{\"c\":\"R\"}");
                    break;
                case 's':
                    // 重設state
                    // {"c":"s","s":100}
                    {
                        cJSON *valueJSObject = cJSON_GetObjectItem(IOTJSObject, "v");
                        if (valueJSObject && valueJSObject->type==cJSON_Number) {
                            printf("MSG_EVENT_CHANGE_STATE Received. value=%d\r\n", valueJSObject->valueint);
                            apSendMsg(pSystemQueue, MSG_EVENT_CHANGE_STATE, valueJSObject->valueint);
                        }
                        // 回覆
                        MQTT_Publish("{\"c\":\"S\"}");
                    }
                    break;
                case 't':
                    // query Time response
                    parseQueryTime(IOTJSObject);
                    break;
                case 'u':
                    // OTA update
                    // {"c":"u","v":1}
                    {
                        cJSON *valueJSObject = cJSON_GetObjectItem(IOTJSObject, "v");
                        if (valueJSObject && valueJSObject->type==cJSON_Number) {
                            printf("MSG_EVENT_UPGRADE_FIRMWARE Received. value=%d\r\n",valueJSObject->valueint);
                            apSendMsg(pSystemQueue, MSG_EVENT_UPGRADE_FIRMWARE, valueJSObject->valueint);
                        }
                        else {
                            printf("MSG_EVENT_UPGRADE_FIRMWARE Received. \r\n");
                            apSendMsg(pSystemQueue, MSG_EVENT_UPGRADE_FIRMWARE, 0);
                        }
                        // 回覆
                        MQTT_Publish("{\"c\":\"U\"}");
                    }
                    break;
                    
            }
            //-----------------------------------------------------------------
        }
        else {
            rtl_printf("Bad JSON format: %s\r\n", buf);
            cJSON_Delete(IOTJSObject);
            return;
        }
            
        cJSON_Delete(IOTJSObject);
    }

    /*
    if (data->message->payloadlen>127)
        len=data->message->payloadlen;
    memcpy(global_MQTTMessage, data->message->payload, len);
    */
}
//========================================================================================================
// command 格式固定為jsom
// command送出後, 把它queue起來, 後續追蹤是否收到ack
// {
//      "c" : "x",      // command
//      "s" : "12",     // parameter
//      "t" : "yyyymmddhhmmsszzz"   // timestamp
// }
//========================================================================================================
void MQTT_Publish_Command(uint8_t cmd, uint8_t param) {
    char status_cmd[50];
    char timestamp[30];
    GetSystemTimestamp(timestamp, 0);
    sprintf(status_cmd, "{\"c\":\"%c\",\"s\":\"%u\",\"t\":\"%s\"}", cmd, param, timestamp);
    MQTT_Publish(status_cmd);
    SaveCloudQueueRequest(cmd, param, 6, timestamp);
}
//========================================================================================================
// 從CloudCmdQueue取出其它thread送來的command
void checkCloudCmdQueue() {
    struct tMessage sMsg;
    if( xQueueReceive( pCloudCmdQueue, &( sMsg ), ( TickType_t ) 1 ) == pdPASS ) {
        rtl_printf("Cloud Message Received: %x (%d)\r\n", sMsg.ucMsg, sMsg.ucParam);
        if (sMsg.ucMsg==MSG_EVENT_CLOUD_REQUEST_BIKESTATUS) {
            // BikeStatus
            MQTT_Publish_Command('B', sMsg.ucParam);
        }
        // sys_profile already updated, write to flash memory
        else if (sMsg.ucMsg==MSG_EVENT_SAVE_SYS_PROFILE_TO_FLASH) {
            rtl_printf("Save Flash....\r\n");
            save_profile_to_flash();
        }
        else if (sMsg.ucMsg==MSG_EVENT_SNTP_SET_RTC) {
            if (g_NetState==3) {
                rtl_printf("SNTP set time.\r\n");
                set_sys_time();
            }
            else {
                rtl_printf("SNTP Failed, network not ready!\r\n");
            }
        }
        else if (sMsg.ucMsg==MSG_EVENT_RESTART_NETWORK) {
            g_ReconnectWifi=1;
        }
        else if (sMsg.ucMsg==MSG_EVENT_CLOUD_REQUEST_ERROR) {
            MQTT_Publish_Command('E', sMsg.ucParam);
            //char status_cmd[50], timestamp[30];
            //sprintf(status_cmd, "{\"c\":\"E\",\"s\":\"%u\",\"t\":\"%s\"}", sMsg.ucParam, GetSystemTimestamp(timestamp, 0));
            //MQTT_Publish(status_cmd);
        }
    }
}
//========================================================================================================
void MQTT_Publish(char *msg) {
    MQTTMessage message;
    char payload[100];
    int retry=0;
    rtl_printf("Pub Data: %s\r\n", msg);
    message.qos = QOS1;
    message.retained = 0;
    message.payload = msg;
    message.payloadlen = strlen(msg);
    if (MQTTPublish(&mqtt_client, pub_topic, &message) != 0) {
        mqtt_printf(MQTT_INFO,"MQTT publish failed!\n");
    }
}
//========================================================================================================

extern uint8_t gIgnoreNetStateMachine;
//========================================================================================================
void network_thread(void* data) {
    
    uint8_t wifi_status;
    uint8_t last_state;

    // 狀態從0開始
    g_NetState=0;
    read_profile_from_flash();

    g_ProfileUpdated=0;
    retry_wifi=0;
    retry_mqtt=0;

    // 先initial wifi peripheral
    LwIP_Init();
    wifi_manager_init();
    wifi_on(THIS_WIFI_MODE);
    wifi_set_autoreconnect(1);

    // 網路狀態機
    while (global_stop==0) {

        if (gIgnoreNetStateMachine) {
            vTaskDelay(500);
            continue;
        }
        // 存下上個狀態
        last_state = g_NetState;

        switch(g_NetState) {
            case 0:
                // 如果開機時ssid就是0000, 沒設帳密, 等待它從bluetooth設定成功
                if (strcmp(sys_profile.dev_ssid,"0000")==0) {
                    vTaskDelay(10000);
                    g_NetState=0;
                }
                else {
                    wifi_status=connect_wlan();
                    rtl_printf("(0)connect_wlan returned : %d\n", wifi_status);
                    g_NetState=1;
                }
                break;
            case 1:                         // Wifi初始中
                // wifi初始成功
                if (wifi_status==0) {
                    retry_wifi=0;
                    retry_mqtt=0;
                    MQTT_Login();
                    g_NetState=2;
                }
                else {
                    g_NetState=101;
                    // wifi初始失敗
                    rtl_printf("network lost : reconnecting...\r\n");
                }
                break;
            case 2:             // Wifi 初始成功, 連線MQTT
                if (MQTT_connected==1) {
                    // MQTT success, 傳送對時
                    // set system time
                    g_SysTimeCalibrated=0;
                    // 傳送對時
                    MQTT_Publish("{\"c\":\"T\"}");
                    last_req_time = rtw_get_current_time();
                    retry_req=0;
                    MQTT_alive_time = rtw_get_current_time();
                    g_NetState=3;
                }
                else {
                    g_NetState=201;
                }
                break;
            case 3:             // 基礎網路成功, MQTT對時中
                if (g_SysTimeCalibrated!=1 && rtw_get_passing_time_ms(last_req_time)>5000) {
                    // 送command逾時未收到Ack
                    if (retry_req>=5) {
                        // 重試對時 5次
                        retry_mqtt++;
                        g_NetState=201;
                    }
                    else {
                        //重送對時, 記錄last_req_time, retry_req++
                        last_req_time = rtw_get_current_time();
                        retry_req++;
                        g_SysTimeCalibrated=0;
                        // 重送對時
                        MQTT_Publish("{\"c\":\"T\"}");
                        g_NetState=3;
                    }
                }
                else {
                    // 未逾時
                    //記錄last_alive_time,
                    last_alive_time = rtw_get_current_time();
                    //記錄last_req_time
                    last_req_time = rtw_get_current_time();
                    MQTT_alive_time = rtw_get_current_time();
                    g_NetState=4;
                }
                break;
            case 4:             // MQTT loop
                // 處理Cloud Cmd Queue
                checkCloudCmdQueue();
                // 處理Cloud Cmd Queue重送
                CheckCloudQueueTimeout();
                // MQTT_messageArrived()是callback, 會處理CloudCmdQueue Ack與消去的事宜
                //
                // loop判斷開始
                if (g_AliveReceived==1) {
                    g_AliveReceived=0;
                    MQTT_alive_time = rtw_get_current_time();
                }
                if (g_ProfileUpdated==1) {
                    save_profile_to_flash();
                    g_ProfileUpdated=0;
                }
                if (rtw_get_passing_time_ms(last_alive_time)>60000) {
                    // 超過60秒, 發送Alive
                    // 送MQTT Alive
                    // 記錄last_alive_time
                    char status_cmd[50], timestamp[30];
                    sprintf(status_cmd, "{\"c\":\"A\",\"t\":\"%s\"}", GetSystemTimestamp(timestamp, 0));
                    MQTT_Publish(status_cmd);
                    last_alive_time = rtw_get_current_time();
                }
                else if (rtw_get_passing_time_ms(MQTT_alive_time)>60000 * 3) {
                    // wifi斷線, wifi重連, 
                    retry_wifi=0;
                    // close wifi, and reconnect it
                    reactive_wifi();
                    wifi_status=connect_wlan();
                    rtl_printf("(4)connect_wlan returned : %d\n", wifi_status);
                    g_NetState=1;
                }
                break;
            case 101:           // Wifi初始失敗
                if (retry_wifi<20) {
                    // sleep 2 seconds
                    vTaskDelay(2000);
                    retry_wifi++;
                    // close wifi, and reconnect it
                    reactive_wifi();
                    wifi_status=connect_wlan();
                    rtl_printf("(101)connect_wlan returned : %d\n", wifi_status);
                    g_NetState=1;
                }
                else {
                    // 重啟
                    apSendMsg(pSystemQueue, MSG_EVENT_REBOOT, 0);
                }
                break;
            case 201:
                if (retry_mqtt<5) {
                    vTaskDelay(2000);
                    retry_mqtt++;
                    // reconnect needed
                    MQTT_connected=0;
                    MQTTUnsubscribe(&mqtt_client, sub_topic);
                    MQTTDisconnect(&mqtt_client);
                    MQTT_Login();
                    g_NetState=2;
                }
                else {
                    retry_wifi=0;
                    reactive_wifi();
                    vTaskDelay(1000);
                    wifi_status=connect_wlan();
                    rtl_printf("(201)connect_wlan returned : %d\n", wifi_status);
                    g_NetState=1;
                }
                break;
            default:
                break;
        }
        // MQTT connected, 就執行yield
        if (MQTT_connected==1) {
            MQTTYield(&mqtt_client, 2000);
        }
        vTaskDelay(100);
    }
	/* Kill init thread after all init tasks done */
	vTaskDelete(NULL);
    
}

//========================================================================================================
void net_main(void) {

    memset(lstCloudCmdSentQueue, 0, sizeof(lstCloudCmdSentQueue));

    //g_NetState=0;

//    xMutexNetState = xSemaphoreCreateMutex();
    xMutexRetryQueue = xSemaphoreCreateMutex();

//    gtimer_init(&retry_bikestatus_timer, TIMER1);
//    gtimer_init(&retry_errstatus_timer, TIMER4);        // timer2, timer3跟pwm相衝,  還不清楚規律為何?

    //read_profile_from_flash();

    // ----------------------------------------------------
	// wlan intialization 
    //printf("\n\rwlan intialization\n\r", __FUNCTION__);

	if(xTaskCreate(network_thread, ((const char*)"net_init"), 512, NULL, tskIDLE_PRIORITY + 3 + PRIORITIE_OFFSET, &hSysThread) != pdPASS)
        printf("\n\r%s xTaskCreate(wlan_thread) failed", __FUNCTION__);

    //
    //
    // continuously ping cloud server to make sure the network is okay
	//if(xTaskCreate(wlan_thread, ((const char*)"net_init"), 512, NULL, tskIDLE_PRIORITY + 3 + PRIORITIE_OFFSET, &hSysThread) != pdPASS)
	//	printf("\n\r%s xTaskCreate(wlan_thread) failed", __FUNCTION__);

    // long request
	//if(xTaskCreate(net_req_thread, ((const char*)"req_thread"), REQ_THREAD_STACK, NULL, tskIDLE_PRIORITY + 3 + PRIORITIE_OFFSET, &hReqThread) != pdPASS)
	//printf("\n\r%s xTaskCreate(req_thread) failed", __FUNCTION__);

    //printf("\n\net_cmd_thread intialization\n\r", __FUNCTION__);
    // receive command from other thread
	//if(xTaskCreate(net_cmd_thread, ((const char*)"cmd_thread"), CMD_THREAD_STACK, NULL, tskIDLE_PRIORITY + 3 + PRIORITIE_OFFSET, &hCmdThread) != pdPASS)
	//	printf("\n\r%s xTaskCreate(cmd_thread) failed", __FUNCTION__);
    


}

