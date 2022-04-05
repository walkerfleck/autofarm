
//  secret code結構
//  [@智^]|[[key1]|[key2]|[key3]]
// 一旦決定了key, 另外亂數產生二個key, 亂數打亂順序, 成為key, key2, key3
// 先將 key1, key2, key3 個別用固定的 key 來加密
// 用正確的 key來加密 '@智^'
// 再用加密過的 '@智^' 來加密 [key1]|[key2]|[key3]
// 結合在一起就是 secret_code
//
// 從secret_code可以得到key
// 得到secret_code之後, 呼叫 carp_secret.checkSecretCode(), 如果得到的key是空字串, 表示不是正確的 secret_code
//
// carp login之後, 會依據req.sessionID 來當作key, 之後的request都會比對 secret_code
//
#include "FreeRTOS.h"
#include "cmsis_os.h"
#include "task.h"
#include "diag.h"
#include "log_uart_api.h"
//#include "osdep_service.h"
#include "device_lock.h"
#include "apdef.h"
#include "../../../component/common/network/ssl/polarssl-1.3.8/include/polarssl/blowfish.h"
#include "../../../component/common/network/ssl/polarssl-1.3.8/include/polarssl/base64.h"

// =====================================================================================
char *genRand(char *buf, int length) {
    char *result = buf;
    char *characters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    int i;
    srand(rtw_get_current_time());
    for (i = 0; i < length; i++, result++) {
        *result = characters[rand() % 62];
    }
    return buf;
}

// =====================================================================================
char *carp_secret_genKey(char *buf) {
     genRand(buf, 4);
     return buf;
}
// =====================================================================================
char *bf_encode(char *out, size_t o_len, char *str, char *key, char *iv) {
    //size_t iv_off=0;
    char bin_out[128];
    char padded_str[128];
    size_t olen;
    
    int len = strlen(str);
    memset(padded_str, 0, 128);
    strcpy(padded_str, str);
    len = (len+7) / 8 * 8;
    blowfish_context ctx;
    blowfish_init(&ctx);
    blowfish_setkey(&ctx, key, strlen(key)*8);
    blowfish_crypt_cbc(&ctx, BLOWFISH_ENCRYPT, len, iv, str, bin_out);
    base64_encode(out, &o_len, bin_out, len);
    //blowfish_crypt_cbc(&ctx, BLOWFISH_ENCRYPT, 0, iv, "", bin_out);
    blowfish_free(&ctx);
    out[o_len]=0;
    
    return out;
}
// =====================================================================================
char *bf_decode(char *out, size_t o_len, char *str, char *key, char *iv) {

    //size_t iv_off=0;
    char bin_out[128];
    size_t olen=128;
    int len = strlen(str);
    blowfish_context ctx;
    base64_decode(bin_out, &olen, str, len);
    blowfish_init(&ctx);
    blowfish_setkey(&ctx, key, strlen(key)*8);
    blowfish_crypt_cbc(&ctx, BLOWFISH_DECRYPT, olen, iv, bin_out, out);
    blowfish_free(&ctx);
    
    return out;
}

// =====================================================================================
/* 
char *carp_secret_genSecretCode(char *this_key)
{
    let c1 = this.b1.encode(this_key);
    let c2 = this.b1.encode(genRand(4));
    let c3 = this.b1.encode(genRand(4));
    let m = new Date().getTime()%3;
    if (m==0) {
        this.b2= new bf_codec(this.key+c1+c2+c3,'s632@#K8');
        let c0 = this.b2.encode('@智^');
        this.b3 = new bf_codec(c0,'s632@#K8');
        this.secret_code = c0+"|"+this.b3.encode(c1+"|"+c2+"|"+c3);
    }
    else if (m==1) {
        this.b2= new bf_codec(this.key+c2+c1+c3,'s632@#K8');
        let c0 = this.b2.encode('@智^');
        this.b3 = new bf_codec(c0,'s632@#K8');
        this.secret_code = c0+"|"+this.b3.encode(c2+"|"+c1+"|"+c3);
    }
    else {
        this.b2= new bf_codec(this.key+c3+c2+c1,'s632@#K8');
        let c0 = this.b2.encode('@智^');
        this.b3 = new bf_codec(c0,'s632@#K8');
        this.secret_code = c0+"|"+this.b3.encode(c3+"|"+c2+"|"+c1);
    }
    return this.secret_code;
}
*/


