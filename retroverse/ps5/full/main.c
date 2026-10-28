/* RetroVerse PS5: curated, LAN-only game browser, downloader, uploader.
 * SPDX-License-Identifier: GPL-3.0-or-later.
 * Uses the PS5 system HTTP/TLS libraries; does not disable TLS verification.
 * The PS5 firmware/loader can reject specific file or network operations.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include "assets.inc"
#include "catalog_data.inc"
#include "archive_data.inc"

#define PORT 8765
#define HEADER_MAX 16384
#define NET_MAX (14u * 1024u * 1024u)
#define DOWNLOAD_MAX (10ULL * 1024ULL * 1024ULL * 1024ULL)
#define BASE_DIR "/data/retroverse"
#define ROM_DIR BASE_DIR "/roms"
#define ITEM_ID "ultimate-rom-collection"

typedef struct { char unused[45]; char message[3075]; } notification_t;
int sceKernelSendNotificationRequest(int, notification_t *, size_t, int);
static void notify(const char *m) {
    notification_t n; memset(&n, 0, sizeof n);
    snprintf(n.message,sizeof n.message,"%s",m);
    sceKernelSendNotificationRequest(0, &n, sizeof n, 0);
}
int sceNetInit(void);
int sceNetPoolCreate(const char *,int,int);
int sceNetPoolDestroy(int);
int sceSslInit(size_t);
int sceSslTerm(int);
int sceHttpInit(int,int,size_t);
int sceHttpTerm(int);
int sceHttpCreateTemplate(int,const char*,int,int);
int sceHttpSetResponseHeaderMaxSize(int,size_t);
int sceHttpDeleteTemplate(int);
int sceHttpCreateConnectionWithURL(int,const char*,int);
int sceHttpDeleteConnection(int);
int sceHttpCreateRequestWithURL(int,int,const char*,uint64_t);
int sceHttpSendRequest(int,const void*,size_t);
int sceHttpGetStatusCode(int,int*);
int sceHttpGetResponseContentLength(int,int*,uint64_t*);
int sceHttpReadData(int,void*,size_t);
int sceHttpDeleteRequest(int);

typedef struct {int pool,ssl,http,tmpl,conn,req;} Net;
static void net_close(Net *n) {
    if(n->req>=0)sceHttpDeleteRequest(n->req);
    if(n->conn>=0)sceHttpDeleteConnection(n->conn);
    if(n->tmpl>=0)sceHttpDeleteTemplate(n->tmpl);
    if(n->http>=0)sceHttpTerm(n->http);
    if(n->ssl>=0)sceSslTerm(n->ssl);
    if(n->pool>=0)sceNetPoolDestroy(n->pool);
}
static int net_open(Net *n, const char *url) {
    n->pool=n->ssl=n->http=n->tmpl=n->conn=n->req=-1;
    if(sceNetInit()<0)return -1;
    if((n->pool=sceNetPoolCreate("retroverse",256*1024,0))<0)return -2;
    if((n->ssl=sceSslInit(256*1024))<0)return -3;
    if((n->http=sceHttpInit(n->pool,n->ssl,256*1024))<0)return -4;
    if((n->tmpl=sceHttpCreateTemplate(n->http,"RetroVerse-PS5/0.2",2,1))<0)return -5;
    if(sceHttpSetResponseHeaderMaxSize(n->tmpl,32768)<0)return -6;
    if((n->conn=sceHttpCreateConnectionWithURL(n->tmpl,url,0))<0)return -7;
    if((n->req=sceHttpCreateRequestWithURL(n->conn,0,url,0))<0)return -8;
    return 0;
}
static int net_begin(Net *n,const char *url) {
    int rc=net_open(n,url),status=0;
    if(rc<0)return rc;
    if(sceHttpSendRequest(n->req,NULL,0)<0)return -9;
    if(sceHttpGetStatusCode(n->req,&status)<0)return -10;
    return status;
}
static unsigned char *net_readall(const char *url,size_t max,size_t *out,int *http_status) {
    Net n;size_t sz=0,cap=65536;unsigned char *p=NULL;int code=net_begin(&n,url);
    *http_status=code;*out=0;
    if(code!=200){net_close(&n);return NULL;}
    p=malloc(cap+1);
    if(!p){net_close(&n);return NULL;}
    for (;;) {
        if(sz==cap){
            if(cap>=max){free(p);net_close(&n);return NULL;}
            size_t next=cap*2;if(next>max)next=max;
            unsigned char *q=realloc(p,next+1);if(!q){free(p);net_close(&n);return NULL;}
            p=q;cap=next;
        }
        int got=sceHttpReadData(n.req,p+sz,cap-sz);
        if(got<0){free(p);net_close(&n);return NULL;}
        if(!got)break;
        sz+=(size_t)got;
        if(sz>max){free(p);net_close(&n);return NULL;}
    }
    p[sz]=0;*out=sz;net_close(&n);return p;
}
static void percent_encode(const char *src,char *dst,size_t max,int allow_slash) {
    const char *hex="0123456789ABCDEF";size_t k=0;
    for(size_t i=0;src[i]&&k+4<max;i++){
        unsigned char c=(unsigned char)src[i];
        if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~'||(allow_slash&&c=='/')) dst[k++]=c;
        else {dst[k++]='%';dst[k++]=hex[c>>4];dst[k++]=hex[c&15];}
    }dst[k]=0;
}
static int hx(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static void percent_decode(const char *src,char *dst,size_t max) {
    size_t j=0;
    for(size_t i=0;src[i]&&j+1<max;i++){
        if(src[i]=='%'&&hx(src[i+1])>=0&&hx(src[i+2])>=0){
            dst[j++]=(char)((hx(src[i+1])<<4)|hx(src[i+2]));i+=2;
        }else if(src[i]=='+')dst[j++]=' ';
        else dst[j++]=src[i];
    }dst[j]=0;
}
static int platform_ok(const char *p){
    const char *s[]={"SNES","NES","GENESIS","GB","GBA","N64","PS1","PS2","DC","ARCADE"};
    for(size_t i=0;i<sizeof(s)/sizeof(s[0]);i++)if(!strcmp(p,s[i]))return 1;
    return 0;
}
static const char *libretro_system(const char *p){
    if(!strcmp(p,"SNES"))return "Nintendo - Super Nintendo Entertainment System";
    if(!strcmp(p,"NES"))return "Nintendo - Nintendo Entertainment System";
    if(!strcmp(p,"GENESIS"))return "Sega - Mega Drive - Genesis";
    if(!strcmp(p,"GB"))return "Nintendo - Game Boy";
    if(!strcmp(p,"GBA"))return "Nintendo - Game Boy Advance";
    if(!strcmp(p,"N64"))return "Nintendo - Nintendo 64";
    if(!strcmp(p,"PS1"))return "Sony - PlayStation";
    if(!strcmp(p,"PS2"))return "Sony - PlayStation 2";
    if(!strcmp(p,"DC"))return "Sega - Dreamcast";
    if(!strcmp(p,"ARCADE"))return "MAME";
    return NULL;
}
static const char *curated_prefix(const char *p){
    if(!strcmp(p,"SNES"))return "Nintendo - SNES.zip/";
    if(!strcmp(p,"NES"))return "Nintendo - NES.zip/";
    if(!strcmp(p,"GENESIS"))return "Sega - Genesis.zip/";
    if(!strcmp(p,"GB"))return "Nintendo - Game Boy.zip/";
    if(!strcmp(p,"GBA"))return "Nintendo - Game Boy Advance.zip/";
    if(!strcmp(p,"N64"))return "Nintendo - N64.zip/";
    if(!strcmp(p,"PS1"))return "Sony - PS1/";
    if(!strcmp(p,"DC"))return "Sega - Dreamcast.zip/";
    if(!strcmp(p,"ARCADE"))return "Arcade - Mame 2003 Plus.zip/";
    return NULL;
}
static int endswith(const char *s,const char *suf){
    size_t a=strlen(s),b=strlen(suf);return a>=b&&!strcasecmp(s+a-b,suf);
}
static int allowed_ext(const char *f) {
    static const char *exts[]={".zip",".7z",".nes",".sfc",".smc",".gb",".gbc",".gba",".n64",".z64",".v64",".md",".gen",".bin",".cue",".iso",".chd",".cso",".pbp",".gdi",".cdi",".m3u",".nds",".rom",".pce",".wad"};
    for(size_t i=0;i<sizeof(exts)/sizeof(exts[0]);i++)if(endswith(f,exts[i]))return 1;
    return 0;
}
static int safe_name(const char *s){
    size_t len=strlen(s);
    if(!len||len>220||s[0]=='.')return 0;
    for(size_t i=0;i<len;i++)if(s[i]=='/'||s[i]=='\\'||(unsigned char)s[i]<32||s[i]==':'||s[i]=='<'||s[i]=='>'||s[i]=='|'||s[i]=='"'||s[i]=='?')return 0;
    return 1;
}
static const char *basename_s(const char *s){const char *p=strrchr(s,'/');return p?p+1:s;}
static int safe_archive_path(const char *platform,const char *file) {
    const char *pref=curated_prefix(platform);if(!pref||strncmp(file,pref,strlen(pref)))return 0;
    if(strstr(file,"..")||strchr(file,'\\')||strchr(file,'?')||strchr(file,'#'))return 0;
    for(const unsigned char *p=(const unsigned char*)file;*p;p++)if(*p<32)return 0;
    const char *name=basename_s(file);
    return safe_name(name)&&allowed_ext(name);
}
static int directories(const char *p) {
    if(mkdir(BASE_DIR,0700)<0&&errno!=EEXIST)return -1;
    if(mkdir(ROM_DIR,0700)<0&&errno!=EEXIST)return -1;
    char folder[512];snprintf(folder,sizeof(folder),"%s/%s",ROM_DIR,p);
    if(mkdir(folder,0700)<0&&errno!=EEXIST)return -1;
    return 0;
}
static int write_all(int s,const void *buf,size_t len){
    const unsigned char *p=buf;while(len){ssize_t n=send(s,p,len,0);if(n<=0)return -1;p+=(size_t)n;len-=(size_t)n;}return 0;
}
static void response(int s,int code,const char *why,const char *mime,const void *body,size_t size) {
    char header[500];int n=snprintf(header,sizeof header,
      "HTTP/1.1 %d %s\r\nContent-Length: %llu\r\nContent-Type: %s\r\nCache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n",
      code,why,(unsigned long long)size,mime);
    if(n<=0||n>=(int)sizeof header)return;
    if(write_all(s,header,(size_t)n)==0&&size)write_all(s,body,size);
}
static void json(int s,int code,const char *str){
    response(s,code,code==200?"OK":"Error","application/json; charset=utf-8",str,strlen(str));
}
static void error(int s,int code,const char *msg){
    char buf[700];snprintf(buf,sizeof buf,"{\"error\":\"%s\"}",msg);json(s,code,buf);
}
static void param(const char *url,const char *key,char *out,size_t n){
    out[0]=0;const char *q=strchr(url,'?');if(!q)return;q++;
    while(*q) {
        const char *eq=strchr(q,'=');if(!eq)break;
        const char *end=strchr(eq+1,'&');if(!end)end=q+strlen(q);
        if((size_t)(eq-q)==strlen(key)&&!strncmp(q,key,strlen(key))){
            char tmp[1024];size_t len=(size_t)(end-eq-1);if(len>=sizeof tmp)len=sizeof(tmp)-1;
            memcpy(tmp,eq+1,len);tmp[len]=0;percent_decode(tmp,out,n);return;
        }
        q=(*end=='&')?end+1:end;if(!*end)break;
    }
}
static int json_str(const char *src,const char *key,char *dst,size_t max) {
    char pattern[90];snprintf(pattern,sizeof pattern,"\"%s\"",key);
    const char *p=strstr(src,pattern);if(!p)return 0;
    p=strchr(p,':');if(!p)return 0;p++;
    while(*p==' '||*p=='\n')p++;
    if(*p++!='"')return 0;
    size_t i=0;
    while(*p&&*p!='"'&&i+1<max){if(*p=='\\'){p++;if(!*p)return 0;}dst[i++]=*p++;}
    dst[i]=0;return *p=='"';
}
static int json_bool(const char *src,const char *key){
    char pattern[90];snprintf(pattern,sizeof pattern,"\"%s\"",key);
    const char *p=strstr(src,pattern);if(!p)return 0;p=strchr(p,':');if(!p)return 0;p++;
    while(isspace((unsigned char)*p))p++;
    return !strncmp(p,"true",4);
}
static void json_quote(const char *in,char *out,size_t max){
    size_t j=0;if(max<3)return;out[j++]='"';
    for(size_t i=0;in[i]&&j+8<max;i++){
        unsigned char c=(unsigned char)in[i];
        if(c=='"'||c=='\\'){out[j++]='\\';out[j++]=c;}
        else if(c<32){int n=snprintf(out+j,max-j,"\\u%04x",c);j+=(size_t)n;}
        else out[j++]=c;
    }
    out[j++]='"';out[j]=0;
}
static void normalize(const char *raw,char *out,size_t max) {
    char name[512];snprintf(name,sizeof name,"%s",basename_s(raw));
    char *dot=strrchr(name,'.');if(dot&&dot>name&&allowed_ext(dot))*dot=0;
    int grouped=0;size_t j=0;int space=1;
    for(size_t i=0;name[i]&&j+1<max;i++){
        char c=name[i];if(c=='('||c=='['){grouped++;continue;}if(c==')'||c==']'){if(grouped)grouped--;continue;}if(grouped)continue;
        if(isalnum((unsigned char)c)){out[j++]=(char)tolower((unsigned char)c);space=0;}
        else if(!space&&j+1<max){out[j++]=' ';space=1;}
    }while(j&&out[j-1]==' ')j--;out[j]=0;
}
static void save_url(const char *platform,const char *file,char *url,size_t len) {
    char escaped[1500];percent_encode(file,escaped,sizeof escaped,1);
    (void)platform;
    snprintf(url,len,"https://archive.org/download/%s/%s",ITEM_ID,escaped);
}

typedef struct {
    volatile int state, cancel;
    volatile uint64_t received,total;
    char platform[20],filename[240],error[200],saved[340],path[800];
    unsigned int id;
} Job;
static Job job={0};
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned int sequence=0;
enum {J_IDLE=0,J_DOWNLOADING=1,J_COMPLETED=2,J_FAILED=3,J_CANCELED=4};

static void *download_thread(void *arg){
    (void)arg;
    char dest[650],part[650],url[2300];
    Net net;FILE *f=NULL;int code;
    char base[256];snprintf(base,sizeof base,"%s",basename_s(job.path));
    if(directories(job.platform)<0){snprintf(job.error,sizeof job.error,"Unable to create /data/retroverse folders");job.state=J_FAILED;return NULL;}
    snprintf(dest,sizeof dest,"%s/%s/%s",ROM_DIR,job.platform,base);
    if(access(dest,F_OK)==0){snprintf(job.error,sizeof job.error,"File already exists; no overwrite permitted");job.state=J_FAILED;return NULL;}
    snprintf(part,sizeof part,"%s/%s/.retroverse-%u.part",ROM_DIR,job.platform,job.id);
    save_url(job.platform,job.path,url,sizeof url);
    code=net_begin(&net,url);
    if(code!=200){snprintf(job.error,sizeof job.error,"Archive download HTTP/status %d",code);net_close(&net);job.state=J_FAILED;return NULL;}
    int len_type=0;uint64_t size=0;
    if(sceHttpGetResponseContentLength(net.req,&len_type,&size)>=0&&size>0&&size<=DOWNLOAD_MAX)job.total=size;
    if(size>DOWNLOAD_MAX){snprintf(job.error,sizeof job.error,"File too large");net_close(&net);job.state=J_FAILED;return NULL;}
    f=fopen(part,"wb");
    if(!f){snprintf(job.error,sizeof job.error,"PS5 storage is not writable");net_close(&net);job.state=J_FAILED;return NULL;}
    unsigned char chunk[65536];int failure=0;
    while(!job.cancel) {
        int n=sceHttpReadData(net.req,chunk,sizeof chunk);
        if(n<0){snprintf(job.error,sizeof job.error,"Network read error %d",n);failure=1;break;}
        if(!n)break;
        if(job.received+(uint64_t)n>DOWNLOAD_MAX){snprintf(job.error,sizeof job.error,"10 GB download limit reached");failure=1;break;}
        if(fwrite(chunk,1,(size_t)n,f)!=(size_t)n){snprintf(job.error,sizeof job.error,"Storage write failed or full");failure=1;break;}
        job.received+=(uint64_t)n;
    }
    if(fclose(f)!=0){failure=1;snprintf(job.error,sizeof job.error,"Failed closing output file");}
    net_close(&net);
    if(job.cancel){unlink(part);job.state=J_CANCELED;return NULL;}
    if(!failure&&job.total&&job.received!=job.total){failure=1;snprintf(job.error,sizeof job.error,"Incomplete download");}
    if(!failure&&rename(part,dest)<0){failure=1;snprintf(job.error,sizeof job.error,"Could not finalize download");}
    if(failure){unlink(part);job.state=J_FAILED;return NULL;}
    snprintf(job.saved,sizeof job.saved,"roms/%s/%s",job.platform,base);
    job.state=J_COMPLETED;return NULL;
}
static void jobs_response(int sock){
    char body[2000],name[500],e[500],saved[750];int state=job.state;
    json_quote(job.filename,name,sizeof name);json_quote(job.error,e,sizeof e);json_quote(job.saved,saved,sizeof saved);
    if(state==J_IDLE){json(sock,200,"{\"jobs\":[]}");return;}
    snprintf(body,sizeof body,"{\"jobs\":[{\"id\":\"%u\",\"filename\":%s,\"platform\":\"%s\",\"status\":\"%s\",\"received\":%llu,\"total\":%llu,\"error\":%s,\"saved_to\":%s}]}",
      job.id,name,job.platform,state==J_DOWNLOADING?"downloading":state==J_COMPLETED?"completed":state==J_FAILED?"failed":"canceled",
      (unsigned long long)job.received,(unsigned long long)job.total,e,saved);
    json(sock,200,body);
}
static void start_download(int sock,const char *body){
    char platform[24],file[650],title[250];title[0]=0;
    if(!json_str(body,"platform",platform,sizeof platform)||!json_str(body,"filename",file,sizeof file)||
       !json_bool(body,"rights_confirmed")){error(sock,400,"Platform, filename and rights confirmation are required");return;}
    json_str(body,"title",title,sizeof title);
    if(!platform_ok(platform)||!safe_archive_path(platform,file)){error(sock,400,"Unrecognized platform or invalid curated file path");return;}
    pthread_mutex_lock(&lock);
    if(job.state==J_DOWNLOADING){pthread_mutex_unlock(&lock);error(sock,409,"A download is already running");return;}
    memset(&job,0,sizeof job);job.state=J_DOWNLOADING;job.id=++sequence;
    snprintf(job.platform,sizeof job.platform,"%s",platform);
    snprintf(job.path,sizeof job.path,"%s",file);
    snprintf(job.filename,sizeof job.filename,"%s",basename_s(file));
    pthread_t thread;
    int started=pthread_create(&thread,NULL,download_thread,NULL);
    if(started){job.state=J_FAILED;pthread_mutex_unlock(&lock);error(sock,500,"Could not create download worker");return;}
    pthread_detach(thread);pthread_mutex_unlock(&lock);
    char response_body[130];snprintf(response_body,sizeof response_body,"{\"ok\":true,\"id\":\"%u\",\"status\":\"downloading\"}",job.id);json(sock,200,response_body);
}
static void library_response(int sock){
    char *body=malloc(250000);if(!body){error(sock,500,"Out of memory");return;}
    size_t used=0;used+=(size_t)snprintf(body+used,250000-used,"{\"games\":[");
    int count=0;const char *p[]={"SNES","NES","GENESIS","GB","GBA","N64","PS1","PS2","DC","ARCADE"};
    for(size_t i=0;i<sizeof p/sizeof p[0];i++){
        char path[500];snprintf(path,sizeof path,"%s/%s",ROM_DIR,p[i]);
        DIR *d=opendir(path);if(!d)continue;
        struct dirent *ent;
        while((ent=readdir(d))&&count<1000&&used<246000) {
            if(!safe_name(ent->d_name)||!allowed_ext(ent->d_name))continue;
            char name[600];json_quote(ent->d_name,name,sizeof name);
            used+=(size_t)snprintf(body+used,250000-used,"%s{\"platform\":\"%s\",\"filename\":%s}",count?",":"",p[i],name);count++;
        }
        closedir(d);
    }
    used+=(size_t)snprintf(body+used,250000-used,"]}");response(sock,200,"OK","application/json; charset=utf-8",body,used);free(body);
}
static void catalog_index(int sock,const char *url){
    char p[32];param(url,"platform",p,sizeof p);
    for(size_t i=0;i<sizeof embedded_catalogs/sizeof embedded_catalogs[0];i++){
        if(!strcmp(p,embedded_catalogs[i].platform)){
            response(sock,200,"OK","text/html; charset=utf-8",
              embedded_catalogs[i].html,embedded_catalogs[i].length);
            return;
        }
    }
    error(sock,400,"No embedded catalog for requested platform");
}

static int suffix_rom(const char *s){return allowed_ext(s);}
static void curated_matches(int sock,const char *url){
    char p[32],title[256];
    param(url,"platform",p,sizeof p);
    param(url,"title",title,sizeof title);
    if(!platform_ok(p)){error(sock,400,"Unknown platform");return;}
    const embedded_source_t *index=NULL;
    for(size_t i=0;i<sizeof embedded_sources/sizeof embedded_sources[0];i++){
        if(!strcmp(p,embedded_sources[i].platform)){index=&embedded_sources[i];break;}
    }
    if(!index || index->length==0 || index->listing[0]==0){
        json(sock,200,"{\"supported\":false,\"files\":[]}");return;
    }
    char wanted[260];normalize(title,wanted,sizeof wanted);
    char *body=malloc(20000);
    if(!body){error(sock,500,"Out of memory");return;}
    size_t used=(size_t)snprintf(body,20000,"{\"supported\":true,\"files\":[");
    unsigned int matches=0;
    const char *cursor=index->listing;
    while(*cursor&&matches<24&&used<16000){
        const char *end=strchr(cursor,'\n');
        if(!end)end=cursor+strlen(cursor);
        size_t len=(size_t)(end-cursor);
        if(len>0&&len<240){
            char name[260],canonical[600],norm[260];
            memcpy(name,cursor,len);name[len]=0;
            normalize(name,norm,sizeof norm);
            if(wanted[0]&&norm[0]&&!strcmp(norm,wanted)){
                int n=snprintf(canonical,sizeof canonical,"%s%s",index->prefix,name);
                if(n>0&&(size_t)n<sizeof canonical&&safe_archive_path(p,canonical)){
                    char quoted_name[650],quoted_path[1400];
                    json_quote(name,quoted_name,sizeof quoted_name);
                    json_quote(canonical,quoted_path,sizeof quoted_path);
                    int written=snprintf(body+used,20000-used,
                      "%s{\"filename\":%s,\"display_name\":%s,\"size\":0,\"source_kind\":\"%s\"}",
                      matches?",":"",quoted_path,quoted_name,
                      strstr(index->prefix,".zip/")?"zip-entry":"direct-file");
                    if(written>0&&(size_t)written<20000-used){
                       used+=(size_t)written;matches++;
                    }
                }
            }
        }
        cursor=(*end=='\n')?end+1:end;
    }
    used+=(size_t)snprintf(body+used,20000-used,"]}");
    response(sock,200,"OK","application/json; charset=utf-8",body,used);
    free(body);
}

/* Enumerate only titles actually named in a locally indexed source collection.
 * A listing does NOT guarantee that the host will deliver the file. */
