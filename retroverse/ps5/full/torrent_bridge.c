/* RetroVerse-to-PS5Torrent loopback API bridge.
 * PS5Torrent (GPL-3.0): https://github.com/Phoenixx1202/PS5Torrent
 * No external site is fetched by this code. No arbitrary URL/path forwarding.
 */
#include "torrent_bridge.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define TORRENT_SERVICE_PORT 12389
#define TORRENT_RESPONSE_CAP 65536
static int send_all(int fd,const char *data,size_t len){
    while(len){ssize_t n=send(fd,data,len,0);if(n<=0)return -1;data+=n;len-=(size_t)n;}
    return 0;
}
static int make_form(const char *magnet,char *out,size_t cap){
    static const char hex[]="0123456789ABCDEF";
    if(!magnet||strncmp(magnet,"magnet:?",8))return -1;
    /* Require a real BitTorrent info hash before accepting the user input. */
    if(!strstr(magnet,"xt=urn:btih:")&&!strstr(magnet,"xt=urn:btmh:"))return -1;
    size_t k=0,n=strlen(magnet);
    if(n<20||n>1800||cap<64)return -1;
    memcpy(out,"magnet=",7);k=7;
    for(size_t i=0;i<n;i++){
        unsigned char c=(unsigned char)magnet[i];
        if(c<33||c>126||c=='\\')return -1;
        if(k+5>=cap)return -1;
        if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')out[k++]=(char)c;
        else{out[k++]='%';out[k++]=hex[c>>4];out[k++]=hex[c&15];}
    }
    static const char suffix[]="&path=%2Fdata%2Fretroverse";
    if(k+sizeof suffix>=cap)return -1;
    memcpy(out+k,suffix,sizeof suffix);
    return 0;
}
int rv_torrent_request(const char *method,const char *route,const char *magnet,
                       int id,char **json_out,size_t *json_len,int *status,
                       char *err,size_t errlen){
    *json_out=NULL;*json_len=0;*status=0;
    const char *path=NULL,*verb=NULL;
    char pathbuf[128],form[6500]={0};
    if(!strcmp(route,"status")){path="/api/status";verb="GET";}
    else if(!strcmp(route,"list")){path="/api/torrents";verb="GET";}
    else if(!strcmp(route,"add")){
        path="/api/torrents/add";verb="POST";
        if(make_form(magnet,form,sizeof form)<0){
            snprintf(err,errlen,"Invalid magnet URI (BTIH or BTMH hash required)");return -1;
        }
    }else if(!strcmp(route,"start")||!strcmp(route,"stop")||!strcmp(route,"remove")){
        if(id<0||id>10000){snprintf(err,errlen,"Invalid torrent ID");return -1;}
        snprintf(pathbuf,sizeof pathbuf,"/api/torrents/%d/%s",id,route);
        path=pathbuf;verb="POST";
    }else{snprintf(err,errlen,"Unsupported torrent command");return -1;}
    if(strcmp(method,verb)){snprintf(err,errlen,"Unsupported method");return -1;}
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0){snprintf(err,errlen,"Cannot open local torrent socket");return -2;}
    struct timeval tv={.tv_sec=3,.tv_usec=0};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof tv);
    struct sockaddr_in addr={0};
    addr.sin_family=AF_INET;addr.sin_port=htons(TORRENT_SERVICE_PORT);
    addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(fd,(struct sockaddr*)&addr,sizeof addr)<0){
        close(fd);snprintf(err,errlen,"PS5Torrent is offline (start PS5Torrent.elf, port 12389)");return -2;
    }
    char request[1024];size_t bodylen=strlen(form);
    int head=snprintf(request,sizeof request,
        "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %zu\r\n\r\n",
        verb,path,bodylen);
    if(head<=0||(size_t)head>=sizeof request||send_all(fd,request,(size_t)head)<0||
       (bodylen&&send_all(fd,form,bodylen)<0)){
        close(fd);snprintf(err,errlen,"Cannot send request to PS5Torrent");return -2;
    }
    char *raw=malloc(TORRENT_RESPONSE_CAP+1);
    if(!raw){close(fd);snprintf(err,errlen,"Out of memory");return -2;}
    size_t count=0;
    while(count<TORRENT_RESPONSE_CAP){
        ssize_t n=recv(fd,raw+count,TORRENT_RESPONSE_CAP-count,0);
        if(n<0){free(raw);close(fd);snprintf(err,errlen,"PS5Torrent response timed out");return -2;}
        if(n==0)break;
        count+=(size_t)n;
    }
    close(fd);raw[count]=0;
    if(count==TORRENT_RESPONSE_CAP){free(raw);snprintf(err,errlen,"Torrent response exceeded safe limit");return -2;}
    char *sep=strstr(raw,"\r\n\r\n");
    int code=0;
    if(!sep||sscanf(raw,"HTTP/%*d.%*d %d",&code)!=1||code<200||code>599){
        free(raw);snprintf(err,errlen,"Malformed PS5Torrent response");return -2;
    }
    /* PS5Torrent's API sends Content-Length, not chunked responses. */
    size_t offset=(size_t)(sep-raw)+4,actual=count-offset;
    long declared=-1;
    for(char *p=raw; p<sep; ){
        char *end=strstr(p,"\r\n");if(!end||end>sep)break;
        if((size_t)(end-p)>15&&!strncasecmp(p,"Content-Length:",15)){
            declared=strtol(p+15,NULL,10);
            break;
        }
        p=end+2;
    }
    if(declared<0||(size_t)declared!=actual||!actual||
       (raw[offset]!='{'&&raw[offset]!='[')){
        free(raw);snprintf(err,errlen,"Invalid or incomplete PS5Torrent JSON response");return -2;
    }
    memmove(raw,raw+offset,actual);raw[actual]=0;
    *json_out=raw;*json_len=actual;*status=code;
    return 0;
}