// =====================================================================================
// =====================================================================================
// =====================================================================================
// =====================================================================================
/*
class bf_codec {
    // ===================================== 
    constructor(key, iv) {
        this.encrypted="";
        this.decrypted="";
        this.key=key;
        this.iv=iv;
    }
    // ===================================== 
    encode(str) {
        this.cipher = crypto.createCipheriv('bf-cbc', this.key, this.iv);
        this.cipher.setAutoPadding(true);
        this.encrypted = this.cipher.update(str, 'utf8', 'base64');
        this.encrypted += this.cipher.final('base64');
        return this.encrypted;
    }
    // ===================================== 
    decode(str) {
        this.cipher = crypto.createDecipheriv('bf-cbc', this.key, this.iv);
        this.cipher.setAutoPadding(true);
        this.decrypted = this.cipher.update(str, 'base64', 'utf8');
        this.decrypted += this.cipher.final('utf-8');
        return this.decrypted;
    }
    // ===================================== 

}

class carp_secret {
    // ===================================== 
    constructor() {
        this.b1= new bf_codec('3%124aC@','s632@#K8');
    }
    // ===================================== 
    genKey() {
        this.key = genRand(4);
    }
    // ===================================== 
    setKey(k) {
        this.key = k;
    }
    // =====================================
    // return secret code
    genSecretCode()
    {
        let c1 = this.b1.encode(this.key);
        let c2 = this.b1.encode(genRand(4));
        let c3 = this.b1.encode(genRand(4));
        let m = new Date().getTime()%3;
        if (m==0) {
            this.b2= new bf_codec(this.key+c1+c2+c3,'s632@#K8');
            let c0 = this.b2.encode('@智^');
            this.b3 = new bf_codec(c0,'s632@#K8');
            this.secret_code = c0+"|"+this.b3.encode(c1+"|"+c2+"|"+c3);
        }
        else if (m==1) {
            this.b2= new bf_codec(this.key+c2+c1+c3,'s632@#K8');
            let c0 = this.b2.encode('@智^');
            this.b3 = new bf_codec(c0,'s632@#K8');
            this.secret_code = c0+"|"+this.b3.encode(c2+"|"+c1+"|"+c3);
        }
        else {
            this.b2= new bf_codec(this.key+c3+c2+c1,'s632@#K8');
            let c0 = this.b2.encode('@智^');
            this.b3 = new bf_codec(c0,'s632@#K8');
            this.secret_code = c0+"|"+this.b3.encode(c3+"|"+c2+"|"+c1);
        }
        return this.secret_code;
    }
    // =====================================
    checkSecretCode(scode)
    {
        // seperate code into two parts first
        let two_parts = scode.split("|");
        if (two_parts.length < 2)
        {
            this.key="";
            return "";
        }
        let c0 = two_parts[0];
        let b4 = new bf_codec(c0, 's632@#K8');
        let keyarray = b4.decode(two_parts[1]).split("|");
        if (keyarray.length < 3)
        {
            this.key="";
            return "";
        }
        this.key="";

        let k0 = this.b1.decode(keyarray[0]);
        let k1 = this.b1.decode(keyarray[1]);
        let k2 = this.b1.decode(keyarray[2]);
        let bb0 = new bf_codec(k0+keyarray[0]+keyarray[1]+keyarray[2], 's632@#K8');
        let bb1 = new bf_codec(k1+keyarray[0]+keyarray[1]+keyarray[2], 's632@#K8');
        let bb2 = new bf_codec(k2+keyarray[0]+keyarray[1]+keyarray[2], 's632@#K8');
        try {
            if (bb0.decode(c0)=='@智^')
                this.key = k0;
        }
        catch(err) {
        }
        try {
            if (bb1.decode(c0)=='@智^')
                this.key = k1;
        }
        catch(err) {
        }
        try {
            if (bb2.decode(c0)=='@智^')
                this.key = k2;
        }
        catch(err) {
        }
        return this.key;
    }
    // =====================================
    getKey()
    {
        return this.key;
    }
    // ===================================== 
}

*/