static void host_games(int sock,const char *url){
    char platform[32],letter[8],host[32];
    param(url,"platform",platform,sizeof platform);
    param(url,"letter",letter,sizeof letter);
    param(url,"host",host,sizeof host);
    if(strcmp(host,"archive")){error(sock,400,"This host has no verified file index");return;}
    if(!platform_ok(platform)){error(sock,400,"Unknown platform");return;}
    const embedded_source_t *index=NULL;
    for(size_t i=0;i<sizeof embedded_sources/sizeof embedded_sources[0];i++)
      if(!strcmp(platform,embedded_sources[i].platform)){index=&embedded_sources[i];break;}
    if(!index || !index->length || !index->listing[0]){
        json(sock,200,"{\"host\":\"archive\",\"indexed\":false,\"games\":[]}");return;
    }
    char *body=malloc(160000);
    if(!body){error(sock,500,"Out of memory");return;}
    size_t used=(size_t)snprintf(body,160000,"{\"host\":\"archive\",\"indexed\":true,\"games\":[");
    unsigned int count=0;
    const char *cursor=index->listing;
    while(*cursor && count<350 && used<154000) {
        const char *end=strchr(cursor,'\n');if(!end)end=cursor+strlen(cursor);
        size_t len=(size_t)(end-cursor);
        if(len>0 && len<240){
            char filename[260],canonical[600],title[300],norm[300];
            memcpy(filename,cursor,len);filename[len]=0;
            snprintf(title,sizeof title,"%s",filename);
            char *dot=strrchr(title,'.');if(dot&&allowed_ext(dot))*dot=0;
            char *group=strstr(title," (");if(group)*group=0;
            normalize(filename,norm,sizeof norm);
            unsigned char first=(unsigned char)title[0];
            char initial=(char)(isalpha(first)?toupper(first):'#');
            if((!letter[0] || letter[0]==initial) && allowed_ext(filename)){
                int n=snprintf(canonical,sizeof canonical,"%s%s",index->prefix,filename);
                if(n>0&&(size_t)n<sizeof canonical&&safe_archive_path(platform,canonical)) {
                    char qtitle[800],qfile[1400];
                    json_quote(title,qtitle,sizeof qtitle);
                    json_quote(canonical,qfile,sizeof qfile);
                    int written=snprintf(body+used,160000-used,
                       "%s{\"title\":%s,\"filename\":%s,\"platform\":\"%s\",\"availability\":\"listed\"}",
                       count?",":"",qtitle,qfile,platform);
                    if(written>0 && (size_t)written < 160000-used){used+=(size_t)written;count++;}
                }
            }
        }
        cursor=*end=='\n'?end+1:end;
    }
    used+=(size_t)snprintf(body+used,160000-used,"]}");
    response(sock,200,"OK","application/json; charset=utf-8",body,used);
    free(body);
}

