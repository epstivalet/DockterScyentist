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
#include "verified_sources.inc"
#include "archive_sources.inc"
/* Backward-compatible SNES/GC flat HTML indexes use a distinct type/symbol. */
#define archive_source_t legacy_archive_source_t
#define archive_sources legacy_archive_sources
#define archive_sources_count legacy_archive_sources_count
#include "archive_data.inc"
#undef archive_source_t
#undef archive_sources
#undef archive_sources_count
#include <openssl/sha.h>
#include <openssl/evp.h>
#include "curl_net.h"
#include "torrent_bridge.h"

#define PORT 8765
#define HEADER_MAX 16384
#define NET_MAX (14u * 1024u * 1024u)
#define DOWNLOAD_MAX (10ULL * 1024ULL * 1024ULL * 1024ULL)
#define BASE_DIR "/data/retroverse"
#define ROM_DIR BASE_DIR "/roms"

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
int sceHttpSetAutoRedirect(int,int);
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
    /* Archive.org often redirects downloads to a regional storage host. */
    (void)sceHttpSetAutoRedirect(n->req,1);
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
    const char *s[]={"SNES","NES","GENESIS","GB","GBA","N64","GC","PS1","PS2","DC","ARCADE"};
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
    if(!strcmp(p,"GC"))return "Nintendo - GameCube";
    if(!strcmp(p,"PS1"))return "Sony - PlayStation";
    if(!strcmp(p,"PS2"))return "Sony - PlayStation 2";
    if(!strcmp(p,"DC"))return "Sega - Dreamcast";
    if(!strcmp(p,"ARCADE"))return "MAME";
    return NULL;
}
static int endswith(const char *s,const char *suf){
    size_t a=strlen(s),b=strlen(suf);return a>=b&&!strcasecmp(s+a-b,suf);
}
static int allowed_ext(const char *f) {
    static const char *exts[]={".zip",".7z",".nes",".sfc",".smc",".gb",".gbc",".gba",".n64",".z64",".v64",".md",".gen",".bin",".cue",".iso",".chd",".cso",".pbp",".gdi",".cdi",".m3u",".nds",".rom",".pce",".wad",".rvz",".gcm"};
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
/* Only paths in the pinned, developer-published manifest can be downloaded.
 * Never turn a client-provided path or URL into an arbitrary network request. */
static const verified_source_t *find_verified_source(const char *platform, const char *path) {
    for(size_t i=0;i<verified_sources_count;i++){
        const verified_source_t *src=&verified_sources[i];
        if(!strcmp(src->platform,platform)&&!strcmp(src->path,path))return src;
    }
    return NULL;
}

static const legacy_archive_source_t *find_legacy_archive_source(const char *platform, const char *file) {
    for(size_t i=0;i<legacy_archive_sources_count;i++){
        const legacy_archive_source_t *src=&legacy_archive_sources[i];
        if(!strcmp(src->platform,platform)&&!strcmp(src->filename,file))return src;
    }
    return NULL;
}
static void archive_title(const char *filename,char *title,size_t n){
    snprintf(title,n,"%s",filename);
    char *dot=strrchr(title,'.');
    if(dot)*dot=0;
}

/* Archive files are indexed at build time from the two configured item
 * metadata APIs. Only IDs from that embedded index can be downloaded. */
static const archive_source_t *find_archive_source(const char *platform,const char *path){
    for(size_t i=0;i<archive_sources_count;i++){
        const archive_source_t *src=&archive_sources[i];
        if(!strcmp(src->platform,platform)&&!strcmp(src->path,path))return src;
    }
    return NULL;
}
static int archive_file_digest(const char *file,const archive_source_t *src) {
    const char *expected=src->sha1[0]?src->sha1:src->md5;
    const EVP_MD *algorithm=src->sha1[0]?EVP_sha1():src->md5[0]?EVP_md5():NULL;
    if(!algorithm)return 0; /* Only size can be checked when no hash exists. */
    EVP_MD_CTX *ctx=EVP_MD_CTX_new();
    if(!ctx)return -1;
    if(EVP_DigestInit_ex(ctx,algorithm,NULL)!=1){EVP_MD_CTX_free(ctx);return -1;}
    FILE *in=fopen(file,"rb");
    if(!in){EVP_MD_CTX_free(ctx);return -1;}
    unsigned char bytes[65536],digest[EVP_MAX_MD_SIZE];unsigned len=0;
    size_t n;int ok=1;
    while((n=fread(bytes,1,sizeof bytes,in))!=0){
        if(EVP_DigestUpdate(ctx,bytes,n)!=1){ok=0;break;}
    }
    if(ferror(in))ok=0;
    fclose(in);
    if(ok&&EVP_DigestFinal_ex(ctx,digest,&len)!=1)ok=0;
    EVP_MD_CTX_free(ctx);
    if(!ok)return -1;
    static const char hex[]="0123456789abcdef";
    char actual[EVP_MAX_MD_SIZE*2+1];
    for(unsigned i=0;i<len;i++){actual[2*i]=hex[digest[i]>>4];actual[2*i+1]=hex[digest[i]&15];}
    actual[len*2]=0;
    return strcmp(actual,expected)==0?0:1;
}

static int digest_matches(const unsigned char digest[SHA256_DIGEST_LENGTH],const char *expected){
    static const char hex[]="0123456789abcdef";
    char actual[SHA256_DIGEST_LENGTH*2+1];
    for(size_t i=0;i<SHA256_DIGEST_LENGTH;i++){
        actual[2*i]=hex[digest[i]>>4];actual[2*i+1]=hex[digest[i]&15];
    }
    actual[SHA256_DIGEST_LENGTH*2]=0;
    return strcmp(actual,expected)==0;
}
static int verify_sha256_file(const char *path,const char *expected){
    unsigned char bytes[65536],hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX ctx;
    FILE *f=fopen(path,"rb");
    if(!f)return -1;
    if(!SHA256_Init(&ctx)){fclose(f);return -1;}
    size_t n;
    while((n=fread(bytes,1,sizeof bytes,f))!=0){
        if(!SHA256_Update(&ctx,bytes,n)){fclose(f);return -1;}
    }
    int io_error=ferror(f);
    fclose(f);
    if(io_error||!SHA256_Final(hash,&ctx))return -1;
    return digest_matches(hash,expected)?0:1;
}
static int verify_sha256_memory(const unsigned char *data,size_t n,const char *expected){
    unsigned char hash[SHA256_DIGEST_LENGTH];
    return SHA256(data,n,hash)!=NULL && digest_matches(hash,expected);
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
typedef struct {
    volatile int state, cancel;
    volatile uint64_t received,total;
    char platform[20],filename[240],error[200],saved[340],path[800],source_kind[16];
    unsigned int id;
} Job;
static Job job={0};
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned int sequence=0;
enum {J_IDLE=0,J_DOWNLOADING=1,J_COMPLETED=2,J_FAILED=3,J_CANCELED=4};

static void *download_thread(void *arg){
    (void)arg;
    const int is_archive=!strcmp(job.source_kind,"archive");
    const verified_source_t *verified=NULL;
    const legacy_archive_source_t *archive=NULL;
    if(is_archive)archive=find_legacy_archive_source(job.platform,job.path);
    else verified=find_verified_source(job.platform,job.path);
    if(!verified&&!archive){
        snprintf(job.error,sizeof job.error,"Selected file is not in its allowlisted source catalog");
        job.state=J_FAILED;return NULL;
    }
    const char *base=is_archive?archive->filename:verified->filename;
    const char *source_url=is_archive?archive->url:verified->url;
    const uint64_t expected=is_archive?0:(uint64_t)verified->size;
    if(!safe_name(base)||!allowed_ext(base)){
        snprintf(job.error,sizeof job.error,"Source filename is not safe");
        job.state=J_FAILED;return NULL;
    }
    char dest[650],part[650];
    if(directories(job.platform)<0){
        snprintf(job.error,sizeof job.error,"Unable to create PS5 library: %s",strerror(errno));
        job.state=J_FAILED;return NULL;
    }
    snprintf(dest,sizeof dest,"%s/%s/%s",ROM_DIR,job.platform,base);
    if(access(dest,F_OK)==0){
        snprintf(job.error,sizeof job.error,"File already exists, no overwrite allowed");
        job.state=J_FAILED;return NULL;
    }
    snprintf(part,sizeof part,"%s/%s/.retroverse-%u.part",ROM_DIR,job.platform,job.id);
    int fd=open(part,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0){
        snprintf(job.error,sizeof job.error,"Cannot create temporary file: %s",strerror(errno));
        job.state=J_FAILED;return NULL;
    }
    FILE *f=fdopen(fd,"wb");
    if(!f){int e=errno;close(fd);unlink(part);
        snprintf(job.error,sizeof job.error,"Cannot open output stream: %s",strerror(e));
        job.state=J_FAILED;return NULL;}
    job.total=expected;
    int err=rv_https_download(source_url,f,DOWNLOAD_MAX,&job.cancel,&job.received,&job.total,
                              job.error,sizeof job.error);
    if(fclose(f)!=0 && !err){
        snprintf(job.error,sizeof job.error,"Failed to flush downloaded file: %s",strerror(errno));
        err=-1;
    }
    if(job.cancel){unlink(part);job.state=J_CANCELED;return NULL;}
    if(!err&&expected&&job.received!=expected){
        snprintf(job.error,sizeof job.error,"Incomplete or unexpected file size: got %llu, expected %llu bytes",
            (unsigned long long)job.received,(unsigned long long)expected);
        err=-1;
    }
    if(!err&&!is_archive){
        int integrity=verify_sha256_file(part,verified->sha256);
        if(integrity!=0){
            snprintf(job.error,sizeof job.error,
                integrity>0?"SHA-256 mismatch: source file rejected":"Could not verify downloaded SHA-256");
            err=-1;
        }
    }
    if(!err && link(part,dest)<0){
        snprintf(job.error,sizeof job.error,"Cannot safely finalize download: %s",strerror(errno));
        err=-1;
    }
    unlink(part);
    if(err){job.state=J_FAILED;return NULL;}
    snprintf(job.saved,sizeof job.saved,"roms/%s/%s",job.platform,base);
    job.total=job.received;
    job.state=J_COMPLETED;return NULL;
}
/* One user-initiated Archive transfer at a time. For PS1 discs a request
 * downloads every BIN track and CUE in its matching basename group, to avoid
 * silently producing a CUE without its companion tracks. */
static void *archive_download_thread(void *arg){
    (void)arg;
    const archive_source_t *selected=find_archive_source(job.platform,job.path);
    if(!selected){snprintf(job.error,sizeof job.error,"Archive file not found in embedded index");job.state=J_FAILED;return NULL;}
    if(directories(job.platform)<0){
        snprintf(job.error,sizeof job.error,"Cannot create PS5 library: %s",strerror(errno));
        job.state=J_FAILED;return NULL;
    }
    const archive_source_t *items[64];size_t count=0;
    int is_ps1=!strcmp(job.platform,"PS1");
    if(is_ps1){
        /* BIN tracks before CUE, making completed CUE files usable immediately. */
        for(int pass=0;pass<2;pass++){
            for(size_t i=0;i<archive_sources_count;i++){
                const archive_source_t *row=&archive_sources[i];
                if(strcmp(row->platform,"PS1")||strcmp(row->group,selected->group))continue;
                int is_cue=endswith(row->filename,".cue");
                if(is_cue != (pass==1))continue;
                if(count==sizeof(items)/sizeof(items[0])){
                    snprintf(job.error,sizeof job.error,"Too many tracks in one disc group");
                    job.state=J_FAILED;return NULL;
                }
                items[count++]=row;
            }
        }
    }else items[count++]=selected;
    if(!count){job.state=J_FAILED;snprintf(job.error,sizeof job.error,"No files in selected disc group");return NULL;}
    char dest[680],part[680],last[340]={0};
    for(size_t i=0;i<count;i++){
        const archive_source_t *src=items[i];
        if(job.cancel){job.state=J_CANCELED;return NULL;}
        if(!safe_name(src->filename)||!src->size||src->size>DOWNLOAD_MAX){
            snprintf(job.error,sizeof job.error,"Unsafe archive filename or file size");
            job.state=J_FAILED;return NULL;
        }
        snprintf(dest,sizeof dest,"%s/%s/%s",ROM_DIR,job.platform,src->filename);
        struct stat st;
        if(stat(dest,&st)==0){
            if(st.st_size<0||(unsigned long long)st.st_size!=src->size||
               archive_file_digest(dest,src)!=0){
                snprintf(job.error,sizeof job.error,"Existing file differs; refusing to overwrite");
                job.state=J_FAILED;return NULL;
            }
            /* Existing matching file belongs to the user and remains untouched. */
            snprintf(last,sizeof last,"roms/%s/%s",job.platform,src->filename);
            continue;
        }
        if(errno!=ENOENT){
            snprintf(job.error,sizeof job.error,"Cannot inspect destination: %s",strerror(errno));
            job.state=J_FAILED;return NULL;
        }
        snprintf(part,sizeof part,"%s/%s/.retroverse-%u-%u.part",ROM_DIR,job.platform,
                 job.id,(unsigned)i);
        int fd=open(part,O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd<0){
            snprintf(job.error,sizeof job.error,"Cannot create temporary file: %s",strerror(errno));
            job.state=J_FAILED;return NULL;
        }
        FILE *out=fdopen(fd,"wb");
        if(!out){int saved=errno;close(fd);unlink(part);
            snprintf(job.error,sizeof job.error,"Cannot open output: %s",strerror(saved));
            job.state=J_FAILED;return NULL;
        }
        snprintf(job.filename,sizeof job.filename,"%s",src->filename);
        job.received=0;job.total=src->size;
        int err=rv_https_download(src->url,out,src->size,&job.cancel,&job.received,
                                  &job.total,job.error,sizeof job.error);
        if(fclose(out)!=0&&!err){
            err=-1;snprintf(job.error,sizeof job.error,"File flush failed: %s",strerror(errno));
        }
        if(job.cancel){unlink(part);job.state=J_CANCELED;return NULL;}
        if(!err&&job.received!=src->size){
            err=-1;snprintf(job.error,sizeof job.error,
                "Unexpected file size: %llu of %llu bytes",
                (unsigned long long)job.received,src->size);
        }
        if(!err){
            int verified=archive_file_digest(part,src);
            if(verified!=0){
                err=-1;snprintf(job.error,sizeof job.error,
                   verified>0?"Archive checksum mismatch":"Could not read file for checksum");
            }
        }
        if(!err && link(part,dest)<0){
            err=-1;snprintf(job.error,sizeof job.error,
               "Cannot finalize file without overwrite: %s",strerror(errno));
        }
        unlink(part);
        if(err){job.state=J_FAILED;return NULL;}
        snprintf(last,sizeof last,"roms/%s/%s",job.platform,src->filename);
    }
    snprintf(job.saved,sizeof job.saved,"%s",last);
    job.state=J_COMPLETED;
    return NULL;
}
static void start_archive_download(int sock,const char *body){
    char platform[24],identifier[180];platform[0]=identifier[0]=0;
    if(!json_bool(body,"rights_confirmed")||
       !json_str(body,"platform",platform,sizeof platform)||
       !json_str(body,"filename",identifier,sizeof identifier)){
        error(sock,400,"Selected archive file and permission confirmation are required");return;
    }
    const archive_source_t *src=find_archive_source(platform,identifier);
    if(!src||!safe_name(src->filename)){error(sock,400,"Archive file is not indexed");return;}
    pthread_mutex_lock(&lock);
    if(job.state==J_DOWNLOADING){pthread_mutex_unlock(&lock);error(sock,409,"A download is already active");return;}
    memset(&job,0,sizeof job);
    job.id=++sequence;job.state=J_DOWNLOADING;
    snprintf(job.platform,sizeof job.platform,"%s",platform);
    snprintf(job.path,sizeof job.path,"%s",identifier);
    snprintf(job.filename,sizeof job.filename,"%s",src->filename);
    job.total=src->size;
    pthread_t worker;
    if(pthread_create(&worker,NULL,archive_download_thread,NULL)){
        job.state=J_FAILED;pthread_mutex_unlock(&lock);error(sock,500,"Could not start archive transfer");return;
    }
    pthread_detach(worker);
    pthread_mutex_unlock(&lock);
    char answer[130];snprintf(answer,sizeof answer,
        "{\"ok\":true,\"id\":\"%u\",\"status\":\"downloading\"}",job.id);
    json(sock,200,answer);
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
    char platform[24],file[650],title[250],origin[16]="verified";title[0]=0;
    if(!json_str(body,"platform",platform,sizeof platform)||!json_str(body,"filename",file,sizeof file)||
       !json_bool(body,"rights_confirmed")){error(sock,400,"Platform, filename and rights confirmation are required");return;}
    json_str(body,"title",title,sizeof title);
    (void)json_str(body,"origin",origin,sizeof origin);
    if(!platform_ok(platform)){error(sock,400,"Unknown platform");return;}
    const int is_archive=!strcmp(origin,"archive");
    const verified_source_t *verified=NULL;
    const legacy_archive_source_t *archive=NULL;
    if(is_archive)archive=find_legacy_archive_source(platform,file);
    else if(!strcmp(origin,"verified"))verified=find_verified_source(platform,file);
    if(!archive&&!verified){error(sock,400,"File is not in the selected source allowlist");return;}
    pthread_mutex_lock(&lock);
    if(job.state==J_DOWNLOADING){pthread_mutex_unlock(&lock);error(sock,409,"A download is already running");return;}
    memset(&job,0,sizeof job);job.state=J_DOWNLOADING;job.id=++sequence;
    snprintf(job.platform,sizeof job.platform,"%s",platform);
    snprintf(job.path,sizeof job.path,"%s",file);
    snprintf(job.source_kind,sizeof job.source_kind,"%s",origin);
    snprintf(job.filename,sizeof job.filename,"%s",is_archive?archive->filename:verified->filename);
    pthread_t thread;
    int started=pthread_create(&thread,NULL,download_thread,NULL);
    if(started){job.state=J_FAILED;pthread_mutex_unlock(&lock);error(sock,500,"Could not create download worker");return;}
    pthread_detach(thread);pthread_mutex_unlock(&lock);
    char response_body[130];snprintf(response_body,sizeof response_body,"{\"ok\":true,\"id\":\"%u\",\"status\":\"downloading\"}",job.id);json(sock,200,response_body);
}
static void library_response(int sock){
    char *body=malloc(250000);if(!body){error(sock,500,"Out of memory");return;}
    size_t used=0;used+=(size_t)snprintf(body+used,250000-used,"{\"games\":[");
    int count=0;const char *p[]={"SNES","NES","GENESIS","GB","GBA","N64","GC","PS1","PS2","DC","ARCADE"};
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
    char platform[32],title[256],wanted[260];
    param(url,"platform",platform,sizeof platform);
    param(url,"title",title,sizeof title);
    if(!platform_ok(platform)){error(sock,400,"Unknown platform");return;}
    normalize(title,wanted,sizeof wanted);
    char body[24000];size_t used=(size_t)snprintf(body,sizeof body,
        "{\"supported\":true,\"files\":[");
    unsigned count=0;
    for(size_t i=0;i<verified_sources_count&&count<24;i++){
        const verified_source_t *src=&verified_sources[i];
        if(strcmp(platform,src->platform))continue;
        char norm[260];normalize(src->title,norm,sizeof norm);
        if(!wanted[0]||strcmp(wanted,norm))continue;
        char filename[700],name[500],provider[400],page[800];
        json_quote(src->path,filename,sizeof filename);
        json_quote(src->title,name,sizeof name);
        json_quote(src->provider,provider,sizeof provider);
        json_quote(src->source_page,page,sizeof page);
        int written=snprintf(body+used,sizeof body-used,
            "%s{\"filename\":%s,\"display_name\":%s,\"size\":%llu,\"source_kind\":\"verified\",\"provider\":%s,\"source_page\":%s,\"verified_sha256\":true}",
            count?",":"",filename,name,src->size,provider,page);
        if(written<0||(size_t)written>=sizeof body-used)break;
        used+=(size_t)written;count++;
    }
    if(!strcmp(platform,"GC")||!strcmp(platform,"SNES")){
        for(size_t i=0;i<legacy_archive_sources_count&&count<24;i++){
            const legacy_archive_source_t *src=&legacy_archive_sources[i];
            if(strcmp(platform,src->platform))continue;
            char game[240],norm[260];archive_title(src->filename,game,sizeof game);
            normalize(game,norm,sizeof norm);
            if(!wanted[0]||strcmp(wanted,norm))continue;
            char file[650],name[650];
            json_quote(src->filename,file,sizeof file);
            json_quote(src->filename,name,sizeof name);
            int written=snprintf(body+used,sizeof body-used,
                "%s{\"filename\":%s,\"display_name\":%s,\"source_kind\":\"archive\",\"provider\":\"Internet Archive\",\"verified_sha256\":false}",
                count?",":"",file,name);
            if(written<0||(size_t)written>=sizeof body-used)break;
            used+=(size_t)written;count++;
        }
    }
    snprintf(body+used,sizeof body-used,"]}");
    json(sock,200,body);
}
/* Source lists are compiled into the ELF. No browser leaves RetroVerse. */
static void host_games(int sock,const char *url){
    char platform[32],letter[8],host[32];
    param(url,"platform",platform,sizeof platform);
    param(url,"letter",letter,sizeof letter);
    param(url,"host",host,sizeof host);
    if(!platform_ok(platform)){error(sock,400,"Unknown platform");return;}
    const int is_archive=!strcmp(host,"archive");
    if(!is_archive && strcmp(host,"verified")){error(sock,400,"Unsupported source provider");return;}
    char body[65000];size_t used=(size_t)snprintf(body,sizeof body,
        "{\"host\":\"%s\",\"indexed\":true,\"games\":[",host);
    unsigned count=0;
    if(is_archive){
        for(size_t i=0;i<legacy_archive_sources_count&&count<180;i++){
            const legacy_archive_source_t *src=&legacy_archive_sources[i];
            if(strcmp(platform,src->platform))continue;
            char title[240];archive_title(src->filename,title,sizeof title);
            unsigned char first=(unsigned char)title[0];
            char initial=(char)(isalpha(first)?toupper(first):'#');
            if(letter[0] && toupper((unsigned char)letter[0])!=initial)continue;
            char qtitle[600],qfile[650];
            json_quote(title,qtitle,sizeof qtitle);
            json_quote(src->filename,qfile,sizeof qfile);
            int written=snprintf(body+used,sizeof body-used,
                "%s{\"title\":%s,\"filename\":%s,\"platform\":\"%s\",\"source_kind\":\"archive\",\"verified_sha256\":false}",
                count?",":"",qtitle,qfile,platform);
            if(written<0||(size_t)written>=sizeof body-used)break;
            used+=(size_t)written;count++;
        }
    }else{
        for(size_t i=0;i<verified_sources_count;i++){
            const verified_source_t *src=&verified_sources[i];
            if(strcmp(platform,src->platform))continue;
            unsigned char first=(unsigned char)src->title[0];
            char initial=(char)(isalpha(first)?toupper(first):'#');
            if(letter[0] && toupper((unsigned char)letter[0])!=initial)continue;
            char qtitle[500],qpath[700],qprovider[400],qpage[800];
            json_quote(src->title,qtitle,sizeof qtitle);
            json_quote(src->path,qpath,sizeof qpath);
            json_quote(src->provider,qprovider,sizeof qprovider);
            json_quote(src->source_page,qpage,sizeof qpage);
            int written=snprintf(body+used,sizeof body-used,
                "%s{\"title\":%s,\"filename\":%s,\"platform\":\"%s\",\"source_kind\":\"verified\",\"availability\":\"verified-source\",\"size\":%llu,\"provider\":%s,\"source_page\":%s}",
                count?",":"",qtitle,qpath,platform,src->size,qprovider,qpage);
            if(written<0||(size_t)written>=sizeof body-used)break;
            used+=(size_t)written;count++;
        }
    }
    snprintf(body+used,sizeof body-used,"]}");
    json(sock,200,body);
}
/* A real download test fetches a pinned homebrew file and checks SHA-256
 * before deleting the temporary copy. No commercial ROM archive is used. */

/* Two independent diagnostics avoid conflating local write permissions with HTTPS. */
static void diag_fail(int sock,const char *part,const char *step,int code) {
    char msg[350];
    snprintf(msg,sizeof msg,"{\"ok\":false,\"component\":\"%s\",\"step\":\"%s\",\"code\":%d,\"detail\":\"%s\"}",
        part,step,code,(code>0?strerror(code):"Native PS5 operation failed"));
    json(sock,200,msg);
}
static void diagnostic_storage(int sock) {
    if(mkdir(BASE_DIR,0700)<0&&errno!=EEXIST){diag_fail(sock,"storage","mkdir /data/retroverse",errno);return;}
    if(mkdir(ROM_DIR,0700)<0&&errno!=EEXIST){diag_fail(sock,"storage","mkdir roms",errno);return;}
    const char *folder=ROM_DIR "/SNES";
    if(mkdir(folder,0700)<0&&errno!=EEXIST){diag_fail(sock,"storage","mkdir SNES",errno);return;}
    char filename[512];
    snprintf(filename,sizeof filename,"%s/.retroverse-test-%ld.tmp",folder,(long)getpid());
    const unsigned char payload[]="RetroVerse PS5 storage verification v0.5";
    int fd=open(filename,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0){diag_fail(sock,"storage","open temporary file",errno);return;}
    ssize_t written=write(fd,payload,sizeof payload);
    int saved_errno=errno;
    if(written!=(ssize_t)sizeof payload){
        close(fd);unlink(filename);
        diag_fail(sock,"storage","write temporary file",written<0?saved_errno:EIO);
        return;
    }
    if(close(fd)<0){int e=errno;unlink(filename);diag_fail(sock,"storage","close temporary file",e);return;}
    fd=open(filename,O_RDONLY);
    if(fd<0){int e=errno;unlink(filename);diag_fail(sock,"storage","reopen temporary file",e);return;}
    unsigned char check[sizeof payload];
    ssize_t n=read(fd,check,sizeof check);
    saved_errno=errno;close(fd);
    int mismatched=n!=(ssize_t)sizeof payload||memcmp(payload,check,sizeof payload)!=0;
    int erased=unlink(filename);
    if(mismatched){diag_fail(sock,"storage","verify temporary file",n<0?saved_errno:EIO);return;}
    if(erased<0){diag_fail(sock,"storage","remove temporary file",errno);return;}
    char ok[250];snprintf(ok,sizeof ok,
        "{\"ok\":true,\"component\":\"storage\",\"location\":\"/data/retroverse/roms/SNES\",\"bytes_verified\":%llu,\"removed\":true}",
        (unsigned long long)sizeof payload);
    json(sock,200,ok);
}
static void diagnostic_network(int sock) {
    const char *url=verified_sources[0].url;
    Net net;int rc=net_open(&net,url);
    if(rc<0){
        net_close(&net);
        char buf[250];snprintf(buf,sizeof buf,
         "{\"ok\":false,\"component\":\"https\",\"step\":\"setup\",\"sdk_stage\":%d}",rc);
        json(sock,200,buf);return;
    }
    rc=sceHttpSendRequest(net.req,NULL,0);
    if(rc<0){
        net_close(&net);
        char buf[250];snprintf(buf,sizeof buf,
         "{\"ok\":false,\"component\":\"https\",\"step\":\"send\",\"native_hex\":\"0x%08x\"}",(unsigned int)rc);
        json(sock,200,buf);return;
    }
    int status=0;
    rc=sceHttpGetStatusCode(net.req,&status);
    if(rc<0){
        net_close(&net);
        char buf[250];snprintf(buf,sizeof buf,
          "{\"ok\":false,\"component\":\"https\",\"step\":\"http status\",\"native_hex\":\"0x%08x\"}",(unsigned int)rc);
        json(sock,200,buf);return;
    }
    if(status!=200){
        net_close(&net);
        char buf[250];snprintf(buf,sizeof buf,
          "{\"ok\":false,\"component\":\"https\",\"step\":\"response status\",\"http_status\":%d}",status);
        json(sock,200,buf);return;
    }
    unsigned char buffer[4097];size_t size=0;int first=1;int invalid=0;
    for(;;){
        int got=sceHttpReadData(net.req,buffer,sizeof buffer-1);
        if(got<0){
            net_close(&net);
            char buf[250];snprintf(buf,sizeof buf,
              "{\"ok\":false,\"component\":\"https\",\"step\":\"read\",\"native_hex\":\"0x%08x\",\"bytes_received\":%llu}",
              (unsigned int)got,(unsigned long long)size);
            json(sock,200,buf);return;
        }
        if(got==0)break;
        if(first){first=0;}
        size+=(size_t)got;
        if(size>verified_sources[0].size){invalid=1;break;}
    }
    net_close(&net);
    if(invalid||size!=verified_sources[0].size){
        json(sock,200,"{\"ok\":false,\"component\":\"https\",\"step\":\"content validation\"}");return;
    }
    char out[260];snprintf(out,sizeof out,
      "{\"ok\":true,\"component\":\"https\",\"http_status\":200,\"bytes_received\":%llu}",
      (unsigned long long)size);
    json(sock,200,out);
}

static void diagnostic_curl(int sock){
    const char *url=verified_sources[0].url;
    unsigned char *data=NULL;size_t len=0;
    char message[400]={0};
    int rc=rv_https_get_memory(url,2u*1024u*1024u,&data,&len,message,sizeof message);
    if(rc<0){
        char quoted[850],body[1050];
        json_quote(message,quoted,sizeof quoted);
        snprintf(body,sizeof body,"{\"ok\":false,\"component\":\"curl\",\"step\":\"https GET\",\"detail\":%s}",quoted);
        json(sock,200,body);return;
    }
    int valid=len==verified_sources[0].size && verify_sha256_memory(data,len,verified_sources[0].sha256);
    free(data);
    if(!valid){json(sock,200,"{\"ok\":false,\"component\":\"curl\",\"step\":\"content verification\"}");return;}
    char body[250];snprintf(body,sizeof body,
     "{\"ok\":true,\"component\":\"curl\",\"bytes_received\":%llu,\"verified\":true}",
     (unsigned long long)len);
    json(sock,200,body);
}
static void test_download(int sock){
    if(mkdir(BASE_DIR,0700)<0&&errno!=EEXIST){
        error(sock,500,"Cannot create RetroVerse data folder");return;
    }
    const verified_source_t *source=&verified_sources[0];
    char path[512];snprintf(path,sizeof path,"%s/.verified-download-test-%ld.part",BASE_DIR,(long)getpid());
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0){error(sock,500,"Cannot create PS5 transfer test file");return;}
    FILE *f=fdopen(fd,"wb");
    if(!f){close(fd);unlink(path);error(sock,500,"Cannot open transfer test stream");return;}
    volatile int cancel=0;
    volatile uint64_t received=0,total=source->size;
    char err[400]={0};
    int rc=rv_https_download(source->url,f,source->size,&cancel,&received,&total,err,sizeof err);
    if(fclose(f)!=0&&rc==0){rc=-1;snprintf(err,sizeof err,"Failed to flush test file");}
    if(!rc&&(received!=source->size||verify_sha256_file(path,source->sha256)!=0)){
        rc=-1;snprintf(err,sizeof err,"Downloaded bytes failed expected size/SHA-256");
    }
    unlink(path);
    if(rc<0){
        char quoted[900],body[1100];
        json_quote(err,quoted,sizeof quoted);
        snprintf(body,sizeof body,"{\"error\":%s,\"step\":\"verified-download\"}",quoted);
        json(sock,502,body);return;
    }
    char body[300];snprintf(body,sizeof body,
        "{\"ok\":true,\"host\":\"developer-github\",\"transport\":\"libcurl\",\"http_status\":200,\"bytes_written\":%llu,\"verified\":true,\"test_file_removed\":true}",
        (unsigned long long)received);
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

/* Read-only artwork proxy for installed foreground app.
 * Strictly whitelisted thumbnail host + system name, bounded PNG size.
 */
static void cover_image(int sock,const char *request_url){
    char platform[32],filename[350];
    param(request_url,"platform",platform,sizeof platform);
    param(request_url,"filename",filename,sizeof filename);
    const char *system=libretro_system(platform);
    if(!system||!filename[0]||strlen(filename)>240
       ||strstr(filename,"..")||strchr(filename,'/')||strchr(filename,'\\')){
        error(sock,400,"Invalid cover request");return;
    }
    char sysenc[400],nameenc[900],url[1600];
    percent_encode(system,sysenc,sizeof sysenc,0);
    percent_encode(filename,nameenc,sizeof nameenc,0);
    snprintf(url,sizeof url,"https://thumbnails.libretro.com/%s/Named_Boxarts/%s.png",
             sysenc,nameenc);
    unsigned char *data=NULL;size_t length=0;
    char err[260]={0};
    int rc=rv_https_get_memory(url,4u*1024u*1024u,&data,&length,err,sizeof err);
    if(rc!=0||length<8||memcmp(data,"\x89PNG\r\n\x1a\n",8)){
        free(data);error(sock,404,"Cover not available or artwork host unreachable");return;
    }
    response(sock,200,"OK","image/png",data,length);
    free(data);
}

/* Local torrent engine bridge; the remote service does not scrape source pages.
 * Files are saved by PS5Torrent under /data/retroverse/torrents/. */
static void torrent_request(int sock,const char *route,const char *method,const char *body){
    char magnet[1850]={0},action[20]={0};int id=-1;
    if(!strcmp(route,"add")){
        if(!body||!json_str(body,"magnet",magnet,sizeof magnet)||
           !json_bool(body,"rights_confirmed")){
            error(sock,400,"Magnet URI and download rights confirmation required");return;
        }
        if(mkdir(BASE_DIR,0700)<0&&errno!=EEXIST){
            error(sock,500,"Cannot create RetroVerse torrent storage folder");return;
        }
    }
    if(!strcmp(route,"action")){
        char idstr[20]={0};
        if(!body||!json_str(body,"action",action,sizeof action)||
           !json_str(body,"id",idstr,sizeof idstr)){
            error(sock,400,"Torrent ID and action are required");return;
        }
        for(size_t i=0;idstr[i];i++)if(!isdigit((unsigned char)idstr[i])){
            error(sock,400,"Invalid torrent ID");return;
        }
        if(!idstr[0]){error(sock,400,"Empty torrent ID");return;}
        long num=strtol(idstr,NULL,10);
        if(num<0||num>10000){error(sock,400,"Torrent ID out of range");return;}
        id=(int)num;
        if(strcmp(action,"start")&&strcmp(action,"stop")&&strcmp(action,"remove")){
            error(sock,400,"Unsupported torrent action");return;
        }
        route=action;
    }
    char *result=NULL,details[256]={0};size_t n=0;int upstream=0;
    int rc=rv_torrent_request(method,route,magnet,id,&result,&n,&upstream,
                              details,sizeof details);
    if(rc!=0){
        char quoted[560],output[700];json_quote(details,quoted,sizeof quoted);
        snprintf(output,sizeof output,"{\"error\":%s}",quoted);
        json(sock,rc==-1?400:503,output);return;
    }
    int code=(upstream>=200&&upstream<300)?200:
             (upstream>=400&&upstream<500)?400:502;
    response(sock,code,code==200?"OK":"Error","application/json; charset=utf-8",
             result,n);free(result);
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
        if(!strcmp(path,"/api/health")){json(sock,200,"{\"ok\":true,\"mode\":\"ps5\",\"version\":\"0.11\",\"features\":[\"catalog\",\"art\",\"verified_sources\",\"sha256_downloads\",\"torrent_bridge\",\"upload\",\"library\"]}");return;}
        if(!strcmp(path,"/api/cover")){cover_image(sock,url);return;}
        if(!strcmp(path,"/api/catalog-index")){catalog_index(sock,url);return;}
        if(!strcmp(path,"/api/host-games")){host_games(sock,url);return;}
        if(!strcmp(path,"/api/curated/matches")){curated_matches(sock,url);return;}
        if(!strcmp(path,"/api/jobs")){jobs_response(sock);return;}
        if(!strcmp(path,"/api/library")){library_response(sock);return;}
        if(!strcmp(path,"/api/torrent/status")){torrent_request(sock,"status","GET",NULL);return;}
        if(!strcmp(path,"/api/torrent/list")){torrent_request(sock,"list","GET",NULL);return;}
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
        if(!strcmp(path,"/api/archive/download")){start_archive_download(sock,data);return;}
        if(!strcmp(path,"/api/torrent/add")){torrent_request(sock,"add","POST",data);return;}
        if(!strcmp(path,"/api/torrent/action")){torrent_request(sock,"action","POST",data);return;}
        if(!strcmp(path,"/api/test-download")){test_download(sock);return;}
        if(!strcmp(path,"/api/diagnostics/storage")){diagnostic_storage(sock);return;}
        if(!strcmp(path,"/api/diagnostics/network")){diagnostic_network(sock);return;}
        if(!strcmp(path,"/api/diagnostics/curl")){diagnostic_curl(sock);return;}
        if(!strcmp(path,"/api/downloads/cancel")){job.cancel=1;json(sock,200,"{\"ok\":true}");return;}
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
