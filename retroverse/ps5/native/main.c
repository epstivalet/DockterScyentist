/* RetroVerse TV Alpha — native PS5 SDL2 frontend
 * SPDX-License-Identifier: GPL-3.0-or-later
 * One ELF: SDL2 TV interface + separate thread running the proven HTTP backend.
 * X: details/confirm download  O: back  L1/R1: console  TRIANGLE: tabs
 * D-pad up/down: game  left/right: letter  OPTIONS: exit
 */
#define SDL_MAIN_HANDLED 1
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "../full/curl_net.h"

#define W 1920
#define H 1080
#define MAX_GAMES 18000
#define HOST_CAP 500
#define RESPONSE_CAP (10u*1024u*1024u)
int retroverse_web_server_main(void);

typedef struct {char title[180],filename[280],path[550];} Game;
typedef struct {const char *id,*name,*thumb;uint8_t r,g,b;} System;
static const System consoles[]={
 {"SNES","SUPER NINTENDO","Nintendo - Super Nintendo Entertainment System",171,147,251},
 {"NES","NINTENDO NES","Nintendo - Nintendo Entertainment System",241,111,104},
 {"GENESIS","SEGA GENESIS","Sega - Mega Drive - Genesis",239,96,148},
 {"GB","GAME BOY","Nintendo - Game Boy",170,216,118},
 {"GBA","GAME BOY ADVANCE","Nintendo - Game Boy Advance",178,150,251},
 {"N64","NINTENDO 64","Nintendo - Nintendo 64",110,196,235},
 {"PS1","PLAYSTATION","Sony - PlayStation",120,171,248},
 {"PS2","PLAYSTATION 2","Sony - PlayStation 2",123,149,237},
 {"DC","DREAMCAST","Sega - Dreamcast",251,166,108},
 {"ARCADE","ARCADE","MAME",239,200,95}
};
#define NC ((int)(sizeof consoles/sizeof consoles[0]))
static Game *games;
static int n_games=0,sys=0,sel=0,screen_tab=0,detail=0,host_only=0,letter='A';
static char status_line[700]="STARTING RETROVERSE BACKEND...",source_path[600]="",job_status[700]="";
static SDL_Window *window;
static SDL_Renderer *ren;
static SDL_Texture *cover=NULL;
static SDL_GameController *pad=NULL;