/* Explicit self-test: download an XML metadata file from the same Archive host,
 * write it to safe PS5 application storage, reread and verify, then delete.
 * No commercial game data is fetched by the diagnostic endpoint. */
static void test_download(int sock){
    const char *url="https://archive.org/download/ultimate-rom-collection/ultimate-rom-collection_meta.xml";
    size_t length=0;int status=0;
    unsigned char *data=net_readall(url,2u*1024u*1024u,&length,&status);
    if(!data) {
        char msg[180];
        snprintf(msg,sizeof msg,"PS5 HTTPS failed (status %d); catalog remains available offline",status);
        error(sock,502,msg);
        return;
    }
    if(length<128 || data[0]!='<' || !strstr((const char*)data,"metadata")){
        free(data);error(sock,502,"Host returned unexpected data, not an XML metadata file");return;
    }
    if(mkdir(BASE_DIR,0700)<0 && errno!=EEXIST){
        free(data);error(sock,500,"Cannot create RetroVerse data folder");return;
    }
    char path[512];snprintf(path,sizeof path,"%s/.archive-download-test.xml",BASE_DIR);
    FILE *out=fopen(path,"wb");
    if(!out){free(data);error(sock,500,"Cannot write test file to PS5 application data");return;}
    int bad=fwrite(data,1,length,out)!=length;
    if(fclose(out)!=0)bad=1;
    unsigned char sample[64]={0};
    FILE *in=fopen(path,"rb");
    if(!in)bad=1;
    else {size_t n=fread(sample,1,sizeof sample,in);if(n!=sizeof sample || memcmp(sample,data,sizeof sample)!=0)bad=1;fclose(in);}
    unlink(path);free(data);
    if(bad){error(sock,500,"PS5 storage could not verify a downloaded file");return;}
    char body[230];snprintf(body,sizeof body,
       "{\"ok\":true,\"host\":\"archive\",\"http_status\":%d,\"bytes_written\":%llu,\"verified\":true,\"test_file_removed\":true}",
       status,(unsigned long long)length);
    json(sock,200,body);
}

