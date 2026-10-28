/* RetroVerse PS5 curl transport: GPL-3.0-or-later.
 * HTTPS-only, peer/host validation mandatory, OS CA roots embedded at build.
 */
#include <curl/curl.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "curl_net.h"
#include "ca_data.inc"

#define RV_CA_FILE "/data/retroverse/.trusted-roots.pem"
#define RV_CA_DIR "/data/retroverse"
static pthread_once_t init_once=PTHREAD_ONCE_INIT;
static int curl_ready=0;
static void init_curl(void){
    if(curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK)curl_ready=1;
}
static int setup_certs(char *err,size_t errlen){
    if(mkdir(RV_CA_DIR,0700)!=0&&errno!=EEXIST){
        snprintf(err,errlen,"Cannot create CA folder: %s",strerror(errno));
        return -1;
    }
    FILE *f=fopen(RV_CA_FILE,"wb");
    if(!f){snprintf(err,errlen,"Cannot write CA bundle: %s",strerror(errno));return -1;}
    size_t n=fwrite(rv_ca_roots,1,RV_CA_ROOTS_LEN,f);
    int closed=fclose(f);
    if(n!=RV_CA_ROOTS_LEN||closed){
        snprintf(err,errlen,"Cannot write complete TLS trust store");return -1;
    }
    return 0;
}
typedef struct {
    unsigned char *data;size_t len,cap,max;
    int overflow;
} MemSink;
static size_t memory_sink(char *bytes,size_t sz,size_t count,void *user){
    MemSink *s=user;
    if(sz&&count>SIZE_MAX/sz)return 0;
    size_t n=sz*count;
    if(s->len> s->max || n>s->max-s->len){s->overflow=1;return 0;}
    if(s->len+n+1>s->cap){
        size_t cap=s->cap?s->cap*2:65536;
        if(cap<s->len+n+1)cap=s->len+n+1;
        if(cap>s->max+1)cap=s->max+1;
        unsigned char *p=realloc(s->data,cap);
        if(!p)return 0;
        s->cap=cap;s->data=p;
    }
    memcpy(s->data+s->len,bytes,n);s->len+=n;
    s->data[s->len]=0;
    return n;
}
typedef struct {
    FILE *file;uint64_t max;
    volatile int *cancel;
    volatile uint64_t *received,*total;
    int io_error,over_limit;
} FileSink;
static size_t file_sink(char *bytes,size_t size,size_t count,void *user){
    FileSink *s=user;
    if(size&&count>SIZE_MAX/size)return 0;
    size_t n=size*count;
    if(*s->received>s->max||n>s->max-*s->received){s->over_limit=1;return 0;}
    if(*s->cancel)return 0;
    size_t w=fwrite(bytes,1,n,s->file);
    if(w!=n){s->io_error=errno;return 0;}
    *s->received+=(uint64_t)w;
    return w;
}
static int progress(void *user,curl_off_t expected,curl_off_t now,
                    curl_off_t up,curl_off_t uploaded){
    (void)now;(void)up;(void)uploaded;
    FileSink *s=user;
    if(expected>0)*s->total=(uint64_t)expected;
    return *s->cancel?1:0;
}
static CURL *new_curl(const char *url,char *err,size_t errlen,char *errorbuf){
    pthread_once(&init_once,init_curl);
    if(!curl_ready){snprintf(err,errlen,"curl_global_init failed");return NULL;}
    if(setup_certs(err,errlen)<0)return NULL;
    CURL *curl=curl_easy_init();
    if(!curl){snprintf(err,errlen,"curl_easy_init failed");return NULL;}
    curl_easy_setopt(curl,CURLOPT_ERRORBUFFER,errorbuf);
    curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,"RetroVerse-PS5/0.6");
    curl_easy_setopt(curl,CURLOPT_CAINFO,RV_CA_FILE);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_MAXREDIRS,6L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,15L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT,90L);
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(curl,CURLOPT_ACCEPT_ENCODING,"");
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"https");
    curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS_STR,"https");
#else
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS,(long)CURLPROTO_HTTPS);
    curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS,(long)CURLPROTO_HTTPS);
#endif
    return curl;
}
static void set_error(char *out,size_t outsz,const char *prefix,CURLcode rc,
                     const char *errorbuf,long http){
    snprintf(out,outsz,"%s: curl=%d (%s), HTTP %ld; %s",prefix,(int)rc,
        curl_easy_strerror(rc),http,errorbuf&&errorbuf[0]?errorbuf:"");
}
int rv_https_get_memory(const char *url,size_t max,unsigned char **out,size_t *size,
                        char *err,size_t errlen){
    char details[CURL_ERROR_SIZE]={0};
    *out=NULL;*size=0;
    CURL *curl=new_curl(url,err,errlen,details);
    if(!curl)return -1;
    MemSink sink={.max=max};
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,memory_sink);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&sink);
    CURLcode rc=curl_easy_perform(curl);
    long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    if(rc!=CURLE_OK||status!=200||sink.overflow){
        set_error(err,errlen,sink.overflow?"Response exceeded limit":"Network",
                  rc,details,status);
        free(sink.data);curl_easy_cleanup(curl);return -2;
    }
    *out=sink.data;*size=sink.len;curl_easy_cleanup(curl);return 0;
}
int rv_https_download(const char *url,FILE *destination,uint64_t max,
                      volatile int *cancel,volatile uint64_t *received,
                      volatile uint64_t *total,char *err,size_t errlen){
    char details[CURL_ERROR_SIZE]={0};
    CURL *curl=new_curl(url,err,errlen,details);
    if(!curl)return -1;
    FileSink sink={.file=destination,.max=max,.cancel=cancel,
                   .received=received,.total=total};
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,file_sink);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&sink);
    curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
    curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,progress);
    curl_easy_setopt(curl,CURLOPT_XFERINFODATA,&sink);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT,0L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,200L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,45L);
    CURLcode rc=curl_easy_perform(curl);
    long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    int fail=(rc!=CURLE_OK || status!=200 || sink.io_error || sink.over_limit);
    if(fail){
        if(*cancel)snprintf(err,errlen,"Canceled by user");
        else if(sink.io_error)snprintf(err,errlen,"Disk write failed: %s",strerror(sink.io_error));
        else if(sink.over_limit)snprintf(err,errlen,"Download exceeds configured size limit");
        else set_error(err,errlen,"Download",rc,details,status);
    }
    curl_easy_cleanup(curl);
    return fail?-1:0;
}
