/*
 *  Routines to access hardware
 *
 *  Copyright (c) 2013 Realtek Semiconductor Corp.
 *
 *  This module is a confidential and proprietary property of RealTek and
 *  possession or use of this module requires written permission of RealTek.
 */
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
#include "nfc_api.h"
#include <httpc/httpc.h>
#include "LiquidCrystal_I2C.h"

log_uart_t uobj;

#define GPIO_LED_PIN1       PC_0
#define GPIO_LED_PIN2       PC_1

extern void console_init(void);
extern unsigned char do_ping_call(char *ip, int loop, int count);
/**
  * @brief  Main program.
  * @param  None
  * @retval None
  */
//int main_app(IN u16 argc, IN u8 *argv[])

//========================================================================================================
gtimer_t my_timer1;
gtimer_t global_timer;
gpio_t gpio_led1;
volatile uint32_t global_tick=0;

#define NFC_MAX_PAGE_NUM 36
//unsigned int  nfc_tag_content[NFC_MAX_PAGE_NUM];
uint8_t nfc_read_ready=0;
uint8_t nfc_write_ready=0;

nfctag_t nfcobj;
uint32_t nfcvalue;

uint8_t g_NetState=0;   // 0: not connected,  1: local enabled,  2:  wan enabled
SemaphoreHandle_t xMutexNetState;
//========================================================================================================
void timer1_timeout_handler(uint32_t id) {
    gpio_t *gpio_led = (gpio_t *)id;

    gpio_write(gpio_led, !gpio_read(gpio_led));    
}

//========================================================================================================
void global_timer_timeout_handler(uint32_t id) {
    global_tick++;
}

