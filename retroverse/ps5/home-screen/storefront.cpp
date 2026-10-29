/* RetroVerse native PS5 foreground storefront v0.8
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Uses the foreground VideoOut backend that already displayed on PS5.
 * Game titles and indexed file listings are bundled as plain-text assets.
 * Downloads are delegated to the proven, TLS-verifying RetroVerse helper.
 * Art PNGs are decoded by stb_image, which is public-domain/MIT.
 */
#include "demo_renderer.hpp"
// The PS5 native runtime does not export libc's __assert helper.
#define STBI_ASSERT(condition) ((void)0)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

extern "C" {
int scePadInit(void);
int scePadOpen(int userId,int type,int index,const void*param);
int scePadReadState(int handle,void*data);
int sceUserServiceInitialize(const void*);
int sceUserServiceGetInitialUser(int*);
int sceKernelSendNotificationRequest(int,void*,size_t,int);
}
namespace {
using ps5::demo::Canvas;
using ps5::demo::Color;
constexpr Color bg=static_cast<Color>(UINT32_C(0xff1a0b0b));
constexpr Color panel=static_cast<Color>(UINT32_C(0xff32211d));
constexpr Color pane2=static_cast<Color>(UINT32_C(0xff473332));
constexpr Color white=static_cast<Color>(UINT32_C(0xfff9f2f2));
constexpr Color muted=static_cast<Color>(UINT32_C(0xffc7b5af));
constexpr Color purple=static_cast<Color>(UINT32_C(0xfff1a6bd));
constexpr Color teal=static_cast<Color>(UINT32_C(0xffdbe890));
constexpr Color gold=static_cast<Color>(UINT32_C(0xff79dcff));
struct Console {const char *id,*name;Color accent;const char *prefix;};
const Console consoles[]={
 {"SNES","SUPER NINTENDO",purple,"Nintendo - SNES.zip/"},
 {"NES","NINTENDO",gold,"Nintendo - NES.zip/"},
 {"GENESIS","SEGA GENESIS",teal,"Sega - Genesis.zip/"},
 {"GB","GAME BOY",teal,"Nintendo - Game Boy.zip/"},
 {"GBA","GAME BOY ADVANCE",purple,"Nintendo - Game Boy Advance.zip/"},
 {"N64","NINTENDO 64",gold,"Nintendo - N64.zip/"},
 {"GC","GAMECUBE",purple,"Game cube/"},
 {"PS1","PLAYSTATION",teal,"Sony - PS1/"},
 {"PS2","PLAYSTATION 2",gold,nullptr},
 {"DC","DREAMCAST",purple,"Sega - Dreamcast.zip/"},
 {"ARCADE","ARCADE",gold,"Arcade - Mame 2003 Plus.zip/"}
};
constexpr int systems=int(sizeof(consoles)/sizeof(consoles[0]));
constexpr int maxgames=8000;
struct Game {char title[190];char original[250];char source[520];};
static Game games[maxgames];
static int ngames=0,system_index=0,tab=0,letter='A',highlight=0;
static bool dialog=false,helper=false,already_autostarted=false,loading_art=false,cover_attempted=false;
static int pad=-1;
static char status[600]="RETROVERSE NATIVE STORE LOADING";
static char transfer[520]="NO TRANSFER ACTIVE";
static char selected_source[700]="";
static unsigned last_buttons=0;
static int artw=0,arth=0;
static uint32_t*artpixels=nullptr;
static char artkey[512]="";

constexpr uint32_t BTN_OPTIONS=0x00000008, BTN_UP=0x10,BTN_RIGHT=0x20,BTN_DOWN=0x40,BTN_LEFT=0x80,
  BTN_L1=0x400,BTN_R1=0x800,BTN_TRI=0x1000,BTN_O=0x2000,BTN_X=0x4000,BTN_SQUARE=0x8000;
struct PadState {
 uint32_t buttons;
 uint8_t analog[8];
 float orientation[4];
 float acceleration[3];
 float angularVelocity[3];
 uint8_t touch[24];
 uint8_t connected;
 uint8_t pad[3];
 uint64_t time;
 uint8_t reserved[176];
};
static void notify(const char*m){
 struct Notification{unsigned char unused[45];char message[3075];};
 Notification n={};std::snprintf(n.message,sizeof n.message,"%s",m);
 sceKernelSendNotificationRequest(0,&n,sizeof n,0);
}
static void upper(char*s) {
 for(;*s;s++)*s=char(std::toupper((unsigned char)*s));
}
static int match_letter(const Game&g){
 int c=std::toupper((unsigned char)g.title[0]);
 return letter=='#'?!std::isalpha(c):c==letter;
}
static int filtered_count(){
 int count=0;for(int i=0;i<ngames;i++)if(match_letter(games[i]))count++;
 return count;
}
static Game*selected(){
 int n=highlight;
 for(int i=0;i<ngames;i++)if(match_letter(games[i]))if(n--==0)return &games[i];
 return nullptr;
}
static void readable(const char*raw,char*out,size_t cap){
 std::snprintf(out,cap,"%s",raw);
 char*ext=std::strrchr(out,'.');
 if(ext&&std::strlen(ext)<=6)*ext=0;
 char*region=std::strstr(out," (");if(region)*region=0;
 region=std::strstr(out," [");if(region)*region=0;
}
static void enc(const char*s,char*out,size_t cap){
 const char*h="0123456789ABCDEF";size_t j=0;
 for(size_t i=0;s[i]&&j+4<cap;i++){
  unsigned char c=(unsigned char)s[i];
  if(std::isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')out[j++]=char(c);
  else{out[j++]='%';out[j++]=h[c>>4];out[j++]=h[c&15];}
 }out[j]=0;
}
static int read_file(const char*path,char*buffer,size_t cap){
 int fd=::open(path,O_RDONLY);
 if(fd<0)return -1;
 size_t len=0;
 while(len+1<cap){
  ssize_t n=::read(fd,buffer+len,cap-1-len);
  if(n<=0)break;
  len+=size_t(n);
 }
 ::close(fd);buffer[len]=0;return int(len);
}
static void load_games(){
 ngames=0;highlight=0;dialog=false;selected_source[0]=0;cover_attempted=false;
 if(artpixels){std::free(artpixels);artpixels=nullptr;}artkey[0]=0;
 char path[250];
 if(tab==3){
  std::snprintf(status,sizeof status,"DOWNLOADS / PRESS SQUARE TO REFRESH");
  return;
 }
 if(tab==2){
  if(!helper){std::snprintf(status,sizeof status,"LIBRARY NEEDS DOWNLOADER HELPER");return;}
  // Library is fetched from helper rather than a potentially sandboxed /data path.
 }else{
  std::snprintf(path,sizeof path,"/app0/assets/catalog/%s%s.txt",
     tab==1?"HOST_":"",consoles[system_index].id);
  constexpr size_t cap=1100000;
  char*all=(char*)std::malloc(cap);
  if(!all){std::snprintf(status,sizeof status,"OUT OF MEMORY");return;}
  int n=read_file(path,all,cap);
  if(n<0){std::snprintf(status,sizeof status,"CATALOG MISSING FOR %s",consoles[system_index].id);std::free(all);return;}
  char*line=all;
  while(*line&&ngames<maxgames){
   char*next=std::strchr(line,'\n');if(next)*next=0;
   size_t len=std::strlen(line);
   if(len&&line[len-1]=='\r')line[--len]=0;
   if(len>1&&len<248){
    Game&g=games[ngames];
    std::snprintf(g.original,sizeof g.original,"%s",line);
    readable(line,g.title,sizeof g.title);
    g.source[0]=0;
    if(tab==1 && consoles[system_index].prefix)
      std::snprintf(g.source,sizeof g.source,"%s%s",consoles[system_index].prefix,line);
    ngames++;
   }
   if(!next)break;line=next+1;
  }
  std::free(all);
  std::snprintf(status,sizeof status,"%s / %d INDEXED TITLES / %s",
       consoles[system_index].name,ngames,tab==1?"HOST FILES":"GAME CATALOG");
  return;
 }
}
// Helper process HTTP: only loopback and a strict 4MB response cap.
struct Response {unsigned char*data;size_t size;int http;};
static Response http(const char*method,const char*path,const char*payload,size_t limit=4*1024*1024){
 Response out={nullptr,0,0};
 if(limit>8*1024*1024)limit=8*1024*1024;
 int fd=::socket(AF_INET,SOCK_STREAM,0);if(fd<0)return out;
 timeval timeout={8,0};
 setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
 setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
 sockaddr_in addr={};addr.sin_family=AF_INET;addr.sin_port=htons(8765);
 inet_pton(AF_INET,"127.0.0.1",&addr.sin_addr);
 if(::connect(fd,(sockaddr*)&addr,sizeof addr)<0){::close(fd);return out;}
 size_t len=payload?std::strlen(payload):0;
 char req[1700];
 int n=std::snprintf(req,sizeof req,
     "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nContent-Length: %zu\r\nContent-Type: application/json\r\n\r\n",
     method,path,len);
 if(n<=0||n>=(int)sizeof req){::close(fd);return out;}
 auto send_all=[&](const char*src,size_t sz)->bool{
   while(sz){ssize_t k=::send(fd,src,sz,0);if(k<=0)return false;src+=k;sz-=size_t(k);}return true;
 };
 if(!send_all(req,size_t(n))||(len&&!send_all(payload,len))){::close(fd);return out;}
 size_t cap=limit+5000,used=0;
 unsigned char*buffer=(unsigned char*)std::malloc(cap+1);
 if(!buffer){::close(fd);return out;}
 for(;;){
  if(used==cap)break;
  ssize_t got=::recv(fd,buffer+used,cap-used,0);
  if(got<=0)break;used+=size_t(got);
 }
 ::close(fd);buffer[used]=0;
 char*start=std::strstr((char*)buffer,"\r\n\r\n");
 if(!start){std::free(buffer);return out;}
 out.http=std::atoi((char*)buffer+9);
 size_t head=size_t(start-(char*)buffer)+4;
 out.size=used-head;
 if(out.size>limit){std::free(buffer);return Response{nullptr,0,413};}
 std::memmove(buffer,buffer+head,out.size);
 buffer[out.size]=0;
 out.data=buffer;return out;
}
static void free_resp(Response&r){std::free(r.data);r.data=nullptr;}
static bool helper_status(){
 Response r=http("GET","/api/health",nullptr,2048);
 bool ok=r.http==200&&r.data&&std::strstr((char*)r.data,"\"ok\":true");
 free_resp(r);return ok;
}
static bool launch_helper(){
 // The signed app only attempts to send its own packaged payload to the
 // existing authorized ELF loader at loopback:9021. It never fetches executable
 // code from a remote server or alters any other title's files.
 int file=::open("/app0/assets/retroverse-helper.elf",O_RDONLY);
 if(file<0)return false;
 int sock=::socket(AF_INET,SOCK_STREAM,0);
 if(sock<0){::close(file);return false;}
 timeval tv={2,0};
 setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof tv);
 sockaddr_in addr={};addr.sin_family=AF_INET;addr.sin_port=htons(9021);
 inet_pton(AF_INET,"127.0.0.1",&addr.sin_addr);
 if(::connect(sock,(sockaddr*)&addr,sizeof addr)<0){
   ::close(sock);::close(file);return false;
 }
 unsigned char buf[32768];bool ok=true;
 for(;;){
  ssize_t n=::read(file,buf,sizeof buf);
  if(n<0){ok=false;break;}if(n==0)break;
  size_t off=0;
  while(off<size_t(n)){
   ssize_t w=::send(sock,buf+off,size_t(n)-off,0);
   if(w<=0){ok=false;break;}off+=size_t(w);
  }
  if(!ok)break;
 }
 ::shutdown(sock,SHUT_WR);::close(sock);::close(file);
 return ok;
}
static void refresh_helper(){
 helper=helper_status();
 if(helper)std::snprintf(status,sizeof status,"RETROVERSE DOWNLOADER CONNECTED");
 else std::snprintf(status,sizeof status,"HELPER OFFLINE / START PAYLOAD ON PORT 9021 / OPTIONS RETRIES");
}
static void load_library(){
 ngames=0;highlight=0;dialog=false;
 if(!helper){std::snprintf(status,sizeof status,"LIBRARY HELPER IS OFFLINE");return;}
 Response r=http("GET","/api/library",nullptr,320000);
 if(r.http!=200||!r.data){std::snprintf(status,sizeof status,"CANNOT READ PS5 LIBRARY");free_resp(r);return;}
 char*cursor=(char*)r.data;
 while(ngames<maxgames && (cursor=std::strstr(cursor,"\"platform\""))){
  char platform[30]="",name[256]="";
  auto get_field=[](const char*obj,const char*key,char*out,size_t n)->bool{
   char quoted[80];std::snprintf(quoted,sizeof quoted,"\"%s\"",key);
   const char*p=std::strstr(obj,quoted);if(!p)return false;
   p=std::strchr(p,':');if(!p)return false;p++;
   while(*p==' ')p++;if(*p++!='"')return false;
   size_t j=0;while(*p&&*p!='"'&&j+1<n){if(*p=='\\'&&p[1])p++;out[j++]=*p++;}
   out[j]=0;return true;
  };
  const char*end=std::strchr(cursor,'}');
  if(!end)break;
  char obj[650]={0};size_t n=size_t(end-cursor);
  if(n<sizeof obj){
   std::memcpy(obj,cursor,n);
   if(get_field(obj,"platform",platform,sizeof platform)&&
      get_field(obj,"filename",name,sizeof name)&&
      !std::strcmp(platform,consoles[system_index].id)){
    Game&g=games[ngames++];
    std::snprintf(g.original,sizeof g.original,"%s",name);
    readable(name,g.title,sizeof g.title);
    g.source[0]=0;
   }
  }
  cursor=(char*)end+1;
 }
 free_resp(r);
 std::snprintf(status,sizeof status,"%d GAME FILES ON PS5 / %s",ngames,consoles[system_index].name);
}
static void refresh_jobs(){
 Response r=http("GET","/api/jobs",nullptr,3500);
 if(r.http==200&&r.data){
  const char*data=(char*)r.data;
  const char*pos=std::strstr(data,"\"status\":\"");
  if(pos) {
   pos+=10;char state[50]={0};size_t i=0;
   while(pos[i]&&pos[i]!='"'&&i<sizeof state-1){state[i]=pos[i];i++;}
   std::snprintf(transfer,sizeof transfer,"TRANSFER STATUS / %s",state);
   const char*bytes=std::strstr(data,"\"received\":");
   const char*total=std::strstr(data,"\"total\":");
   if(bytes&&total){
    unsigned long long have=std::strtoull(bytes+11,nullptr,10);
    unsigned long long max=std::strtoull(total+8,nullptr,10);
    std::snprintf(transfer,sizeof transfer,"TRANSFER %s / %llu OF %llu BYTES",
                  state,have,max);
   }
  }else std::snprintf(transfer,sizeof transfer,"NO ACTIVE TRANSFER");
 }else std::snprintf(transfer,sizeof transfer,"DOWNLOADER OFFLINE");
 free_resp(r);
}
static void load_source(){
 selected_source[0]=0;
 Game*g=selected();if(!g)return;
 if(tab==1){
  std::snprintf(selected_source,sizeof selected_source,"%s",g->source);
  std::snprintf(status,sizeof status,selected_source[0]?"HOST FILE LISTED / X TO REQUEST DOWNLOAD":"NO CURATED FILE FOR THIS SYSTEM");
  return;
 }
 if(tab!=0)return;
 if(!helper){std::snprintf(status,sizeof status,"DOWNLOAD HELPER OFFLINE");return;}
 char encoded[500],url[1000];
 enc(g->title,encoded,sizeof encoded);
 std::snprintf(url,sizeof url,"/api/curated/matches?platform=%s&title=%s",
               consoles[system_index].id,encoded);
 Response r=http("GET",url,nullptr,18000);
 if(r.http==200&&r.data){
  const char*p=std::strstr((char*)r.data,"\"filename\":\"");
  if(p){
   p+=12;size_t j=0;while(p[j]&&p[j]!='"'&&j+1<sizeof selected_source){selected_source[j]=p[j];j++;}
   selected_source[j]=0;
  }
 }
 free_resp(r);
 std::snprintf(status,sizeof status,
   selected_source[0]?"SOURCE FOUND / PRESS X TO CONFIRM RIGHTS AND DOWNLOAD":"NO CURATED FILE MATCHES THIS GAME");
}
static void start_download(){
 Game*g=selected();if(!g||!selected_source[0])return;
 if(!helper){std::snprintf(status,sizeof status,"CAN'T DOWNLOAD / HELPER OFFLINE");return;}
 char safeTitle[300],safePath[900],body[1600];
 auto quote=[](const char*src,char*dst,size_t cap) {
  size_t j=0;for(size_t i=0;src[i]&&j+3<cap;i++){
   if(src[i]=='"'||src[i]=='\\')dst[j++]='\\';
   if((unsigned char)src[i]<32)continue;
   dst[j++]=src[i];
  }dst[j]=0;
 };
 quote(g->title,safeTitle,sizeof safeTitle);
 quote(selected_source,safePath,sizeof safePath);
 std::snprintf(body,sizeof body,
    "{\"platform\":\"%s\",\"filename\":\"%s\",\"title\":\"%s\",\"rights_confirmed\":true}",
    consoles[system_index].id,safePath,safeTitle);
 Response r=http("POST","/api/curated/download",body,4500);
 if(r.http==200&&r.data&&std::strstr((char*)r.data,"\"ok\":true")){
  std::snprintf(status,sizeof status,"DOWNLOAD STARTED / TRIANGLE TO DOWNLOADS TAB");
  tab=3;
  refresh_jobs();
 }else{
  std::snprintf(status,sizeof status,"DOWNLOAD FAILED / CHECK HELPER AND FILE AVAILABILITY");
 }
 free_resp(r);dialog=false;
}
static void cover_clear(){
 std::free(artpixels);artpixels=nullptr;artw=0;arth=0;
}
static void get_cover(){
 Game*g=selected();if(!g||!helper||tab>1)return;
 char key[510];std::snprintf(key,sizeof key,"%s:%s",consoles[system_index].id,g->original);
 if(!std::strcmp(key,artkey))return;
 std::snprintf(artkey,sizeof artkey,"%s",key);
 cover_clear();
 char name[800],url[1300];
 enc(g->original,name,sizeof name);
 std::snprintf(url,sizeof url,"/api/cover?platform=%s&filename=%s",consoles[system_index].id,name);
 Response r=http("GET",url,nullptr,4*1024*1024);
 if(r.http!=200||!r.data){free_resp(r);return;}
 int w=0,h=0,ch=0;
 stbi_uc*raw=stbi_load_from_memory(r.data,int(r.size),&w,&h,&ch,4);
 free_resp(r);
 if(!raw||w<=0||h<=0||w>2400||h>2400){if(raw)stbi_image_free(raw);return;}
 uint32_t*out=(uint32_t*)std::malloc(size_t(w)*size_t(h)*4);
 if(!out){stbi_image_free(raw);return;}
 for(size_t i=0;i<size_t(w)*size_t(h);i++){
  const unsigned char*p=raw+4*i;
  out[i]=(uint32_t(p[3])<<24)|(uint32_t(p[2])<<16)|(uint32_t(p[1])<<8)|p[0];
 }
 stbi_image_free(raw);artpixels=out;artw=w;arth=h;
}
static void input_action(unsigned pressed){
 if(pressed&BTN_OPTIONS){
  if(!helper){refresh_helper();if(helper)std::snprintf(status,sizeof status,"DOWNLOADER CONNECTED");}
  else if(tab==3)refresh_jobs();
 }
 if(pressed&BTN_TRI){
  dialog=false;tab=(tab+1)%4;load_games();
  if(tab==2)load_library();
  if(tab==3)refresh_jobs();
  return;
 }
 if(pressed&BTN_O){dialog=false;return;}
 if(pressed&BTN_L1){system_index=(system_index+systems-1)%systems;load_games();if(tab==2)load_library();return;}
 if(pressed&BTN_R1){system_index=(system_index+1)%systems;load_games();if(tab==2)load_library();return;}
 if(dialog){
  if(pressed&BTN_X){
   if(selected_source[0])start_download();
   else{dialog=false;std::snprintf(status,sizeof status,"NO FILE AVAILABLE FOR THIS SELECTION");}
  }
  return;
 }
 if(pressed&BTN_SQUARE){
  if(tab==3)refresh_jobs();
  else {load_games();if(tab==2)load_library();}
  return;
 }
 if(pressed&BTN_LEFT){letter=(letter=='A'?'#':letter=='#'?'Z':letter-1);highlight=0;artkey[0]=0;cover_clear();}
 if(pressed&BTN_RIGHT){letter=(letter=='Z'?'#':letter=='#'?'A':letter+1);highlight=0;artkey[0]=0;cover_clear();}
 int total=filtered_count();
 if((pressed&BTN_DOWN)&&total){highlight=(highlight+1)%total;artkey[0]=0;cover_clear();}
 if((pressed&BTN_UP)&&total){highlight=(highlight+total-1)%total;artkey[0]=0;cover_clear();}
 if((pressed&BTN_X)&&selected()&&tab!=3){
  dialog=true;
  if(tab==0||tab==1)load_source();
 }
}
static void draw(Canvas&c) noexcept {
 const Console&sys=consoles[system_index];
 c.clear(bg);
 c.rectangle(0,0,1920,136,panel);
 c.rectangle(0,132,1920,6,sys.accent);
 c.text(70,30,"RETROVERSE",11,white);
 c.text(1180,43,"PS5 NATIVE STORE",6,sys.accent);
 const char*tabs[]={"STORE","HOSTS","MY LIBRARY","DOWNLOADS"};
 for(int i=0;i<4;i++){
  unsigned x=68+306*unsigned(i);
  c.rectangle(x,171,288,65,i==tab?sys.accent:pane2);
  c.text(x+26,186,tabs[i],5,i==tab?bg:white);
 }
 c.text(75,284,sys.name,7,sys.accent);
 c.text(80,367,"L1 / R1 CHANGE CONSOLE",4,muted);
 for(int i=0;i<systems;i++){
  unsigned x=80+158*unsigned(i);
  c.rectangle(x,426,148,51,i==system_index?sys.accent:pane2);
  c.text(x+13,440,consoles[i].id,4,i==system_index?bg:white);
 }
 c.rectangle(72,503,875,409,panel);
 c.rectangle(97,527,822,361,bg);
 if(artpixels){
  unsigned iw=unsigned(artw),ih=unsigned(arth);
  unsigned maxw=500,maxh=315;
  unsigned width=iw*maxh/ih,height=maxh;
  if(width>maxw){width=maxw;height=ih*maxw/iw;}
  c.image(509-width/2,546+(315-height)/2,width,height,artpixels,iw,ih);
 }else{
  c.rectangle(193,575,640,263,pane2);
  c.rectangle(243,600,540,213,bg);
  c.text(338,663,"GAME COVER",7,sys.accent);
  c.text(335,752,"ARTWORK PREVIEW",4,muted);
 }
 c.rectangle(972,503,873,409,panel);
 char heading[110];
 std::snprintf(heading,sizeof heading,"%c  /  %d TITLES",letter,filtered_count());
 c.text(1005,535,heading,5,sys.accent);
 if(tab==3){
  c.text(1003,640,"DOWNLOAD MANAGER",6,white);
  c.text(1003,722,transfer,3,muted);
 }else{
  int count=filtered_count();
  int start=(highlight>4)?highlight-4:0;
  if(start+7>count&&count>7)start=count-7;
  int at=0,row=0;
  for(int i=0;i<ngames&&row<7;i++){
   if(!match_letter(games[i]))continue;
   if(at++<start)continue;
   unsigned y=599+45*unsigned(row);
   if(start+row==highlight){
    c.rectangle(992,y-7,835,43,pane2);
    c.rectangle(992,y-7,7,43,sys.accent);
   }
   c.text(1018,y,games[i].title,4,start+row==highlight?white:muted);
   row++;
  }
  if(!count)c.text(1010,701,"NO GAMES UNDER THIS LETTER",4,muted);
 }
 if(dialog){
  c.rectangle(105,458,1710,523,pane2);
  c.rectangle(111,463,1698,8,sys.accent);
  Game*g=selected();
  c.text(166,520,"GAME DETAILS",7,sys.accent);
  if(g)c.text(166,608,g->title,6,white);
  c.text(166,710,selected_source[0]?"FILE LISTED IN CURATED ARCHIVE":"NO DIRECT FILE AVAILABLE",4,white);
  c.text(166,796,selected_source[0]?"X CONFIRMS YOU HAVE RIGHTS AND STARTS PS5 DOWNLOAD":"O RETURNS TO BROWSING",4,teal);
  c.text(166,857,"ONLY DOWNLOAD CONTENT YOU ARE AUTHORIZED TO USE",3,muted);
 }
 c.rectangle(0,995,1920,85,panel);
 c.text(52,1018,"D-PAD  BROWSE   L1/R1  CONSOLE   TRIANGLE  TABS   X  SELECT   O  BACK   SQUARE  REFRESH",3,white);
 c.text(56,950,status,3,sys.accent);
 c.text(1360,956,helper?"HELPER ONLINE":"HELPER OFFLINE",3,helper?teal:gold);
}
} // namespace
extern "C" bool retroverse_process_input(){
 static bool initialized=false;
 static int deferred_art=0,refresh=0;
 if(!initialized){
  initialized=true;
  unsigned char userParams[64]={0};
  sceUserServiceInitialize(userParams);
  int user=-1;
  if(sceUserServiceGetInitialUser(&user)>=0 && user>=0){
   scePadInit();pad=scePadOpen(user,0,0,nullptr);
  }
  load_games();
  helper=helper_status();
  if(!helper&&!already_autostarted){
   already_autostarted=true;
   bool sent=launch_helper();
   std::snprintf(status,sizeof status,sent?"DOWNLOADER START REQUESTED / CHECKING CONNECTION":"HELPER OFFLINE / LOAD RETROVERSE PAYLOAD IF REQUIRED");
  }
  return true;
 }
 bool changed=false;
 if(pad>=0){
  PadState state={};
  if(scePadReadState(pad,&state)>=0&&state.connected){
   unsigned newpress=(state.buttons)&~last_buttons;
   last_buttons=state.buttons;
   if(newpress){input_action(newpress);changed=true;deferred_art=1;}
  }
 }
 if(!helper&&++refresh>=50){
  refresh=0;bool live=helper_status();
  if(live){helper=true;std::snprintf(status,sizeof status,"DOWNLOADER CONNECTED / GAME ART READY");changed=true;deferred_art=1;}
 }
 if(helper&&tab<2&&!dialog&&deferred_art){
  deferred_art=0;get_cover();changed=true;
 }
 if(tab==3&&++refresh>=50){refresh=0;refresh_jobs();changed=true;}
 return changed;
}
int main(){
 ps5::demo::run(draw,"RetroVerse Store is running on PS5 TV");
}
