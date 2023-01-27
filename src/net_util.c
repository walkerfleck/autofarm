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
/*
char dev_ssid[20]="TP-Link_09B1";
char dev_pass[20]="0930710512";
char devid[20]="A01000001";
char req_url[100]="192.168.0.103";  // "192.168.0.103/PK/AliveCk" / "webservproxy.smartwayparking.com"
uint16_t req_port=5000;     // 443
*/

#define FLASH_APP_BASE   0xF5000
//========================================================================================================
char this_mac[32];  // mac address of this device
extern QueueHandle_t pSystemQueue;     // cloud request queue, created by main.c
//extern unsigned int sntp_gen_system_ms(unsigned int *diff, unsigned int *sec, int timezone); // from sntp.c, modified version, return ms tick
extern char *GetSystemTimestamp(char *buf, int fmt);        // main.c
//========================================================================================================
extern void apSendMsg(QueueHandle_t q, unsigned char id, unsigned char param);  // main.c
//========================================================================================================
extern uint8_t global_stop;             // from main.c
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
  rtl_printf("Calculate CRC from address:%X,  %d bytes, CRC=%u\r\n", data, n_bytes, *crc);

}
//========================================================================================================
//========================================================================================================
// Garbage collection routines
//========================================================================================================
sTaskTime *garbageTaskTime=NULL;
sDevTask *garbageDevTask=NULL;
//========================================================================================================
sDevTask *AllocDevTask() {
    if (garbageDevTask!=NULL) {
        sDevTask *ret=garbageDevTask;
        garbageDevTask=ret->next;
        return ret;
    }
    else {
        return (sDevTask *) malloc(sizeof(sDevTask));
    }
}
//========================================================================================================
void FreeDevTask(sDevTask *p) {
    p->next = garbageDevTask;
    garbageDevTask=p;
}
//========================================================================================================
sTaskTime *AllocTaskTime() {
    if (garbageTaskTime!=NULL) {
        sTaskTime *ret=garbageTaskTime;
        garbageTaskTime=ret->next;
        return ret;
    }
    else {
        return (sTaskTime *) malloc(sizeof(sTaskTime));
    }
}
//========================================================================================================
void FreeTaskTime(sTaskTime *p) {
    p->next = garbageTaskTime;
    garbageTaskTime=p;
}
//========================================================================================================
// 解碼儲存在buffer裡的data, 轉成linklist, 連結至sys_profile裡面
void DecodeTaskTimeBuf(char *buf, int length) {
    sTaskTime *pTaskTime=buf;
    sTaskTime *lastnode=NULL;
    int idx=0;

    sys_profile.TaskTime.head=NULL;
    while (idx<length) {
        sTaskTime *node = AllocTaskTime();
        if (lastnode!=NULL)
            lastnode->next = node;
        lastnode=node;
        pTaskTime=(sTaskTime *)(buf+idx);
        memcpy(node, pTaskTime, sizeof(sTaskTime)-8);
        node->DevTaskHead=NULL;
        node->next=NULL;
        // first node
        if (sys_profile.TaskTime.head==NULL)
            sys_profile.TaskTime.head=node;
        idx += sizeof(sTaskTime)-8;
        sDevTask *pDevTask=(sDevTask *)(buf+idx);
        sDevTask *lastdnode=NULL;
        int i;
        //有device設定
        if (node->count_DevTask>0) {
            for (i=0;i<node->count_DevTask;i++) {
                pDevTask=(sDevTask *)(buf+idx);
                sDevTask *dnode=AllocDevTask();
                if (lastdnode!=NULL)
                    lastdnode->next=dnode;
                lastdnode=dnode;
                if (node->DevTaskHead==NULL)
                    node->DevTaskHead=dnode;
                memcpy(dnode, pDevTask, sizeof(sDevTask)-4);
                idx += sizeof(sDevTask)-4;
                dnode->next=NULL;
            }
        }
    }
}
//========================================================================================================
void save_profile_to_flash_offset(int offset, int len) {
    flash_t         flash;
    int loop = 0;
    uint32_t address = FLASH_APP_BASE;
    uint32_t *ptr = (uint32_t *)&sys_profile;
    char *p=(char *)&sys_profile;
    uint32_t crc;

    crc32(p+4, sizeof(sys_profile)-4, &crc);
    rtl_printf("New CRC=%u!\r\n", crc);
    sys_profile.crc = crc;
    // write crc
    ptr = (uint32_t *)&sys_profile;
    device_mutex_lock(RT_DEV_LOCK_FLASH);
    flash_erase_sector(&flash, address+(loop<<2));
    flash_write_word(&flash, address+(loop<<2), *ptr);
    device_mutex_unlock(RT_DEV_LOCK_FLASH);

    for(loop=offset;loop<(offset+len)/4;loop++) {
        device_mutex_lock(RT_DEV_LOCK_FLASH);
        flash_erase_sector(&flash, address+(loop<<2));
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
    }
    for(loop = offset, ptr = ((uint32_t *)&sys_profile) + offset/4; loop < (offset+len)/4; loop++, ptr++)
    {
        device_mutex_lock(RT_DEV_LOCK_FLASH);
        flash_write_word(&flash, address+(loop<<2), *ptr);
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
    }


}
//========================================================================================================
void  save_profile_to_flash() {
    flash_t         flash;
    int loop = 0;
    uint32_t address = FLASH_APP_BASE;
    uint32_t *ptr = (uint32_t *)&sys_profile;
    char *p=(char *)&sys_profile;
    uint32_t crc;
    int total_len=sizeof(sys_profile)-4;

    crc32(p+4, total_len-4, &crc);
    rtl_printf("New CRC=%u!\r\n", crc);
    sys_profile.crc = crc;

    rtl_printf("Write System Profile into Flash!\r\n");
    // erase and write
    for(loop = 0; loop < total_len/4; loop++)
    {
        device_mutex_lock(RT_DEV_LOCK_FLASH);
        flash_erase_sector(&flash, address+(loop<<2));
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
    }
    for(loop = 0, ptr = (uint32_t *)&sys_profile; loop < total_len/4; loop++, ptr++)
    {
        device_mutex_lock(RT_DEV_LOCK_FLASH);
        flash_write_word(&flash, address+(loop<<2), *ptr);
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
    }
}
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
    for (loop=0,p=(char *)&sys_profile;loop<sizeof(sys_profile);loop++, p++) {
        if ((loop&0x07)==0)
            rtl_printf("\r\n%04X - ", loop);
        rtl_printf("%02X ", *((unsigned char *)p));
    }
    rtl_printf("\r\n");
    */

    uint32_t crc;
    crc32(((char *)&sys_profile)+4, sizeof(sys_profile)-4, &crc);
    // crc not match, use default settings
    if (sys_profile.crc != crc) {
        rtl_printf("calculated_crc=%u, profile_crc=%u\r\n", crc, sys_profile.crc);
        memset(&sys_profile, 0, sizeof(sys_profile));
        strcpy(sys_profile.dev_ssid,"0000");
        strcpy(sys_profile.dev_pass, "0000");
        strcpy(sys_profile.req_url, "https://morelohas.com");
        strcpy(sys_profile.ota_url, "https//morelohas.com");
        strcpy(sys_profile.ota_file, "/fmupd/ota.bin");
        sys_profile.req_port=443;     // 443
        sys_profile.ota_port=443;     // 443
        memset(&(sys_profile.dev), 0xff, sizeof(sys_profile.dev));

        // update crc, erase and write
        rtl_printf("Rewrite Flash....\r\n");
        save_profile_to_flash();

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
void httpd_write_body(struct httpd_conn *conn, char *body, int body_len) {
    if (body_len>2048) {
        char *p = body;
        while (body_len>=2048) {
            httpd_response_write_data(conn, p, 2048);
            p+=2048;
            body_len-=2048;
            vTaskDelay(1);
        }
        if (body_len>0)
            httpd_response_write_data(conn, p, body_len);
    }
    else 
        httpd_response_write_data(conn, body, body_len);;
}
//========================================================================================================
void homepage_cb(struct httpd_conn *conn)
{
	char *user_agent = NULL;
    char *body=index_html;
    unsigned int body_len=index_html_len;

    // the gateway is in reset mode
    if (sys_profile.Server_Mode!=0) {
        body=index_ok_html;
        body_len=index_ok_html_len;
    }

	// test log to show brief header parsing
	httpd_conn_dump_header(conn);

	// test log to show extra User-Agent header field
	if(httpd_request_get_header_field(conn, "User-Agent", &user_agent) != -1) {
		printf("\nUser-Agent=[%s]\n", user_agent);
		httpd_free(user_agent);
	}

	// GET homepage
	if(httpd_request_is_method(conn, "GET")) {
		// write HTTP response
		httpd_response_write_header_start(conn, "200 OK", "text/html", index_html_len);
		httpd_response_write_header(conn, "Connection", "close");
		httpd_response_write_header_finish(conn);
        httpd_write_body(conn, body, body_len);
	}
	else {
		// HTTP/1.1 405 Method Not Allowed
		httpd_response_method_not_allowed(conn, NULL);
	}

	httpd_conn_close(conn);
}
//========================================================================================================
    /*
void process_edit(char *add_script,struct httpd_conn *conn) {
    char *s=NULL, *d=NULL;
    add_script[0]=0;
    httpd_request_get_query_key(conn, "s", &s);
    httpd_request_get_query_key(conn, "d", &d);
    if (s==NULL && d==NULL) {
        // 新增, 空白頁面
        int i;
        for(i=0;i<20;i++) {
            if (sys_profile.def[i].dev==0)
                break;
        }
        sprintf(add_script, "<script>document.getElementById(\"sid\").value=%d;</script>", i);
        return;
    }
    if (s!=NULL && d==NULL) {
        int i = atoi(s);
        int j;
        char buf[60];
        char *p=buf;
        // 修改
        if (sys_profile.def[i].dev==0) {
            sprintf(add_script, "<script>alert('URL不正確!');</script>", i,buf);
            return;
        }
        *p='X';p++; 
        sprintf(p, "%04x", sys_profile.def[i].dev); p+=4; 
        for (j=0;j<5;j++) {
            if (sys_profile.def[i].timedef[j].wday==0)
                break;
            *p='V';p++; 
            sprintf(p, "%02x", sys_profile.def[i].timedef[j].wday); p+=2; 
            sprintf(p, "%02u", sys_profile.def[i].timedef[j].begintime/100); p+=2; 
            sprintf(p, "%02u", sys_profile.def[i].timedef[j].begintime%100); p+=2; 
            sprintf(p, "%02u", sys_profile.def[i].timedef[j].duration); p+=2; 
        }
        *p=0;
        sprintf(add_script, "<script>document.getElementById(\"sid\").value=%d;load_data('%s');</script>", i,buf);
        return;
    }
    else if (s!=NULL && d!=NULL) {
        // 儲存
        // X0007V06080005
        char *p=d;
        int i = atoi(s);
        if (p[0]=='X') {
            memset(&(sys_profile.def[i]), 0, sizeof(struct sWaterTask));
            uint32_t dev;
            p++;
            sscanf(p, "%04x", &dev); p+=4;
            rtl_printf("\nSaving : dev=%04x\n", dev);
            sys_profile.def[i].dev = dev;
            int ndx=0;
            while (p[0]=='V') {
                unsigned char wday;
                unsigned char hour;
                unsigned char min;
                unsigned short begintime;
                unsigned char duration;
                p++;
                rtl_printf("\nSaving : remain buffer=%s\n", p);
                sscanf(p, "%02x%02d%02d%02d",&wday, &hour, &min, &duration);
                begintime=hour*100+min;
                rtl_printf("\nSaving : wday=%x\n", wday);
                rtl_printf("\nSaving : begintime=(%u,%u)%u\n", hour, min, begintime);
                rtl_printf("\nSaving : duration=%u\n", duration);
                p+=8;
                sys_profile.def[i].timedef[ndx].wday=wday;
                sys_profile.def[i].timedef[ndx].begintime=begintime;
                sys_profile.def[i].timedef[ndx].duration=duration;
                ndx++;
            }
        }
        save_profile_to_flash();
        sprintf(add_script, "<script>alert('儲存完畢');</script>");
    }
}
//========================================================================================================
//========================================================================================================
void process_setting(char *add_script,struct httpd_conn *conn) {
    char *s=NULL;
    struct tm timeinfo;
    time_t tt;
    add_script[0]=0;
    httpd_request_get_query_key(conn, "s", &s);
    if (s!=NULL) {
        // 設定
        unsigned short y,M,d,h,m,ss;
        char buf[50];
        // frtc=<year>,<month>,<day>,<hour>,<min>,<sec>
        // s:yyyymmddhhmmss
        sscanf(s, "%04d%02d%02d%02d%02d%02d", &y, &M, &d, &h, &m, &ss);
        timeinfo.tm_year = y-1900;
        timeinfo.tm_mon = M-1;
        timeinfo.tm_mday = d;
        timeinfo.tm_hour = h;
        timeinfo.tm_min = m;
        timeinfo.tm_sec = ss;
        timeinfo.tm_isdst=0;
        tt = mktime(&timeinfo);
        rtc_write(tt);
        rtl_printf("\n\rSet RTC : %d/%02d/%02d %02d:%02d:%02d\r\n", timeinfo.tm_year+1970, timeinfo.tm_mon+1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);


        sprintf(add_script, "<script>alert('設定完畢');</script>");
    }
}
*/
//========================================================================================================
struct sDocRecord {
    const char *filename;
    const char *body;
    int body_len;
    char *doctype;
    void (*fun_ptr)(char *,struct httpd_conn *);
};
//========================================================================================================
const char *strdoctype[]={
    "text/html",
    "text/css",
    "image/gif"
};
//========================================================================================================
#define DOC_W3_CSS                  0
#define DOC_WAITING_GIF             1
//#define DOC_EDIT_HTML           2
//#define DOC_EDIT_CONTENT_HTML   3
//#define DOC_SETTING_HTML        4
//#define DOC_SET_CONTENT_HTML    5
//========================================================================================================
#define TOTAL_DOCS              2
//========================================================================================================
//char *filelist[TOTAL_DOCS] = { "/w3.css", "/logo.gif", "/edit.html", "/setting.html" };
char *filelist[TOTAL_DOCS] = { "/w3.css", "/waiting.gif" };
//========================================================================================================
//char *bodylist[TOTAL_DOCS] = { w3_css, logo_gif, edit_html, setting_html };
char *bodylist[TOTAL_DOCS] = { w3_css, waiting_gif };
//========================================================================================================
//int *bodylenlist[TOTAL_DOCS] = { &w3_css_len, &logo_gif_len, &edit_html_len, &setting_html_len };
int *bodylenlist[TOTAL_DOCS] = { &w3_css_len, &waiting_gif_len };
//========================================================================================================
struct sDocRecord docRecords[TOTAL_DOCS];// = {
/*
    {"/w3.css",  w3_css, w3_css_len, strdoctype[1]}, 
    {"/logo.gif",  logo_gif, logo_gif_len, strdoctype[2]},
    {"/edit.html",  edit_html, edit_html_len, strdoctype[0]},
    {"/setting.html",  setting_html, setting_html_len, strdoctype[0]},
};*/
//========================================================================================================
void init_docs() {
    int i;
    for (i=0;i<TOTAL_DOCS;i++) {
        docRecords[i].filename = filelist[i];
        docRecords[i].body = bodylist[i];
        docRecords[i].body_len = *bodylenlist[i];
        if (strstr(docRecords[i].filename, ".css")!=NULL)
            docRecords[i].doctype = strdoctype[1];
        else if (strstr(docRecords[i].filename, ".html")!=NULL)
            docRecords[i].doctype = strdoctype[0];
        else if (strstr(docRecords[i].filename, ".gif")!=NULL)
            docRecords[i].doctype = strdoctype[2];
/*
        if (strstr(docRecords[i].filename, "edit.html")!=NULL) {
            docRecords[i].fun_ptr=&process_edit;
        }
        else if (strstr(docRecords[i].filename, "setting.html")!=NULL) {
            docRecords[i].fun_ptr=&process_setting;
        }
        else
            docRecords[i].fun_ptr=NULL;
            */
    }
}
//========================================================================================================
void direct_cb(struct httpd_conn *conn) {
	// test log to show brief header parsing
	httpd_conn_dump_header(conn);
    char *body=NULL;
    unsigned int body_len=0;
    char *doctype=NULL;     // 0: text, 1: css, 2: gif
    int i;
    char additional_script[160];
    additional_script[0]=0;
    //rtl_printf("\nrequest.path:%s\n", conn->request.path+1);
    //rtl_printf("\nrequest.path_len:%d\n", conn->request.path_len);
    for(i=0;i<TOTAL_DOCS;i++) {
        if (conn->request.path_len==strlen(docRecords[i].filename) && memcmp(conn->request.path, docRecords[i].filename, strlen(docRecords[i].filename))==0) {
            body=docRecords[i].body;
            body_len=docRecords[i].body_len;
            doctype=docRecords[i].doctype;
            if (docRecords[i].fun_ptr!=NULL)
                docRecords[i].fun_ptr(additional_script, conn);
            break;
        }
    }
	if(httpd_request_is_method(conn, "GET") && doctype!=NULL) {
		// write HTTP response
		httpd_response_write_header_start(conn, "200 OK", doctype, body_len+strlen(additional_script));
		httpd_response_write_header(conn, "Connection", "close");
		httpd_response_write_header_finish(conn);
        httpd_write_body(conn, body, body_len);
        if (additional_script[0]!=0)
            httpd_write_body(conn, additional_script, strlen(additional_script));
	}
	else {
		// HTTP/1.1 405 Method Not Allowed
		httpd_response_method_not_allowed(conn, NULL);
	}

	httpd_conn_close(conn);
}
//========================================================================================================
/*
void gettime_cb(struct httpd_conn *conn) {
    char body[40];
    body[0]=0;
    GetSystemTimestamp(body, 0);
    int body_len=strlen(body);

	if(httpd_request_is_method(conn, "GET")) {
		// write HTTP response
		httpd_response_write_header_start(conn, "200 OK", "text/html", body_len);
		httpd_response_write_header(conn, "Connection", "close");
		httpd_response_write_header_finish(conn);

		httpd_response_write_data(conn, body, body_len);
	}
	else {
		// HTTP/1.1 405 Method Not Allowed
		httpd_response_method_not_allowed(conn, NULL);
	}

	httpd_conn_close(conn);
}
*/
//========================================================================================================
    /*
void content_cb(struct httpd_conn *conn) {
	// test log to show brief header parsing
	httpd_conn_dump_header(conn);
    uint8_t *body = (uint8_t *) malloc(120);
    unsigned int body_len=0;
    char *p=body;
    body[0]=0;
    int i,j;

    //  mmmm wwhhmmll 
    // X0031V07060005V78221006
    for(i=0;i<40;i++) {
        rtl_printf("\ncontent_cb: %d, %04x\n", i, sys_profile.def[i].dev);
        if (sys_profile.def[i].dev==0)
            break;
        *p='X';p++; body_len++;
        sprintf(p, "%04x", sys_profile.def[i].dev); p+=4; body_len+=4;
        for (j=0;j<5;j++) {
            if (sys_profile.def[i].timedef[j].wday==0)
                break;
            rtl_printf("\ncontent_cb.def[%d].timedef[%d].wday: %x\n", i, j, sys_profile.def[i].timedef[j].wday);
            rtl_printf("\ncontent_cb.def[%d].timedef[%d].begintime: %x\n", i, j, sys_profile.def[i].timedef[j].begintime);
            rtl_printf("\ncontent_cb.def[%d].timedef[%d].duration: %x\n", i, j, sys_profile.def[i].timedef[j].duration);
            *p='V';p++; body_len++;
            sprintf(p, "%02x", sys_profile.def[i].timedef[j].wday); p+=2; body_len+=2;
            sprintf(p, "%02d", sys_profile.def[i].timedef[j].begintime/100); p+=2; body_len+=2;
            sprintf(p, "%02d", sys_profile.def[i].timedef[j].begintime%100); p+=2; body_len+=2;
            sprintf(p, "%02d", sys_profile.def[i].timedef[j].duration); p+=2; body_len+=2;
        }
    }
    *p=0;
    rtl_printf("\n========================================\n");
    rtl_printf("\n%s\n", body);
    rtl_printf("\n========================================\n");
	if(httpd_request_is_method(conn, "GET")) {
		// write HTTP response
		httpd_response_write_header_start(conn, "200 OK", "text/html", body_len);
		httpd_response_write_header(conn, "Connection", "close");
		httpd_response_write_header_finish(conn);

		httpd_response_write_data(conn, body, body_len);
	}
	else {
		// HTTP/1.1 405 Method Not Allowed
		httpd_response_method_not_allowed(conn, NULL);
	}
    free(body);

	httpd_conn_close(conn);
}
    */
//========================================================================================================
void fc_htm_cb(struct httpd_conn *conn)
{
	// test log to show brief header parsing
	httpd_conn_dump_header(conn);

	if(httpd_request_is_method(conn, "GET")) {
		char body_buf[1024], *body = \
"<HTML><BODY>" \
"Status Query<BR><hr>" \
"Available Heap Size: %d<BR>" \
"<hr>" \
"</BODY></HTML>";
        sprintf(body_buf, body, xPortGetFreeHeapSize());
		// write HTTP response
		httpd_response_write_header_start(conn, "200 OK", "text/html", strlen(body_buf));
		httpd_response_write_header(conn, "Connection", "close");
		httpd_response_write_header_finish(conn);
		httpd_response_write_data(conn, body_buf, strlen(body_buf));
	}
	else {
		// HTTP/1.1 405 Method Not Allowed
		httpd_response_method_not_allowed(conn, NULL);
	}

	httpd_conn_close(conn);
}
//========================================================================================================
int connectWifi(char *ssid, char *pass) {
    printf("\n\rConnecting to AP\n");
    if(wifi_connect(ssid, RTW_SECURITY_WPA2_AES_PSK, pass, strlen(ssid), strlen(pass), -1, NULL) == RTW_SUCCESS)
        LwIP_DHCP(0, DHCP_START);

    int timeout=20;
    while (timeout>0 && wifi_is_ready_to_transceive(RTW_STA_INTERFACE)!=RTW_SUCCESS) {
        vTaskDelay(1 * configTICK_RATE_HZ);
        timeout --;
    }
    if (timeout==0)
        return -1;
    return 0;
}

//========================================================================================================
uint8_t global_MQTTMessage[128];
//========================================================================================================
void messageArrived(MessageData* data)
{
    int len=127;
	mqtt_printf(MQTT_INFO, "Message arrived on topic %.*s: %.*s\n", data->topicName->lenstring.len, data->topicName->lenstring.data,
		data->message->payloadlen, (char*)data->message->payload);
    if (data->message->payloadlen<=127)
        len=data->message->payloadlen;
    memcpy(global_MQTTMessage, data->message->payload, len);
}

//========================================================================================================
uint8_t MQTT_connected=0;
MQTTClient mqtt_client;
Network mqtt_network;
MQTTPacket_connectData mqtt_connectData = MQTTPacket_connectData_initializer;
//========================================================================================================
char* MQTTLogin(char *uuser, char *upass)
{
	unsigned char sendbuf[150], readbuf[150];
    unsigned char pubtopic[50], subtopic[50];
	int rc = 0, count = 0;
    uint8_t error=0;
	char* address = "139.162.5.214";
    wifi_get_mac_address(this_mac);
    sprintf(subtopic, "MLHS/IoT/V1/%s/Ack", this_mac);  // command return '0': success
    sprintf(pubtopic, "MLHS/IoT/V1/%s", this_mac);
	char* sub_topic = subtopic;
	char* pub_topic = pubtopic;

    if (MQTT_connected==0) {    
        NetworkInit(&mqtt_network);
        MQTTClientInit(&mqtt_client, &mqtt_network, 30000, sendbuf, sizeof(sendbuf), readbuf, sizeof(readbuf));

        //mqtt_printf(MQTT_INFO, "Wait Wi-Fi to be connected.");
        while(wifi_is_ready_to_transceive(RTW_STA_INTERFACE) != RTW_SUCCESS) {
            vTaskDelay(5000 / portTICK_PERIOD_MS);
        }
        mqtt_printf(MQTT_INFO, "Wi-Fi okay.");
        
        //mqtt_printf(MQTT_INFO, "Connect Network \"%s\"", address);
        mqtt_connectData.username.cstring = "morelohas";
        mqtt_connectData.password.cstring = "NspNspVrHolqOini22244#";

        while ((rc = NetworkConnect(&mqtt_network, address, 1883)) != 0){
            //mqtt_printf(MQTT_INFO, "Return code from network connect is %d\n", rc);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        mqtt_printf(MQTT_INFO, "\"%s\" Connected", address);

        mqtt_connectData.MQTTVersion = 3;
        mqtt_connectData.clientID.cstring = this_mac;

        mqtt_printf(MQTT_INFO, "Start MQTT connection");
        while ((rc = MQTTConnect(&mqtt_client, &mqtt_connectData)) != 0){
            //mqtt_printf(MQTT_INFO, "Return code from MQTT connect is %d\n", rc);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        mqtt_printf(MQTT_INFO, "MQTT Connected");

        mqtt_printf(MQTT_INFO, "Subscribe to Topic: %s", sub_topic);
        if ((rc = MQTTSubscribe(&mqtt_client, sub_topic, QOS2, messageArrived)) != 0) 
            error=1; //mqtt_printf(MQTT_INFO, "Return code from MQTT subscribe is %d\n", rc);
        if (error==0)
            MQTT_connected=1;
    }
    
	//mqtt_printf(MQTT_INFO, "Publish Topics: %s", pub_topic);

    // zero the message
    global_MQTTMessage[0]=0;
    MQTTMessage message;
    char payload[100];
    int retry=0;

    message.qos = QOS1;
    message.retained = 0;
    message.payload = payload;
    sprintf(payload, "{\"c\":\"L\",\"u\":\"%s\",\"p\":\"%s\"}", uuser, upass);
    message.payloadlen = strlen(payload);


	while (retry<5)
	{

        if ((rc = MQTTPublish(&mqtt_client, pub_topic, &message)) != 0)
            ; //mqtt_printf(MQTT_INFO,"Return code from MQTT publish is %d\n", rc);

        // yield cpu to receive packet
		if ((rc = MQTTYield(&mqtt_client, 2000)) != 0)
			; //mqtt_printf(MQTT_INFO,"Return code from yield is %d\n", rc);
        // packet received from server
        if (global_MQTTMessage[0]!=0)
            break;
        retry++;

		vTaskDelay(2000);
	}

    // close connection
    //MQTTDisconnect(&mqtt_client);

    if (retry>=5)
        return NULL;

    return global_MQTTMessage;
}
//========================================================================================================
// 如果wifi已經連線成功, 就不需render wifi格子
// 用文字來顯示等待, 分開顯示 : 正在連線wifi... 正在登入雲端主機...
// 前端用promise來處理二個request
// 連線完成後, 重新開機, 就進入STA+long request mode, 
void home_post_cb(struct httpd_conn *conn)
{
	// POST /test_post
	if(httpd_request_is_method(conn, "POST")) {
		size_t read_size;
		uint8_t buf[50];
		size_t content_len = conn->request.content_len;
		uint8_t *body = (uint8_t *) malloc(content_len + 1);
        uint8_t *p;
		uint8_t ssid[36];
		uint8_t pass[36];
		uint8_t uuser[36];
		uint8_t upass[36];

		if(body) {
			// read HTTP body
			memset(body, 0, content_len + 1);
			read_size = httpd_request_read_data(conn, body, content_len);

			// write HTTP response
			httpd_response_write_header_start(conn, "200 OK", "application/json", 0);
            // "Content-Type", "application/json;charset=UTF-8"
			//httpd_response_write_header(conn, "Content-Type", "application/json;charset=UTF-8");
			httpd_response_write_header(conn, "Connection", "close");
			httpd_response_write_header_finish(conn);


            // body : {"ssid":"asdf","pass":"qwer","uuser":"1234","upass":"zxcv"}
            p=strtok(body, "{}\",:");
            while (p!=NULL) {
                if (strcmp(p, "ssid")==0) {
                    strcpy(ssid, strtok(NULL, "{}\",:"));
                }
                else if (strcmp(p, "pass")==0) {
                    strcpy(pass, strtok(NULL, "{}\",:"));
                }
                else if (strcmp(p, "uuser")==0) {
                    strcpy(uuser, strtok(NULL, "{}\",:"));
                }
                else if (strcmp(p, "upass")==0) {
                    strcpy(upass, strtok(NULL, "{}\",:"));
                }
                p=strtok(NULL, "{}\",:");
            }
            int wret = connectWifi(ssid, pass);
            if (wret!=0)
                strcpy(buf, "{\"status\":-1, \"msg\":\"failed to connect wifi\"}");
            else {
                if (MQTTLogin(uuser, upass)==NULL)
                    strcpy(buf, "{\"status\":-2, \"msg\":\"failed to login server\"}");
                else {
                    strcpy(buf, "{\"status\":0, \"msg\":\"ok\"}");
                    // success means the data is correct,
                    // write to flash 
                    strcpy(sys_profile.dev_ssid, ssid);
                    strcpy(sys_profile.dev_pass, pass);
                    sys_profile.Server_Mode=1;
                    // write dev_ssid && dev_pass
                    save_profile_to_flash_offset(4, 72);
                    // write Server_Mode
                    save_profile_to_flash_offset(260, 4);
		            vTaskDelay(5000);
                    sys_reset();
                }
            }
			httpd_response_write_data(conn, buf, strlen(buf));
			//httpd_response_write_data(conn, body, strlen(body));
		}
		else {
			// HTTP/1.1 500 Internal Server Error
			httpd_response_internal_server_error(conn, NULL);
		}
	    free(body);
	}
	else {
		// HTTP/1.1 405 Method Not Allowed
		httpd_response_method_not_allowed(conn, NULL);
	}

	httpd_conn_close(conn);
}
//========================================================================================================
/*
void test_get_cb(struct httpd_conn *conn)
{
	// GET /test_post
	if(httpd_request_is_method(conn, "GET")) {
		char *test1 = NULL, *test2 = NULL;

		// get 'test1' and 'test2' in query string
		if((httpd_request_get_query_key(conn, "test1", &test1) != -1) &&
		   (httpd_request_get_query_key(conn, "test2", &test2) != -1)) {

			// write HTTP response
			httpd_response_write_header_start(conn, "200 OK", "text/plain", 0);
			httpd_response_write_header(conn, "Connection", "close");
			httpd_response_write_header_finish(conn);
			httpd_response_write_data(conn, "\r\nGET query string", strlen("\r\nGET query string"));
			httpd_response_write_data(conn, "\r\ntest1: ", strlen("\r\ntest1: "));
			httpd_response_write_data(conn, test1, strlen(test1));
			httpd_response_write_data(conn, "\r\ntest2: ", strlen("\r\ntest2: "));
			httpd_response_write_data(conn, test2, strlen(test2));
		}
		else {
			// HTTP/1.1 400 Bad Request
			httpd_response_bad_request(conn, "Bad Request - test1 or test2 not in query string");
		}

		if(test1)
			httpd_free(test1);

		if(test2)
			httpd_free(test2);
	}
	else {
		// HTTP/1.1 405 Method Not Allowed
		httpd_response_method_not_allowed(conn, NULL);
	}

	httpd_conn_close(conn);
}

void test_post_htm_cb(struct httpd_conn *conn)
{
	// GET /test_post.htm
	if(httpd_request_is_method(conn, "GET")) {
		char *body = \
"<HTML><BODY>" \
"<FORM action=\"/test_post\" method=\"post\">" \
"Text1: <INPUT type=\"text\" name=\"text1\" size=\"50\" maxlength=\"50\"><BR>" \
"Text2: <INPUT type=\"text\" name=\"text2\" size=\"50\" maxlength=\"50\"><BR>" \
"<INPUT type=\"submit\" value=\"POST\"><BR>" \
"</FORM>" \
"</BODY></HTML>";

		// write HTTP response
		httpd_response_write_header_start(conn, "200 OK", "text/html", strlen(body));
		httpd_response_write_header(conn, "Connection", "close");
		httpd_response_write_header_finish(conn);
		httpd_response_write_data(conn, body, strlen(body));
	}
	else {
		// HTTP/1.1 405 Method Not Allowed
		httpd_response_method_not_allowed(conn, NULL);
	}

	httpd_conn_close(conn);
}

*/

//
//
//********************************************************************************************************
//  AP Mode routines
//
//
//
//
//********************************************************************************************************
//  HTTP request routines
//
//
//
//
//********************************************************************************************************
//========================================================================================================
//void start_interactive_mode(void);
//********************************************************************************************************
//========================================================================================================
#if CONFIG_LWIP_LAYER
extern struct netif xnetif[NET_IF_NUM]; 
#endif

//========================================================================================================
void StartAPMode() {
    int i;
    char ssid[60];

    if(wifi_on(RTW_MODE_STA_AP) < 0){
        printf("\n\rwifi_on failed\n");
        return;
    }

    wifi_get_mac_address(this_mac);
    printf("\n\rMAC Address is :%s\n\r", this_mac);

    // generate ssid
    sprintf(ssid, "FM_%s", this_mac);


    if(wifi_start_ap(ssid, RTW_SECURITY_WPA2_AES_PSK, "12345678", strlen(ssid), 8, 10) < 0) {
        rtl_printf("AP initial failed...  \r\n");
    }
    else
        rtl_printf("=================>AP Starting[%s]...\r\n", ssid);
   
    printf("\n\rCheck AP running\n");
    int timeout = 20;
    while(1) {
        char essid[33];
        if(wext_get_ssid(WLAN1_NAME, (unsigned char *) essid) > 0) {
            if(strcmp((const char *) essid, (const char *)ssid) == 0) {
                printf("\n\r %s started\n", ssid);
                break;
            }
        }
        if(timeout == 0) {
            printf("\n\r ERROR: Start AP timeout\n");	
            return;
        }
        vTaskDelay(1 * configTICK_RATE_HZ);
        timeout --;
    }
    xnetif[1].ip_addr.addr=0x500CA8C0;
    xnetif[1].gw.addr=0x010CA8C0;

    printf("\n\rStart DHCP server\n");
    // For more setting about DHCP, please reference fATWA in atcmd_wifi.c.
#if CONFIG_LWIP_LAYER
    dhcps_init(&xnetif[1]);
#endif
    rtl_printf("\r\nThe default host IP is : 192.168.12.80\r\n");
    
}
//********************************************************************************************************
//  HTTP request routines
//
//
//
//
//********************************************************************************************************
//========================================================================================================
void StopAPMode() {
    dhcps_deinit();
    //            LwIP_DHCP(0, DHCP_STOP);
    vTaskDelay(500);
    g_NetState = 1;
    wifi_disconnect();
    vTaskDelay(500);
    wifi_off();
}
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
struct sPacket {
    uint8_t head;           // must be "P"
    uint8_t cmd;            // 
    uint8_t id;             // from server : receiver's device id, from client : device's id
    uint8_t parameter1;     // according to cmd
    uint8_t parameter2;     // according to cmd
    uint16_t check_code;    // second random unsigned short which is generate from adding every bytes before and an initial unsigned short value : 135786
    uint8_t end;            // must be "<"
};

//========================================================================================================
//void start_interactive_mode(void);
//========================================================================================================
//========================================================================================================
//  wireless LAN thread, connect to AP
//  hard coded SSID and password
void wlan_thread(void* data) {


    if (sys_profile.Server_Mode==0) {           // httpd server
        rtl_printf("\r\nwlan thread starting...\r\n");
        // waiting the system stable
#if defined(CONFIG_WIFI_NORMAL) && defined(CONFIG_NETWORK)
        //wlan_network();
#endif

        rtl_printf("\r\nLwIP_Init...\r\n");
        LwIP_Init();
        //rtl_printf("\r\nwifi_manager_init...\r\n");
        //wifi_manager_init();
        rtl_printf("\r\nStartAPMode...\r\n");
        //wifi_on(THIS_WIFI_MODE);
        StartAPMode();


        //rtw_wifi_setting_t setting;
        //wifi_get_setting(WLAN0_NAME,&setting);
        //wifi_show_setting(WLAN0_NAME,&setting);
     

        httpd_reg_page_callback("/", homepage_cb);
        httpd_reg_page_callback("/index.html", homepage_cb);
        httpd_reg_page_callback(docRecords[DOC_W3_CSS].filename, direct_cb);
        httpd_reg_page_callback(docRecords[DOC_WAITING_GIF].filename, direct_cb);
    	httpd_reg_page_callback("/home_post", home_post_cb);
    	//httpd_reg_page_callback("/home_post_step2", home_post_step2_cb);
    //	httpd_reg_page_callback(docRecords[DOC_LOGO_GIF].filename, direct_cb);
    //	httpd_reg_page_callback(docRecords[DOC_EDIT_HTML].filename, direct_cb);
    //	httpd_reg_page_callback(docRecords[DOC_EDIT_CONTENT_HTML].filename, direct_cb);
    //	httpd_reg_page_callback(docRecords[DOC_SETTING_HTML].filename, direct_cb);
    //	httpd_reg_page_callback(docRecords[DOC_SET_CONTENT_HTML].filename, direct_cb);
    //	httpd_reg_page_callback("/gettime.html", gettime_cb);
    //	httpd_reg_page_callback("/content.html", content_cb);
    //	httpd_reg_page_callback("/test_get", test_get_cb);
    //	httpd_reg_page_callback("/test_post.htm", test_post_htm_cb);
        httpd_reg_page_callback("/fc.html", fc_htm_cb);
    //	httpd_reg_page_callback("/test_post", home_post_cb);
        if(httpd_start(80, 5, 4096, HTTPD_THREAD_SINGLE, HTTPD_SECURE_NONE) != 0) {
            printf("ERROR: httpd_start");
            httpd_clear_page_callbacks();
        }

        while (global_stop==0) {
            //rtl_printf("\r\nNetwork Trigger\r\n");
            vTaskDelay(2000);
        }
    }   //sys_profile.Server_Mode==0
    else {
        // normal wifi client mode
        LwIP_Init();
        wifi_manager_init();
        if(wifi_on(RTW_MODE_STA) < 0){
            printf("\n\rwifi_on failed\n");
        }
        while (g_NetState<2) {
            while (wifi_is_up(RTW_STA_INTERFACE)==0) {
                vTaskDelay(500);
            }
            wifi_set_autoreconnect(0);
            if(wifi_connect(sys_profile.dev_ssid, RTW_SECURITY_WPA2_AES_PSK , sys_profile.dev_pass, strlen(sys_profile.dev_ssid), strlen(sys_profile.dev_pass), -1, NULL) == RTW_SUCCESS)
            {
                printf("\n\rWiFi Connected.\n\r");
                LwIP_DHCP(0, DHCP_START);
            }
            else
            {
                printf("\n\rWiFi Failed.\n\r");
                vTaskDelay(2000);
                continue;
            }
            rtw_wifi_setting_t setting;
            wifi_get_setting(WLAN0_NAME,&setting);
            wifi_show_setting(WLAN0_NAME,&setting);

            wifi_get_mac_address(this_mac);
            printf("\n\rMAC Address is :%s\n\r", this_mac);
            while(wifi_is_ready_to_transceive(RTW_STA_INTERFACE) != RTW_SUCCESS) {
                vTaskDelay(5000 / portTICK_PERIOD_MS);
            }
            printf("\n\rWiFi Ready.\n\r");
            g_NetState=2;
        }   // while (g_NetState<2)

        // MQTT loop
        while (global_stop==0) {
            //rtl_printf("\r\nNetwork Trigger\r\n");
            vTaskDelay(2000);
        }

    }   // normal wifi client mode

exit:
	/* Kill init thread after all init tasks done */
	vTaskDelete(NULL);

}
//========================================================================================================
void net_main(void) {

    read_profile_from_flash();

    init_docs();

    // ----------------------------------------------------
	// wlan intialization 

    // continuously ping cloud server to make sure the network is okay
	if(xTaskCreate(wlan_thread, ((const char*)"net_init"), 512, NULL, tskIDLE_PRIORITY + 3 + PRIORITIE_OFFSET, &hSysThread) != pdPASS)
		printf("\n\r%s xTaskCreate(wlan_thread) failed", __FUNCTION__);

}