static const char *glyphs="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,:!?-/[]()+#%=_>";
static const unsigned char bitmap[][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{15,16,16,16,16,16,15},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {15,16,16,23,17,17,15},{17,17,17,31,17,17,17},{31,4,4,4,4,4,31},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14},
 {0,0,0,0,0,0,0},{0,0,0,0,0,12,12},{0,0,0,12,12,0,12},
 {0,0,0,0,12,4,8},{0,0,4,0,4,0,0},{14,17,1,2,4,0,4},
 {0,0,0,31,0,0,0},{1,2,4,8,16,0,0},{14,8,8,8,8,8,14},
 {14,2,2,2,2,2,14},{0,4,14,21,4,0,0},{0,4,4,31,4,4,0},
 {0,0,31,0,31,0,0},{17,18,4,8,19,0,0},{31,1,2,4,8,16,31},
 {0,0,0,31,0,0,0},{0,0,31,0,31,0,0},{0,4,2,31,2,4,0}
};
static void color(uint8_t r,uint8_t g,uint8_t b,uint8_t a){SDL_SetRenderDrawColor(ren,r,g,b,a);}
static void rect(int x,int y,int w,int h,uint8_t r,uint8_t g,uint8_t b){
    SDL_Rect box={x,y,w,h};color(r,g,b,255);SDL_RenderFillRect(ren,&box);
}
static void outline(int x,int y,int w,int h,uint8_t r,uint8_t g,uint8_t b){
    SDL_Rect box={x,y,w,h};color(r,g,b,255);SDL_RenderDrawRect(ren,&box);
}
static void text(int x,int y,const char *s,int scale,uint8_t r,uint8_t g,uint8_t b,int maxchars){
    if(!s)return;int step=scale*6;int i=0;
    color(r,g,b,255);
    for(;*s&&i<maxchars;s++,i++){
        unsigned char ch=(unsigned char)toupper((unsigned char)*s);
        const char *found=strchr(glyphs,ch);
        int gi=found?(int)(found-glyphs):37;
        if(gi >= (int)(sizeof bitmap/sizeof bitmap[0]))gi=37;
        for(int row=0;row<7;row++){
            unsigned char bits=bitmap[gi][row];
            for(int col=0;col<5;col++)if(bits&(1<<(4-col))){
                SDL_Rect p={x+i*step+col*scale,y+row*scale,scale,scale};
                SDL_RenderFillRect(ren,&p);
            }
        }
    }
}
static void *backend(void *unused){(void)unused;retroverse_web_server_main();return NULL;}
static void uri_encode(const char *src,char *dest,size_t cap){
    const char *hex="0123456789ABCDEF";size_t n=0;
    for(size_t i=0;src[i]&&n+4<cap;i++){
        unsigned char c=(unsigned char)src[i];
        if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')dest[n++]=c;
        else{dest[n++]='%';dest[n++]=hex[c>>4];dest[n++]=hex[c&15];}
    }dest[n]=0;
}
static void uri_decode(const char *src,char *dest,size_t cap){
    size_t n=0;
    for(int i=0;src[i]&&n+1<cap;i++){
        if(src[i]=='%'&&isxdigit((unsigned char)src[i+1])&&isxdigit((unsigned char)src[i+2])){
            int a=src[i+1],b=src[i+2];
            a=isdigit(a)?a-'0':toupper(a)-'A'+10;
            b=isdigit(b)?b-'0':toupper(b)-'A'+10;
            dest[n++]=(char)(a*16+b);i+=2;
        }else if(src[i]=='+')dest[n++]=' ';
        else dest[n++]=src[i];
    }dest[n]=0;
}
static char *local_request(const char *method,const char *path,const char *body){
    int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return NULL;
    struct timeval timeout={.tv_sec=8,.tv_usec=0};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
    struct sockaddr_in addr={0};addr.sin_family=AF_INET;
    addr.sin_port=htons(8765);inet_pton(AF_INET,"127.0.0.1",&addr.sin_addr);
    if(connect(fd,(void *)&addr,sizeof addr)<0){close(fd);return NULL;}
    char headers[1600];size_t len=body?strlen(body):0;
    int header_len=snprintf(headers,sizeof headers,
        "%s %s HTTP/1.1\r\nHost: localhost\r\nContent-Length: %zu\r\n"
        "Content-Type: application/json\r\nConnection: close\r\n\r\n",
        method,path,len);
    if(header_len<=0||header_len>=(int)sizeof headers){close(fd);return NULL;}
    if(send(fd,headers,header_len,0)<0){close(fd);return NULL;}
    if(body && send(fd,body,len,0)<0){close(fd);return NULL;}
    char *data=malloc(RESPONSE_CAP+1);if(!data){close(fd);return NULL;}
    size_t got=0;
    while(got<RESPONSE_CAP){
        ssize_t n=recv(fd,data+got,RESPONSE_CAP-got,0);
        if(n<=0)break;got+=(size_t)n;
    }
    close(fd);data[got]=0;
    char *payload=strstr(data,"\r\n\r\n");
    if(!payload){free(data);return NULL;}
    payload+=4;
    memmove(data,payload,got-(size_t)(payload-data)+1);
    return data;
}
static void title_from_name(const char *name,char *out,size_t n){
    size_t i=0;
    for(;name[i]&&i+1<n;i++){if(name[i]=='('||name[i]=='[')break;out[i]=name[i];}
    while(i>0&&(out[i-1]==' '||out[i-1]=='.'))i--;
    out[i]=0;
}
static int pick_count(void) {
    int count=0;
    for(int i=0;i<n_games;i++){
        int c=toupper((unsigned char)games[i].title[0]);
        if((letter=='#'&&!isalpha(c))||(letter!='#'&&c==letter))count++;
    }
    return count;
}
static int game_index(int nth){
    for(int i=0;i<n_games;i++){
        int c=toupper((unsigned char)games[i].title[0]);
        if((letter=='#'&&!isalpha(c))||(letter!='#'&&c==letter)){
            if(nth--==0)return i;
        }
    }
    return -1;
}
static Game *selected(void){int i=game_index(sel);return i>=0?games+i:NULL;}
static void parse_catalog(const char *body){
    n_games=0;
    const char *p=body;
    while((p=strstr(p,"href=\""))&&n_games<MAX_GAMES){
        p+=6;const char *end=strchr(p,'"');if(!end)break;
        size_t n=(size_t)(end-p);
        if(n>4&&n<500){
            char encoded[520],decoded[300];
            memcpy(encoded,p,n);encoded[n]=0;uri_decode(encoded,decoded,sizeof decoded);
            size_t len=strlen(decoded);
            if(len>4&&!strcasecmp(decoded+len-4,".png")){
                decoded[len-4]=0;
                Game *g=&games[n_games];
                snprintf(g->filename,sizeof g->filename,"%s",decoded);
                title_from_name(decoded,g->title,sizeof g->title);
                if(g->title[0])n_games++;
            }
        }p=end+1;
    }
}
static int json_string(const char *object,const char *name,char *out,size_t cap){
    char token[90];snprintf(token,sizeof token,"\"%s\"",name);
    const char *p=strstr(object,token);if(!p)return 0;
    p=strchr(p,':');if(!p)return 0;p++;while(*p==' ')p++;
    if(*p++!='"')return 0;
    size_t k=0;
    while(*p&&*p!='"'&&k+1<cap){
        if(*p=='\\'&&p[1])p++;
        out[k++]=*p++;
    }out[k]=0;return 1;
}
static void parse_host(const char *body){
    n_games=0;
    const char *p=body;
    while((p=strstr(p,"\"title\""))&&n_games<HOST_CAP) {
        const char *next=strchr(p,'}');
        if(!next)break;
        char obj[1400];size_t z=(size_t)(next-p);
        if(z<sizeof obj){
            memcpy(obj,p,z);obj[z]=0;
            Game *g=&games[n_games];
            if(json_string(obj,"title",g->title,sizeof g->title)&&
                json_string(obj,"filename",g->path,sizeof g->path)){
                snprintf(g->filename,sizeof g->filename,"%s",g->title);
                n_games++;
            }
        }p=next+1;
    }
}
static void load_games(void){
    n_games=0;sel=0;detail=0;source_path[0]=0;
    char path[800];
    if(screen_tab==0){
        snprintf(path,sizeof path,"/api/catalog-index?platform=%s",consoles[sys].id);
        char *data=local_request("GET",path,NULL);
        if(!data){snprintf(status_line,sizeof status_line,"FAILED LOADING EMBEDDED CATALOG");return;}
        parse_catalog(data);free(data);
    }else if(screen_tab==1){
        snprintf(path,sizeof path,"/api/host-games?host=archive&platform=%s&letter=%c",
                 consoles[sys].id,letter);
        char *data=local_request("GET",path,NULL);
        if(!data){snprintf(status_line,sizeof status_line,"HOST INDEX UNAVAILABLE");return;}
        parse_host(data);free(data);
    }else if(screen_tab==2){
        char *data=local_request("GET","/api/library",NULL);
        if(!data){snprintf(status_line,sizeof status_line,"LIBRARY API UNAVAILABLE");return;}
        n_games=0;const char *p=data;
        while((p=strstr(p,"\"platform\""))&&n_games<MAX_GAMES) {
            const char *end=strchr(p,'}');if(!end)break;
            char obj[600];size_t n=(size_t)(end-p);
            if(n<sizeof obj){
                memcpy(obj,p,n);obj[n]=0;
                char platform[50],name[280];
                if(json_string(obj,"platform",platform,sizeof platform)&&
                   json_string(obj,"filename",name,sizeof name)){
                    Game *g=&games[n_games];
                    snprintf(g->filename,sizeof g->filename,"%s",name);
                    title_from_name(name,g->title,sizeof g->title);
                    snprintf(g->path,sizeof g->path,"%s/%s",platform,name);
                    n_games++;
                }
            }
            p=end+1;
        }free(data);
    }else{
        char *data=local_request("GET","/api/jobs",NULL);
        if(data){
            char status[100],name[150],failure[250];
            if(json_string(data,"status",status,sizeof status)){
                json_string(data,"filename",name,sizeof name);
                if(!json_string(data,"error",failure,sizeof failure))failure[0]=0;
                snprintf(job_status,sizeof job_status,"%s   %s   %s",status,name,failure);
            }else snprintf(job_status,sizeof job_status,"NO ACTIVE DOWNLOADS");
            free(data);
        }else snprintf(job_status,sizeof job_status,"DOWNLOAD MANAGER UNAVAILABLE");
    }
    snprintf(status_line,sizeof status_line,"%d GAME ENTRIES   %s",
             n_games,screen_tab==1?"INDEXED SOURCE FILES":"LOCAL CATALOG");
}
static void check_source(void) {
    source_path[0]=0;
    Game *g=selected();
    if(!g)return;
    if(screen_tab==1){snprintf(source_path,sizeof source_path,"%s",g->path);return;}
    if(screen_tab!=0)return;
    char encoded[450],path[900];
    uri_encode(g->title,encoded,sizeof encoded);
    snprintf(path,sizeof path,"/api/curated/matches?platform=%s&title=%s",
             consoles[sys].id,encoded);
    char *data=local_request("GET",path,NULL);
    if(!data){snprintf(status_line,sizeof status_line,"SOURCE LOOKUP UNAVAILABLE");return;}
    if(!json_string(data,"filename",source_path,sizeof source_path))
        snprintf(status_line,sizeof status_line,"NO MATCHING CURATED DOWNLOAD FOR THIS GAME");
    else snprintf(status_line,sizeof status_line,"CURATED SOURCE FOUND   PRESS X AGAIN TO DOWNLOAD");
    free(data);
}
static void download_selected(void){
    Game *g=selected();
    if(!g || !source_path[0]){snprintf(status_line,sizeof status_line,"NO SOURCE FOUND FOR SELECTED GAME");return;}
    char filename[1300],title[400],json[2200];
    snprintf(filename,sizeof filename,"%s",source_path);
    /* Curated filenames from trusted pre-indexed paths: no raw user supplied URL. */
    snprintf(title,sizeof title,"%s",g->title);
    /* Escape quotes for JSON before posting. */
    for(char *p=filename;*p;p++)if(*p=='"')*p=' ';
    for(char *p=title;*p;p++)if(*p=='"')*p=' ';
    snprintf(json,sizeof json,
             "{\"platform\":\"%s\",\"filename\":\"%s\",\"title\":\"%s\",\"rights_confirmed\":true}",
             consoles[sys].id,filename,title);
    char *response=local_request("POST","/api/curated/download",json);
    if(response&&strstr(response,"\"ok\":true"))
        snprintf(status_line,sizeof status_line,"TRANSFER STARTED. OPEN DOWNLOADS TO CHECK STATUS");
    else snprintf(status_line,sizeof status_line,"DOWNLOAD REJECTED OR ANOTHER TRANSFER IS ACTIVE");
    free(response);detail=0;
}
typedef struct {pthread_mutex_t mutex;char url[1200];unsigned char *bytes;size_t length;int ready,busy,generation,loaded;} Art;
static Art art={.mutex=PTHREAD_MUTEX_INITIALIZER};
static void *art_thread(void *ptr){
    char url[1200];int generation;
    pthread_mutex_lock(&art.mutex);
    snprintf(url,sizeof url,"%s",art.url);generation=art.generation;
    pthread_mutex_unlock(&art.mutex);
    unsigned char *bytes=NULL;size_t size=0;char error[200]={0};
    rv_https_get_memory(url,4u*1024u*1024u,&bytes,&size,error,sizeof error);
    pthread_mutex_lock(&art.mutex);
    if(generation==art.generation){
        free(art.bytes);art.bytes=bytes;art.length=size;art.ready=1;
    }else free(bytes);
    art.busy=0;
    pthread_mutex_unlock(&art.mutex);
    return NULL;
}
static void prepare_art(void){
    Game *g=selected();
    if(!g)return;
    char encoded_sys[300],encoded_title[520],url[1200];
    uri_encode(consoles[sys].thumb,encoded_sys,sizeof encoded_sys);
    uri_encode(g->filename,encoded_title,sizeof encoded_title);
    snprintf(url,sizeof url,"https://thumbnails.libretro.com/%s/Named_Boxarts/%s.png",
             encoded_sys,encoded_title);
    pthread_mutex_lock(&art.mutex);
    if(strcmp(url,art.url)){
        snprintf(art.url,sizeof art.url,"%s",url);
        art.generation++;art.loaded=0;
        if(cover){SDL_DestroyTexture(cover);cover=NULL;}
    }
    if(!art.busy && !art.loaded){
        art.busy=1;art.ready=0;art.loaded=1;
        pthread_t worker;
        if(!pthread_create(&worker,NULL,art_thread,NULL))pthread_detach(worker);
        else art.busy=0;
    }
    pthread_mutex_unlock(&art.mutex);
}
static void update_art(void){
    pthread_mutex_lock(&art.mutex);
    if(!art.ready){pthread_mutex_unlock(&art.mutex);return;}
    art.ready=0;
    unsigned char *bytes=art.bytes;size_t length=art.length;
    art.bytes=NULL;
    pthread_mutex_unlock(&art.mutex);
    if(bytes&&length&&length<4u*1024u*1024u){
        SDL_RWops *rw=SDL_RWFromConstMem(bytes,(int)length);
        if(rw){
            SDL_Surface *surface=IMG_Load_RW(rw,1);
            if(surface){if(cover)SDL_DestroyTexture(cover);
                cover=SDL_CreateTextureFromSurface(ren,surface);SDL_FreeSurface(surface);}
        }
    }free(bytes);
}
static void screen_fill(void){
    for(int y=0;y<H;y+=4){
        rect(0,y,W,4,(uint8_t)(7+y/250),(uint8_t)(11+y/200),(uint8_t)(26+y/110));
    }
}
static void render(void){
    const System *c=&consoles[sys];
    screen_fill();
    rect(0,0,W,113,11,16,31);rect(0,112,W,4,c->r,c->g,c->b);
    text(55,24,"RETROVERSE",9,244,246,255,12);
    text(1340,36,"PS5 NATIVE TV ALPHA",4,c->r,c->g,c->b,24);
    const char *tabs[]={"STORE","HOSTS","MY LIBRARY","DOWNLOADS"};
    for(int i=0;i<4;i++){
        int x=54+i*304;
        rect(x,145,284,64,i==screen_tab?c->r:34,i==screen_tab?c->g:39,i==screen_tab?c->b:59);
        text(x+20,159,tabs[i],5,i==screen_tab?10:222,i==screen_tab?15:226,i==screen_tab?25:237,19);
    }
    text(54,242,c->name,7,c->r,c->g,c->b,28);
    text(55,311,"CONSOLE L1 / R1",3,155,167,195,25);
    for(int i=0;i<NC;i++){
        int x=55+i*180;
        if(i==sys){rect(x,360,167,45,c->r,c->g,c->b);}
        else rect(x,360,167,45,28,35,58);
        text(x+12,373,consoles[i].id,3,i==sys?6:184,i==sys?11:190,i==sys?22:208,12);
    }
    rect(52,445,900,505,16,20,34);
    outline(52,445,900,505,70,80,111);
    rect(90,483,824,410,21,24,31);
    outline(90,483,824,410,89,98,119);
    rect(122,516,758,342,7,9,20);
    if(cover){
        int w=0,h=0;SDL_QueryTexture(cover,NULL,NULL,&w,&h);
        if(w>0&&h>0){
            float scale=fminf(690.0f/w,315.0f/h);
            SDL_Rect dst={501-(int)(w*scale/2),687-(int)(h*scale/2),
                          (int)(w*scale),(int)(h*scale)};
            SDL_RenderCopy(ren,cover,NULL,&dst);
        }
    }else{
        text(310,607,"RETRO",12,c->r,c->g,c->b,8);
        text(332,715,"VERSE",12,205,213,241,8);
    }
    for(int y=521;y<850;y+=8)rect(127,y,747,1,29,34,49);
    text(95,911,"GAME COVER / CRT DISPLAY",3,140,145,166,45);
    rect(985,445,879,505,15,20,37);
    outline(985,445,879,505,55,67,98);
    if(screen_tab==3){
        text(1020,490,"DOWNLOAD MANAGER",7,246,247,250,25);
        text(1020,570,job_status,4,c->r,c->g,c->b,41);
        text(1020,840,"PRESS X TO REFRESH",4,195,202,225,30);
    } else {
        char heading[130];
        snprintf(heading,sizeof heading,"%c / %d TITLES",letter,pick_count());
        text(1022,472,heading,5,c->r,c->g,c->b,34);
        int count=pick_count();
        int start=sel>4?sel-4:0;
        if(start+8>count && count>8)start=count-8;
        for(int row=0;row<8&&start+row<count;row++){
            int idx=game_index(start+row);
            if(idx<0)break;
            int y=539+row*48;
            if(sel==start+row){
                rect(1005,y-8,838,44,48,53,85);
                rect(1005,y-8,6,44,c->r,c->g,c->b);
            }
            text(1022,y,games[idx].title,4,
                 sel==start+row?250:174,
                 sel==start+row?252:184,
                 sel==start+row?255:213,32);
        }
        if(!count)text(1020,625,"NO ENTRIES FOR THIS LETTER",4,199,194,207,33);
    }
    if(detail){
        Game *g=selected();
        rect(85,440,1740,523,16,21,41);
        outline(85,440,1740,523,c->r,c->g,c->b);
        text(137,479,"GAME DETAILS",7,c->r,c->g,c->b,22);
        if(g)text(137,571,g->title,5,248,248,253,58);
        text(137,647,source_path[0]?"CURATED FILE FOUND":"NO DIRECT SOURCE FOUND",4,
             source_path[0]?140:243,source_path[0]?231:159,source_path[0]?157:152,40);
        text(137,716,source_path[0]?"X = DOWNLOAD TO PS5 STORAGE":"O = BACK TO BROWSER",4,221,226,243,49);
        text(137,800,"ONLY DOWNLOAD FILES YOU ARE AUTHORIZED TO USE",3,153,168,201,60);
    }
    rect(0,1000,W,80,10,14,28);
    text(50,1026,"D-PAD = BROWSE  LEFT/RIGHT = A-Z  X = SELECT  O = BACK  TRIANGLE = TABS  OPTIONS = EXIT",3,180,198,228,110);
    text(54,970,status_line,3,190,197,215,100);
    SDL_RenderPresent(ren);
}
static void action(int type){
    if(type==9){detail=0;screen_tab=(screen_tab+1)%4;load_games();return;}
    if(type==10){if(detail)detail=0;else SDL_PushEvent(&(SDL_Event){.type=SDL_QUIT});return;}
    if(type==4){sys=(sys+NC-1)%NC;load_games();return;}
    if(type==5){sys=(sys+1)%NC;load_games();return;}
    if(type==2){if(detail)return;int n=pick_count();if(n){sel=(sel+n-1)%n;prepare_art();}return;}
    if(type==3){if(detail)return;int n=pick_count();if(n){sel=(sel+1)%n;prepare_art();}return;}
    if(type==0||type==1){
        if(detail)return;
        if(screen_tab==3)return;
        letter=(letter=='#'?(type==1?'A':'Z'):(type==1?(letter=='Z'?'#':letter+1):(letter=='A'?'#':letter-1)));
        sel=0;if(screen_tab==1)load_games();else prepare_art();
        return;
    }
    if(type==6){
        if(screen_tab==3){load_games();return;}
        if(!detail){if(selected()){detail=1;check_source();}return;}
        download_selected();
    }
}
static void key(SDL_Keycode key){
    switch(key){
    case SDLK_LEFT:action(0);break;case SDLK_RIGHT:action(1);break;
    case SDLK_UP:action(2);break;case SDLK_DOWN:action(3);break;
    case SDLK_q:action(4);break;case SDLK_e:action(5);break;
    case SDLK_RETURN:case SDLK_SPACE:action(6);break;
    case SDLK_ESCAPE:action(10);break;case SDLK_TAB:action(9);break;
    default:break;
    }
}
int SDL_main(int argc,char **argv){
    (void)argc;(void)argv;
    games=calloc(MAX_GAMES,sizeof *games);
    if(!games)return 1;
    pthread_t web;pthread_create(&web,NULL,backend,NULL);pthread_detach(web);
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_JOYSTICK)!=0)return 2;
    IMG_Init(IMG_INIT_PNG);
    window=SDL_CreateWindow("RetroVerse PS5 Native",0,0,W,H,SDL_WINDOW_FULLSCREEN);
    if(!window)return 3;
    ren=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!ren)ren=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!ren)return 4;
    SDL_RenderSetLogicalSize(ren,W,H);
    for(int i=0;i<SDL_NumJoysticks();i++)if(SDL_IsGameController(i)){pad=SDL_GameControllerOpen(i);break;}
    for(int i=0;i<30;i++){
        char *resp=local_request("GET","/api/health",NULL);
        if(resp){free(resp);break;}SDL_Delay(80);
    }
    load_games();prepare_art();
    int running=1;uint32_t last=0;
    while(running) {
        SDL_Event event;
        while(SDL_PollEvent(&event)){
            if(event.type==SDL_QUIT){running=0;break;}
            if(event.type==SDL_KEYDOWN)key(event.key.keysym.sym);
            if(event.type==SDL_CONTROLLERDEVICEADDED&&!pad)
                pad=SDL_GameControllerOpen(event.cdevice.which);
            if(event.type==SDL_CONTROLLERBUTTONDOWN){
                switch(event.cbutton.button){
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:action(0);break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:action(1);break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:action(2);break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:action(3);break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:action(4);break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:action(5);break;
                case SDL_CONTROLLER_BUTTON_A:action(6);break;
                case SDL_CONTROLLER_BUTTON_Y:action(9);break;
                case SDL_CONTROLLER_BUTTON_B:action(10);break;
                case SDL_CONTROLLER_BUTTON_START:running=0;break;
                default:break;
                }
            }
        }
        update_art();
        if(screen_tab==3&&SDL_GetTicks()-last>1500){load_games();last=SDL_GetTicks();}
        render();
        SDL_Delay(16);
    }
    if(cover)SDL_DestroyTexture(cover);
    if(pad)SDL_GameControllerClose(pad);
    SDL_DestroyRenderer(ren);SDL_DestroyWindow(window);
    IMG_Quit();SDL_Quit();free(games);
    return 0;
}
