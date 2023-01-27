#ifndef __APDEF_H__
#define __APDEF_H__

//========================================================================================================
// Version Switch
//#define __PARKEY_VER_3__    1
#define __PARKEY_VER_2__    1
//======================================================
#define DEV_VERSION "0.0.3"
//======================================================
/*
#define MSG_EVENT_USER_BTN              0x01
#define MSG_EVENT_MOTOR_OPEN_POS        0x02
#define MSG_EVENT_MOTOR_CLOSE_POS       0x03
#define MSG_EVENT_RADAR_DETECTED        0x04
*/
#define MSG_EVENT_TRANSACTION_OK_OPEN   0x05
#define MSG_EVENT_GATE_BLOCKED          0x06
#define MSG_EVENT_SHOW_TIME             0x07
#define MSG_EVENT_SYSTEM_ERROR          0x08
#define MSG_EVENT_CHANGE_STATE          0x09
#define MSG_EVENT_REBOOT                0x0a
#define MSG_EVENT_UPGRADE_FIRMWARE      0x0b
//======================================================
#define MSG_GPIO_INDICATOR_ON           0x10
#define MSG_GPIO_INDICATOR_OFF          0x11
#define MSG_GPIO_LOCK                   0x12
#define MSG_GPIO_UNLOCK                 0x13
/*
#define MSG_GPIO_MOTOR_FORWARD          0x10
#define MSG_GPIO_MOTOR_BACKWARD         0x11
#define MSG_GPIO_MOTOR_STOP             0x12
#define MSG_GPIO_EMULATE_DETECTION      0x13
#define MSG_GPIO_SOLENOID_LOCK          0x14
#define MSG_GPIO_SOLENOID_UNLOCK        0x15
*/
//======================================================
#define MSG_EVENT_CLOUD_REQUEST_BIKESTATUS         0x20
#define MSG_EVENT_SAVE_SYS_PROFILE_TO_FLASH        0x22
#define MSG_EVENT_SNTP_SET_RTC                     0x23
#define MSG_EVENT_RESTART_NETWORK                  0x24
#define MSG_EVENT_CLOUD_REQUEST_ERROR              0x25 
//======================================================
#define ERROR_MOTOR_FORWARD_ERROR       0x01
#define ERROR_MOTOR_BACKWARD_ERROR      0x02
#define ERROR_OBJECT_DETECTION_ERROR    0x03
#define ERROR_PARKIN_GATE_NOT_CLOSED    0x04
#define ERROR_OBJECT_DISAPPEARED        0x05
#define ERROR_BIKEIN_ERROR              0x06
#define ERROR_BIKEOUT_ERROR             0x07
//======================================================
// system encounter message querying infinite loop or other uncovered error
#define MSG_EVENT_SYS_UNSTABLE          0xff 
//======================================================
#define APSENDMSG(q, x) while (xQueueSend(q, ( void * ) &x, ( TickType_t ) 0 )!=pdTRUE) vTaskDelay(20);
//======================================================
// maximum mili-seconds for motor activation
#define MOTOR_MAX_MSEC      10000
//======================================================
#define OBJ_EXIST_VALUE     900
//======================================================
#define OBJ_EXIST_TIME     10
//======================================================
struct tMessage
{
   unsigned char ucMsg;
   unsigned char ucParam;
};

//======================================================
struct sTaskTime {
    unsigned char wday;         // bit assigned, 星期幾
    unsigned short begintime;   // number of hhmm
    unsigned char reserved;
};       // 4 bytes;
//======================================================
struct sDevice {
    unsigned char devid;            // device id from lora network
    unsigned char duration;         // on duration in minutes
    unsigned char dev_state;        // 0: off, 1: on, 2: depend on sensor, 3: reset mode
    unsigned char dev_type;         // dev_type: 0 : 未定義 ,1 : 水閥門設備 ,2 : 智慧閥門設備 ,3 : ON/OFF Button Sensor ,4 : 開關設備
};      // 4 bytes
//======================================================
struct sys_profile_t {
    uint32_t crc;               // Offset: 0
    char dev_ssid[36];          // Offset: 4
    char dev_pass[36];          // Offset: 40
    char req_url[60];           // Offset: 76
    char ota_url[60];           // Offset: 136
    char ota_file[60];          // Offset: 196
    uint16_t req_port;          // Offset: 256  - (443)
    uint16_t ota_port;          // Offset: 258
    uint8_t Server_Mode;        // Offset: 260  - 0 : wifi ap+dhcpserver+webserv, 1 : connected to another wifi+JC1278A lora Server
    uint8_t count_Task;         // Task count
    uint8_t count_Dev;          // dev count
    uint8_t reserved;
    struct sTaskTime TaskTime[10];      // Offset: 264, Length : 40  -  10 sets of task
    struct sDevice dev[255];            // Offset: 304, Length : 1020
    uint16_t gatewayid;         // gatewayid for this gateway, after registration, the gateway get it's id
};      // 1328 bytes
//======================================================
extern char *GetSystemTimestamp(char *buf, int fmt);    // main.c
extern void apSendMsg(QueueHandle_t q, unsigned char id, unsigned char param);  // main.c

#endif
