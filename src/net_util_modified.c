/*
 *
 * todo
 *   1. 使用array queue, 取代timer
 *   2. 增加GetTime request來對時, SNTP有時太慢了
 *   3. ping好像久了會有問題, 就採用last alive來看看是否網路ok
 *
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
#include <httpc/httpc.h>
//#include <sntp/sntp.h>
#include "flash_api.h"
//#include "osdep_service.h"
#include "device_lock.h"
#include "apdef.h"
#include <cJSON.h>
#include <httpd/httpd.h>
#include "pages.h"

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

struct sys_profile_t sys_profile;
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
void  save_profile_to_flash() {
    flash_t         flash;
    int loop = 0;
    uint32_t address = FLASH_APP_BASE;
    uint32_t *ptr = (uint32_t *)&sys_profile;
    char *p=(char *)&sys_profile;
    uint32_t crc;

    crc32(p+4, sizeof(sys_profile)-4, &crc);
    rtl_printf("New CRC=%u!\r\n", crc);
    sys_profile.crc = crc;

    rtl_printf("Write System Profile into Flash!\r\n");
    // erase and write
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
void process_edit(char *add_script,struct httpd_conn *conn) {
    /*
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
*/
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
#define DOC_W3_CSS              0
#define DOC_LOGO_GIF            1
#define DOC_EDIT_HTML           2
#define DOC_EDIT_CONTENT_HTML   3
#define DOC_SETTING_HTML        4
#define DOC_SET_CONTENT_HTML    5
//========================================================================================================
#define TOTAL_DOCS              6
//========================================================================================================
char *filelist[TOTAL_DOCS] = { "/w3.css", "/logo.gif", "/edit.html", "/setting.html" };
//========================================================================================================
char *bodylist[TOTAL_DOCS] = { w3_css, logo_gif, edit_html, setting_html };
//========================================================================================================
int *bodylenlist[TOTAL_DOCS] = { &w3_css_len, &logo_gif_len, &edit_html_len, &setting_html_len };
//========================================================================================================
struct sDocRecord docRecords[TOTAL_DOCS]; /* = {
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

        if (strstr(docRecords[i].filename, "edit.html")!=NULL) {
            docRecords[i].fun_ptr=&process_edit;
        }
        else if (strstr(docRecords[i].filename, "setting.html")!=NULL) {
            docRecords[i].fun_ptr=&process_setting;
        }
        else
            docRecords[i].fun_ptr=NULL;
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
//========================================================================================================
void content_cb(struct httpd_conn *conn) {
    /*
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
    */
}
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

