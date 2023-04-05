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
//

//======================================================
struct tMessage
{
   unsigned char ucMsg;
   unsigned char ucParam;
};

//======================================================
typedef struct {
    unsigned char devid;
    unsigned short wday : 4;
    unsigned short begintime : 12;
    unsigned char duration;     // duration in minutes
    unsigned char param1;       //  parameter 1
    unsigned char param2;       //  parameter 2
    unsigned char param3;       //  parameter 3
    unsigned char param4;       //  parameter 4
} sDevTask; // 8 bytes
//======================================================
typedef struct {
    unsigned char devid;            // device id from lora network
    unsigned char dev_type;         // dev_type: 0 : 未定義 ,1 : 水閥門設備 ,2 : 智慧閥門設備 ,3 : ON/OFF Button Sensor ,4 : 開關設備
    unsigned char dev_state;        // 0: off, 1: on, 2: depend on sensor, 3: reset mode
    unsigned char reserved;         // 
    unsigned char uid[12];
} sDevice;      // 16 bytes
//======================================================
#define MAX_DEVICE_NUM  30
#define MAX_TASK_NUM  60
//======================================================
typedef struct {
    uint32_t crc;               // Offset: 0
    char dev_ssid[36];          // Offset: 4
    char dev_pass[36];          // Offset: 40
    char ota_url[60];           // Offset: 76
    char ota_file[20];          // Offset: 136
    uint16_t ota_port;          // Offset: 156
    uint8_t count_Dev;          // Device count Offset : 158
    uint8_t count_Task;         // Task count   Offset : 159
    sDevice dev[MAX_DEVICE_NUM];           // Offset: 160, Length : 480
    sDevTask tasks[MAX_TASK_NUM];        // Offset: 640, Length : 480
    uint32_t reversed;
} sys_profile_t;      // 1120 bytes
//======================================================
extern char *GetSystemTimestamp(char *buf, int fmt);    // main.c
extern void apSendMsg(QueueHandle_t q, unsigned char id, unsigned char param);  // main.c
//======================================================
struct sFlags {
    uint8_t ReconnectWifi : 1;
    uint8_t SystemTimeCalibrated : 1;
    uint8_t AliveReceived : 1;
    uint8_t ProfileUpdated : 1;
    uint8_t JC1278A_setting : 1;
    uint8_t Packet_received : 1;
    uint8_t Reserved : 2;
} ;
//======================================================
//配對 / 解除配對, gateway指定某個device為旗下的device
//gateway必須指定device 12bytes的uid, 被指定的device就會成為client,
//後續就用id來辨識device,
//因為id為1-byte, 所以gateway最多可連結255個device

struct sPacket_Match {
    uint8_t head;           // must be "P"
    uint8_t id;             // new id for the device
    uint8_t cmd;            // G: 配對, K: 解除配對
    uint8_t length;         // = 17
    uint8_t uid[12];        // device's uid
    uint8_t cksum;          // 
};

// client回覆配對, 
struct sPacket_Match_Ack {
    uint8_t head;           // "P"
    uint8_t id;             // id of the device
    uint8_t cmd;            // g: 配對, k: 解除配對
    uint8_t length;         // = 6
    uint8_t dev_type;       // device type
    uint8_t cksum;          // 
};

// gateway回覆配對Ack, 
struct sPacket_Match_AckAck {
    uint8_t head;           // "P"
    uint8_t id;             // id of the device
    uint8_t cmd;            // g: 配對, k: 解除配對
    uint8_t length;         // = 5
    uint8_t cksum;          // 
};
//=================================================================================
//詢問狀態, gateway會輪流詢問device目前狀態

struct sPacket_Query {
    uint8_t head;           // must be "P"
    uint8_t id;             // id of the device
    uint8_t cmd;            // Q
    uint8_t length;         // = 5
    uint8_t cksum;          // 
};

// client回覆Query, 
struct sPacket_Query_Ack {
    uint8_t head;           // "P"
    uint8_t id;             // id of the device
    uint8_t cmd;            // q
    uint8_t length;         // = 15
    uint8_t data[10];       // data from device's response, the length is variable and depends on device type
    uint8_t cksum;          // 
};

// gateway回覆Query Ack, 
struct sPacket_Query_AckAck {
    uint8_t head;           // "P"
    uint8_t id;             // id of the device
    uint8_t cmd;            // q
    uint8_t length;         // = 5
    uint8_t cksum;          // 
};

//======================================================
// device parameters(registers)
//
//      WATERGATE       SMARTGATE       BTNSENSOR       RELAY
// 0      閥狀態         閥狀態          BTN狀態      RELAY狀態
// 1     功能狀態       功能狀態
// 2
// 3
// 4
// 5
// 6
// 7
// 8
// 9
//
//======================================================
//=================================================================================
//設定parameters, gateway會依Service Logic發送parameters給client device


struct sPacket_Set {
    uint8_t head;           // must be "P"
    uint8_t id;             // id of the device
    uint8_t cmd;            // S
    uint8_t length;         // = 15
    uint8_t data[10];       // parameters, the length is variable and depends on device type
    uint8_t cksum;          // 
};

// client回覆Set, 
struct sPacket_Set_Ack {
    uint8_t head;           // "P"
    uint8_t id;             // id of the device
    uint8_t cmd;            // s
    uint8_t length;         // = 5
    uint8_t cksum;          // summary of bytes from id to data, get the next ramdom seed and get the lowest byte
};

// gateway回覆Set Ack, 
struct sPacket_Set_AckAck {
    uint8_t head;           // "P"
    uint8_t id;             // id of the device
    uint8_t cmd;            // s
    uint8_t length;         // = 5
    uint8_t cksum;          // summary of bytes from id to data, get the next ramdom seed and get the lowest byte
};

//======================================================


//======================================================
//======================================================
//======================================================
//======================================================
#endif