//========================================================================================================
//  wireless LAN thread, connect to AP
//  hard coded SSID and password
void wlan_thread(void* in_id) {
	char *id = in_id;
    char mac[32];
    while (wifi_is_up(RTW_STA_INTERFACE)==0) {
        vTaskDelay(500);
    }
    printf("\n\rConnecting to AP...\n\r");
    if(wifi_connect("TP-Link_09B1", RTW_SECURITY_WPA2_AES_PSK , "0930710512", 12, 10, -1, NULL) == RTW_SUCCESS)
    {
        printf("\n\rWiFi Connected.\n\r");
        LwIP_DHCP(0, DHCP_START);
    }
    else
    {
        printf("\n\rWiFi Failed.\n\r");
    }
	rtw_wifi_setting_t setting;
	wifi_get_setting(WLAN0_NAME,&setting);
	wifi_show_setting(WLAN0_NAME,&setting);

    wifi_get_mac_address(mac);
    printf("\n\rMAC Address is :%s\n\r", mac);

    //  checking network,  !!! it should be checking constantly
    while (g_NetState<2) {
        if (do_ping_call("35.185.168.76", 0, 1)<0) {
            if( xSemaphoreTake( xMutexNetState, ( TickType_t ) 10 ) == pdTRUE ) {
                g_NetState = 1;
                xSemaphoreGive(xMutexNetState);
            }
            printf("\n\rPing Failed.. Network Error\n\r");
        }
        else {
            if( xSemaphoreTake( xMutexNetState, ( TickType_t ) 10 ) == pdTRUE ) {
                g_NetState = 2;
                xSemaphoreGive(xMutexNetState);
            }
        }
        vTaskDelay(2000);
    }

	/* Kill init thread after all init tasks done */
	vTaskDelete(NULL);

}
//========================================================================================================
void nfc_write_listener(void *arg, unsigned int page, unsigned int pgdat) {
    nfc_write_ready=1;
    printf("\n\rNFC Write Event[%d]=%X : \n\r", page, pgdat);
    //nfc_tag_content[page] = pgdat;
    //nfc_tag_dirty[page] = 1;
    //if (nfc_tid) {
    //    osSignalSet(nfc_tid, NFC_EV_WRITE);
    //}
}
//========================================================================================================
void nfc_read_listener(void *arg, void *buf, unsigned int page) {
    printf("\n\rNFC Read Event[%d] : \n\r", page);
    //if (nfc_tid) {
    //    osSignalSet(nfc_tid, NFC_EV_READ);
    //}
/*    
    int i;
    printf("\n\rNFC Event[%d] : ", page);
    for(i=0;i<32;i++) {
        printf("%02X ", ((unsigned char *)buf)[i]);
    }
    printf("\n\r");
*/    
}
//========================================================================================================
void nfc_event_listener(void *arg, unsigned int event) {
    switch(event) {
        case NFC_EV_READER_PRESENT:
            printf("\r\nNFC_EV_READER_PRESENT\r\n");
            nfc_read_ready=1;
            break;
        case NFC_EV_READ:
            printf("\r\nNFC_EV_READ\r\n");
            break;
        case NFC_EV_WRITE:
            printf("\r\nNFC_EV_WRITE\r\n");
            break;
        case NFC_EV_ERR:
            printf("\r\nNFC_EV_ERR\r\n");
            break;
        case NFC_EV_CACHE_READ:
            printf("\r\nNFC_EV_CACHE_READ\r\n");
            break;
    }
}
//========================================================================================================
int is_valid_nfc_uid(unsigned int *nfc_tag_content) {
    int valid_content = 1;

    unsigned char uid[7];
    unsigned char bcc[2];

    uid[0] = (unsigned char)((nfc_tag_content[0] & 0x000000FF) >>  0);
    uid[1] = (unsigned char)((nfc_tag_content[0] & 0x0000FF00) >>  8);
    uid[2] = (unsigned char)((nfc_tag_content[0] & 0x00FF0000) >> 16);
    bcc[0] = (unsigned char)((nfc_tag_content[0] & 0xFF000000) >> 24);
    uid[3] = (unsigned char)((nfc_tag_content[1] & 0x000000FF) >>  0);
    uid[4] = (unsigned char)((nfc_tag_content[1] & 0x0000FF00) >>  8);
    uid[5] = (unsigned char)((nfc_tag_content[1] & 0x00FF0000) >> 16);
    uid[6] = (unsigned char)((nfc_tag_content[1] & 0xFF000000) >> 24);
    bcc[1] = (unsigned char)((nfc_tag_content[2] & 0x000000FF) >>  0);

    // verify Block Check Character
    if (bcc[0] != (0x88 ^ uid[0] ^ uid[1] ^ uid[2])) {
        valid_content = 0;
    }
    if (bcc[1] != (uid[3] ^ uid[4] ^ uid[5] ^ uid[6])) {
        valid_content = 0;
    }

    return valid_content;
}
//========================================================================================================
void nfc_thread(void* arg) {
    unsigned int  nfc_tag_content[NFC_MAX_PAGE_NUM];
    osEvent evt;
    /*
    // for test, set LCD screen
    LiquidCrystal_I2C_LiquidCrystal_I2C(0x27,16,2);
    LiquidCrystal_I2C_init();
    LiquidCrystal_I2C_backlight();
    LiquidCrystal_I2C_setCursor(0,0);
    LiquidCrystal_I2C_print("Hello World");
    */

    // ----------------------------------------------------
    // Initial NFC Reader
    printf("\n\rInitializing NFC ...\n\r");
    nfc_init(&nfcobj, nfc_tag_content);
    nfc_event(&nfcobj, nfc_event_listener, NULL, 0xFF);
    printf("\n\rNFC Reading...[%x]\n\r", global_tick);    
    memset(nfc_tag_content, 0, sizeof(nfc_tag_content));
    nfc_write(&nfcobj, nfc_write_listener, NULL);
    nfc_read(&nfcobj, nfc_read_listener, NULL);
    //osSignalClear(nfc_tid, NFC_EV_READ);
    
    while (1) {
        vTaskDelay(1500);
        if (nfc_read_ready>0) {
            nfc_read_ready=0;
            nfc_read(&nfcobj, nfc_read_listener, NULL);
            printf("\n\rNFC Reading...[%x]\n\r", global_tick);
        }
        if (nfc_write_ready>0) {
            nfc_write_ready=0;
            nfc_cache_write(&nfcobj, &(nfc_tag_content[4]), 4, 1);
            printf("\n\rNFC Writing...[%x]\n\r", global_tick);
        }
        //nfc_read(&nfcobj, my_nfc_event_cb, NULL);/
        /*
        evt = osSignalWait (0, 0xFFFFFFFF); // wait for any signal with max timeout
        if (evt.status == osEventSignal && (evt.value.signals & NFC_EV_READ)) {
            printf("\n\rNFC Reading...[%x]\n\r", global_tick);
            osDelay(300);
            osSignalClear(nfc_tid, NFC_EV_READ);
        }
        */
    }
	/* Kill init thread after all init tasks done */
	vTaskDelete(NULL);
}
//========================================================================================================
uint8_t get_g_NetState() {
    uint8_t NetState;
    if( xSemaphoreTake( xMutexNetState, ( TickType_t ) 10 ) == pdTRUE ) {
        NetState = g_NetState;
        xSemaphoreGive(xMutexNetState);
    }
    else
        NetState=0;
    return NetState;
}
//========================================================================================================
void httpc_thread(void *param)
{
	struct httpc_conn *conn = NULL;
    // waiting for the wan connection
    while (get_g_NetState()<2) {
        vTaskDelay(2000);
    }
    vTaskDelay(5000);
    /*
    conn = httpc_conn_new(HTTPC_SECURE_TLS, NULL, NULL, NULL);
    if(httpc_conn_connect(conn, "webservproxy.smartwayparking.com", 443, 0) == 0) {
        // start a header and add Host (added automatically), Content-Type and Content-Length (added by input param)
        httpc_request_write_header_start(conn, "GET", "/", NULL, 0);
        // add other required header fields if necessary
        httpc_request_write_header(conn, "Connection", "close");
        // finish and send header
        httpc_request_write_header_finish(conn);

        // receive response header
        if(httpc_response_read_header(conn) == 0) {
            httpc_conn_dump_header(conn);

            // receive response body
            if(httpc_response_is_status(conn, "200 OK")) {
                uint8_t buf[1024];
                int read_size = 0, total_size = 0;

                while(1) {
                    memset(buf, 0, sizeof(buf));
                    read_size = httpc_response_read_data(conn, buf, sizeof(buf) - 1);

                    if(read_size > 0) {
                        total_size += read_size;
                        printf("%s", buf);
                    }
                    else {
                        break;
                    }

                    if(conn->response.content_len && (total_size >= conn->response.content_len))
                        break;
                }
            }
        }
    }
    else {
        printf("\nERROR: httpc_conn_connect\n");
    }

    httpc_conn_close(conn);
    httpc_conn_free(conn);
    */
	/* Kill init thread after all init tasks done */
	vTaskDelete(NULL);
}
//========================================================================================================
void main(void) {


    // ----------------------------------------------------
	/* Initialize log uart and at command service */
	console_init();

    // ----------------------------------------------------
    // 1ms timer handler
    gtimer_init(&global_timer, TIMER1);
    gtimer_start_periodical(&global_timer, 1000, (void*)global_timer_timeout_handler, NULL);

    // ----------------------------------------------------
    // Initial a periodical timer
    //gtimer_init(&my_timer1, TIMER0);
    //gtimer_start_periodical(&my_timer1, 1000000, (void*)timer1_timeout_handler, (uint32_t)&gpio_led1);

/*
    xMutexNetState = xSemaphoreCreateMutex();

    // ----------------------------------------------------
	// wlan intialization 
#if defined(CONFIG_WIFI_NORMAL) && defined(CONFIG_NETWORK)
	wlan_network();
#endif

	if(xTaskCreate(wlan_thread, ((const char*)"init"), 512, NULL, tskIDLE_PRIORITY + 3 + PRIORITIE_OFFSET, NULL) != pdPASS)
		printf("\n\r%s xTaskCreate(wlan_thread) failed", __FUNCTION__);
    
    // ----------------------------------------------------
    // Init LED control pin
    gpio_init(&gpio_led1, GPIO_LED_PIN1);
    gpio_dir(&gpio_led1, PIN_OUTPUT);    // Direction: Output
    gpio_mode(&gpio_led1, PullNone);     // No pull





	if(xTaskCreate(httpc_thread, ((const char*)"httpc"), 512, NULL, tskIDLE_PRIORITY + 3 + PRIORITIE_OFFSET, NULL) != pdPASS)
		printf("\n\r%s xTaskCreate(httpc_thread) failed", __FUNCTION__);


*/
    // ----------------------------------------------------
    // start NFC thread
	if(xTaskCreate(nfc_thread, ((const char*)"init"), 512, NULL, tskIDLE_PRIORITY + 3 + PRIORITIE_OFFSET, NULL) != pdPASS)
		printf("\n\r%s xTaskCreate(nfc_thread) failed", __FUNCTION__);

    printf("\n\rEntering main loop...\n\r");

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