void test_post_cb(struct httpd_conn *conn)
{
	// POST /test_post
	if(httpd_request_is_method(conn, "POST")) {
		size_t read_size;
		uint8_t buf[50];
		size_t content_len = conn->request.content_len;
		uint8_t *body = (uint8_t *) malloc(content_len + 1);

		if(body) {
			// read HTTP body
			memset(body, 0, content_len + 1);
			read_size = httpd_request_read_data(conn, body, content_len);

			// write HTTP response
			httpd_response_write_header_start(conn, "200 OK", "text/plain", 0);
			httpd_response_write_header(conn, "Connection", "close");
			httpd_response_write_header_finish(conn);
			memset(buf, 0, sizeof(buf));
			sprintf(buf, "%d bytes from POST: ", read_size);
			httpd_response_write_data(conn, buf, strlen(buf));
			httpd_response_write_data(conn, body, strlen(body));
			free(body);
		}
		else {
			// HTTP/1.1 500 Internal Server Error
			httpd_response_internal_server_error(conn, NULL);
		}
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

    /*
    int timeout = 20;
    while(1) {
        char essid[33];
        if(wext_get_ssid(WLAN0_NAME, (unsigned char *) essid) > 0) {
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
    */

    printf("\n\rStart DHCP server\n");
    // For more setting about DHCP, please reference fATWA in atcmd_wifi.c.
#if CONFIG_LWIP_LAYER

    rtl_printf("NET_IF_NUM=%d\n", NET_IF_NUM);
    //xnetif[0].ip_addr.addr=0x640CA8C0;
    //xnetif[0].gw.addr=0x010CA8C0;
    rtl_printf("IP=%08X\n", xnetif[0].ip_addr.addr);
    rtl_printf("GW=%08X\n", xnetif[0].gw.addr);
    dhcps_init(&xnetif[0]);

	//memcpy(&dhcps_local_address, &pnetif->ip_addr,
	//						sizeof(struct ip_addr));
    
#endif
    
}
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
void Start_Httpd() {
   	httpd_reg_page_callback("/", homepage_cb);
	httpd_reg_page_callback("/index.html", homepage_cb);
	httpd_reg_page_callback(docRecords[DOC_W3_CSS].filename, direct_cb);
	httpd_reg_page_callback(docRecords[DOC_LOGO_GIF].filename, direct_cb);
	httpd_reg_page_callback(docRecords[DOC_EDIT_HTML].filename, direct_cb);
	httpd_reg_page_callback(docRecords[DOC_EDIT_CONTENT_HTML].filename, direct_cb);
	httpd_reg_page_callback(docRecords[DOC_SETTING_HTML].filename, direct_cb);
	httpd_reg_page_callback(docRecords[DOC_SET_CONTENT_HTML].filename, direct_cb);
	httpd_reg_page_callback("/gettime.html", gettime_cb);
	httpd_reg_page_callback("/content.html", content_cb);
//	httpd_reg_page_callback("/test_get", test_get_cb);
//	httpd_reg_page_callback("/test_post.htm", test_post_htm_cb);
	httpd_reg_page_callback("/fc.html", fc_htm_cb);
//	httpd_reg_page_callback("/test_post", test_post_cb);
	if(httpd_start(80, 5, 4096, HTTPD_THREAD_SINGLE, HTTPD_SECURE_NONE) != 0) {
		printf("ERROR: httpd_start");
		httpd_clear_page_callbacks();
	}
}
//========================================================================================================
void Stop_Httpd() {
    httpd_clear_page_callbacks();
    httpd_stop();
}
//========================================================================================================
// url : "http://www.server.com"
// rport : 5000
// route : "/PK/AliveCk" or other request route
// action : "GET" / "POST"
// posttxt : "", or ",\"key":"value"\""
// cb_process: call back function to process the returned packet
//
// for example:  http_request("GET", "/PK/AliveCk", "", cbAlive);
void http_request(char *url, uint16_t rport, char *action, char *route, char *posttxt, void (*cb_process)(char *, uint32_t ), uint32_t extra_data, uint32_t timeout_ms) {
    struct httpc_conn *conn = NULL;
    int url_start_pos=0;
    // httpc_setup_debug()無用, 直接修改lib_http.a, 將訊息變成空白字串
    //
    //conn = httpc_conn_new(HTTPC_SECURE_TLS, NULL, NULL, NULL);
    //httpc_setup_debug(HTTPC_DEBUG_OFF);
    conn = httpc_conn_new(identify_url(&url_start_pos, url), NULL, NULL, NULL);
    //httpc_setup_debug(HTTPC_DEBUG_OFF);
    // connect in 6sec timeout
    if(httpc_conn_connect(conn, url+url_start_pos, rport, 6) == 0) {
    //httpc_setup_debug(HTTPC_DEBUG_OFF);
        char post_data[120], timestamp[30];
        sprintf(post_data, "{\"gatewayid\":\"%u\",\"ver\":\"%s\",\"ptime\":\"%s\"%s}", sys_profile.gatewayid, dev_version, GetSystemTimestamp(timestamp, 0),posttxt);
        // start a header and add Host (added automatically), Content-Type and Content-Length (added by input param)        
        httpc_request_write_header_start(conn, action, route, NULL, strlen(post_data));

        // add other required header fields if necessary
        httpc_request_write_header(conn, "Connection", "close");
        httpc_request_write_header(conn, "Content-Type", "application/json");
        // finish and send header
        if (httpc_request_write_header_finish(conn)<0) {
            // due to httpc_conn_connect() can not detect fail connection, we detect fail here
            printf("HTTP socket error (1)[%s]  ", route);
            cb_process(NULL, extra_data);
            httpc_conn_close(conn);
            httpc_conn_free(conn);
            return;
        }

        if (httpc_request_write_data(conn, post_data, strlen(post_data))<0) {
            printf("HTTP socket error (2)[%s]  ", route);
            cb_process(NULL, extra_data);
            httpc_conn_close(conn);
            httpc_conn_free(conn);
            return;
        }

        // receive response header
        if(httpc_response_read_header(conn) == 0) {
            httpc_conn_dump_header(conn);

            // receive response body
            if(httpc_response_is_status(conn, "200 OK")) {
                uint8_t buf[300];
                memset(buf, 0, sizeof(buf));
                int read_size = 0, total_size = 0;

                u32 request_time = rtw_get_current_time();
                while(1) {
                    read_size = httpc_response_read_data(conn, buf+total_size, sizeof(buf) - total_size - 1);

                    if(read_size > 0) {
                        //printf("%s", buf);
                        total_size += read_size;
                    }
                    else {
                        // no thing happen, 
                        // it is possible, 
                        // because the internet may break packet into many pieces

                        //break;
                    }

                    // packet finished
                    if(conn->response.content_len && (total_size >= conn->response.content_len)) {
                        //printf("%s\r\n", buf);
                        cb_process(buf, extra_data);
                        break;
                    }

                    // timeout, break the loop
                    if (rtw_get_passing_time_ms(request_time) > timeout_ms) {
                        printf("Request Timeout: [%s]\r\n", route);
                        cb_process(NULL, extra_data);
                        break;
                    }

                    vTaskDelay(20);
                }   // while(1)
            }
            else {
                printf("HTTP status error[%s]\r\n", route);
                cb_process(NULL, extra_data);
            }
        }
        else {
            printf("\nERROR: httpc_response_read_header[%s]\n", route);
            cb_process(NULL, extra_data);
        }
    }
    else {
        printf("\nERROR: httpc_conn_connect[%s]\n", route);
        cb_process(NULL, extra_data);
    }

    httpc_conn_close(conn);
    httpc_conn_free(conn);
}
//========================================================================================================
void cbGetTime(char *buf, uint32_t data) {
    // 回傳
    // {
    //   “ptime”: "yyyymmddhhmmssZZZ",
    // }
    if (buf==NULL)
        return;
    cJSON_Hooks memoryHook;
    memoryHook.malloc_fn = malloc;
    memoryHook.free_fn = free;
    cJSON_InitHooks(&memoryHook);
    cJSON *IOTJSObject, *ptimeJSObject;

    if((IOTJSObject = cJSON_Parse(buf)) != NULL) {
        ptimeJSObject = cJSON_GetObjectItem(IOTJSObject, "ptime");
        if(ptimeJSObject && 
                ptimeJSObject->type==cJSON_String) {
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
                strncpy(tmp, ptimeJSObject->valuestring+12, 2); tmp[2]=0;      // min
                timeinfo.tm_sec = atoi(tmp);
                timeinfo.tm_isdst=0;
                tt = mktime(&timeinfo);
                rtc_write(tt);
                g_NetState=2;
            }   // if (strlen(ptimeJSObject->valuestring)==17)
        }
        else {
            rtl_printf("Bad format: %s\r\n", buf);
            cJSON_Delete(IOTJSObject);
            return;
        }
            
        cJSON_Delete(IOTJSObject);
    }
}
//========================================================================================================
//  wireless LAN thread, connect to AP
//  hard coded SSID and password
void wlan_thread(void* data) {
    int retry_ping=0;
    int retry_wlan=0;


    rtl_printf("\r\nwlan thread starting...\r\n");

    if (sys_profile.Server_Mode!=0) {
        // connecting to wifi
        while (g_NetState<2) {
            while (wifi_is_up(RTW_STA_INTERFACE)==0) {
                vTaskDelay(500);
            }
            // disable autoconnect function
            wifi_set_autoreconnect(0);
            printf("\n\rConnecting to AP...\n\r");
            if(wifi_connect(sys_profile.dev_ssid, RTW_SECURITY_WPA2_AES_PSK , sys_profile.dev_pass, strlen(sys_profile.dev_ssid), strlen(sys_profile.dev_pass), -1, NULL) == RTW_SUCCESS)
            {
                printf("\n\rWiFi Connected.\n\r");
                LwIP_DHCP(0, DHCP_START);
            }
            else
            {
                printf("\n\rWiFi Failed.\n\r");
                vTaskDelay(2000);
                // 90 times retrying wifi connection, reset the SSID to default to relocate the new station's SSID
                if (retry_wlan++>90) {
                    strcpy(sys_profile.dev_ssid, "0000");
                }
                continue;
            }
            rtw_wifi_setting_t setting;
            wifi_get_setting(WLAN0_NAME,&setting);
            wifi_show_setting(WLAN0_NAME,&setting);

            wifi_get_mac_address(this_mac);
            printf("\n\rMAC Address is :%s\n\r", this_mac);
         
            //  checking network,  
            int ping_lost=0;
            set_ping_blind_mode(1);
            // ping SmartWay's google cloud server
            retry_ping=0;
            while (retry_ping<6) {
                do_ping_call("35.185.168.76", 0, 1);
                get_ping_report(&ping_lost);
                // network success
                if (ping_lost==0)
                    break;
                vTaskDelay(200);
                retry_ping++;
            }

            if (retry_ping>=6 || ping_lost>0) {
                LwIP_DHCP(0, DHCP_STOP);
                vTaskDelay(500);
                g_NetState = 1;
                rtl_printf("waiting for connection...  \r\n");
                wifi_disconnect();
                vTaskDelay(500);
                wifi_off();
                vTaskDelay(500);
                wifi_on(THIS_WIFI_MODE);
#ifdef __CONCURRENT_MODE__            
                StartAPMode();
#endif            
            }
            else {
                rtl_printf("calibrating system time...  \r\n");
                http_request(sys_profile.req_url, sys_profile.req_url, "POST", "/GetTime", "", cbGetTime, 0, 3 * 1000 * 60);
                /*
                // sntp time calibration
                sntp_init();
                set_sntp_time();
                sntp_stop();
                //http_request_alive();
                g_NetState = 2;
                */
                g_ReconnectWifi = 0;
                rtl_printf("network ready...  \r\n");
            }
        }
    } else {
        // wifi AP + web server


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
        //rtl_printf("\r\nThe default host IP is : 192.168.1.80\r\n");
        rtw_bss_info_t ap_info;
        rtw_security_t security;
        wifi_get_ap_info(&ap_info, &security);
     
        // starting httpd server
        Start_Httpd();

        while (global_stop==0) {
            //rtl_printf("\r\nNetwork Trigger\r\n");
            vTaskDelay(2000);
        }
    }  // wifi AP + web server

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