static int parse_header(const char *s,const char *key,unsigned long long *val){
    size_t len=strlen(key);const char *p=s;
    while(p&&*p){const char *nl=strstr(p,"\r\n");if(!nl)break;
        if((size_t)(nl-p)>=len+1&&!strncasecmp(p,key,len)&&p[len]==':'){
            const char *start=p+len+1;while(*start==' ')start++;
            *val=strtoull(start,NULL,10);return 1;
        }p=nl+2;if(p[0]=='\r'&&p[1]=='\n')break;
    }return 0;
}
static void upload(int sock,const char *url,const char *header,const unsigned char *first,size_t firstlen){
    char p[32],filename[256];param(url,"platform",p,sizeof p);param(url,"filename",filename,sizeof filename);
    if(!platform_ok(p)||!safe_name(filename)||!allowed_ext(filename)){error(sock,400,"Invalid destination or file extension");return;}
    unsigned long long length=0;
    if(!parse_header(header,"Content-Length",&length)||!length||length>DOWNLOAD_MAX){error(sock,413,"Invalid or oversized upload");return;}
    if(directories(p)<0){error(sock,500,"PS5 library storage is not writable");return;}
    char dest[800],part[800];snprintf(dest,sizeof dest,"%s/%s/%s",ROM_DIR,p,filename);
    if(access(dest,F_OK)==0){error(sock,409,"That filename already exists; no overwrite permitted");return;}
    snprintf(part,sizeof part,"%s/%s/.incoming-%u.part",ROM_DIR,p,++sequence);
    FILE *f=fopen(part,"wb");if(!f){error(sock,500,"Unable to open destination");return;}
    uint64_t done=firstlen;if(done>length)done=length;
    int failed=fwrite(first,1,(size_t)done,f)!=(size_t)done;
    unsigned char buf[65536];
    while(!failed&&done<length){
        size_t want=(size_t)((length-done)<sizeof buf?(length-done):sizeof buf);
        ssize_t n=recv(sock,buf,want,0);
        if(n<=0){failed=1;break;}
        if(fwrite(buf,1,(size_t)n,f)!=(size_t)n){failed=1;break;}done+=(uint64_t)n;
    }
    if(fclose(f)!=0)failed=1;
    if(!failed&&rename(part,dest)<0)failed=1;
    if(failed){unlink(part);error(sock,500,"Upload incomplete or storage error");return;}
    char saved[1000],quoted[1200],json_body[1400];
    snprintf(saved,sizeof saved,"roms/%s/%s",p,filename);
    json_quote(saved,quoted,sizeof quoted);snprintf(json_body,sizeof json_body,"{\"ok\":true,\"saved_to\":%s}",quoted);json(sock,200,json_body);
}
static void serve(int sock) {
    unsigned char header[HEADER_MAX+1];size_t used=0;
    size_t split=0;
    while(used<HEADER_MAX){
        ssize_t n=recv(sock,header+used,HEADER_MAX-used,0);if(n<=0)return;used+=(size_t)n;header[used]=0;
        char *end=strstr((char*)header,"\r\n\r\n");
        if(end){split=(size_t)(end-(char*)header)+4;break;}
    }
    if(!split){error(sock,431,"Request headers too large");return;}
    char method[16],url[2048],version[20];
    if(sscanf((char*)header,"%15s %2047s %19s",method,url,version)!=3){error(sock,400,"Malformed HTTP request");return;}
    char *query=strchr(url,'?');char path[1024];size_t path_len=query?(size_t)(query-url):strlen(url);
    if(path_len>=sizeof path){error(sock,414,"URL too long");return;}memcpy(path,url,path_len);path[path_len]=0;
    if(!strcmp(method,"GET")){
        if(!strcmp(path,"/")||!strcmp(path,"/index.html")){response(sock,200,"OK","text/html; charset=utf-8",ui_page,UI_PAGE_LEN);return;}
        if(!strcmp(path,"/api/health")){json(sock,200,"{\"ok\":true,\"mode\":\"ps5\",\"version\":\"0.3\",\"features\":[\"catalog\",\"art\",\"curated_source\",\"downloads\",\"upload\",\"library\"]}");return;}
        if(!strcmp(path,"/api/catalog-index")){catalog_index(sock,url);return;}
        if(!strcmp(path,"/api/host-games")){host_games(sock,url);return;}
        if(!strcmp(path,"/api/curated/matches")){curated_matches(sock,url);return;}
        if(!strcmp(path,"/api/jobs")){jobs_response(sock);return;}
        if(!strcmp(path,"/api/library")){library_response(sock);return;}
        error(sock,404,"Not found");return;
    }
    if(!strcmp(method,"POST")){
        const unsigned char *body=header+split;size_t body_len=used-split;
        if(!strcmp(path,"/api/local/upload")){upload(sock,url,(char*)header,body,body_len);return;}
        unsigned long long len=0;
        if(!parse_header((char*)header,"Content-Length",&len)||len>8000){error(sock,413,"Request body too large");return;}
        char data[8192];size_t have=body_len;if(have>len)have=(size_t)len;
        memcpy(data,body,have);
        while(have<len){ssize_t n=recv(sock,data+have,(size_t)len-have,0);if(n<=0){error(sock,400,"Incomplete request");return;}have+=(size_t)n;}
        data[have]=0;
        if(!strcmp(path,"/api/curated/download")){start_download(sock,data);return;}
        if(!strcmp(path,"/api/test-download")){test_download(sock);return;}
        if(!strcmp(path,"/api/archive/cancel")){job.cancel=1;json(sock,200,"{\"ok\":true}");return;}
        error(sock,404,"Unsupported endpoint");return;
    }
    error(sock,405,"Only GET and POST methods are supported");
}
int main(void){
    signal(SIGPIPE,SIG_IGN);
    int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0){notify("RetroVerse: socket failure");return 1;}
    int yes=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes);
    struct sockaddr_in addr;memset(&addr,0,sizeof addr);
    addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_ANY);addr.sin_port=htons(PORT);
    if(bind(fd,(struct sockaddr*)&addr,sizeof addr)!=0||listen(fd,10)!=0){
        notify("RetroVerse: port 8765 busy. Stop old ELF first.");close(fd);return 2;
    }
    notify("RetroVerse Store active: visit PS5-IP:8765");
    for(;;){
        int client=accept(fd,NULL,NULL);if(client<0){if(errno==EINTR)continue;break;}
        struct timeval tv={.tv_sec=35,.tv_usec=0};
        setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);
        serve(client);close(client);
    }
    close(fd);return 0;
}
