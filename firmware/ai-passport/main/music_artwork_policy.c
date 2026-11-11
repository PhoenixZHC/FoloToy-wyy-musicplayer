#include "music_artwork_policy.h"
#include <stdio.h>
#include <string.h>

bool music_artwork_request_url(const char *url,char *out,size_t capacity)
{
    if(!out || !capacity)return false;
    out[0]=0;
    if(!url || (strncmp(url,"https://",8) && strncmp(url,"http://",7)))return false;
    const char *host=strstr(url,"://")+3,*end=strchr(host,'/');
    if(!end || memchr(host,'@',(size_t)(end-host)) || memchr(host,':',(size_t)(end-host)) ||
       strchr(url,'#') || strchr(url,'\r') || strchr(url,'\n'))return false;
    bool netease=false,qq=false;
    const char *suffixes[]={".music.126.net",".music.163.com"};
    for(unsigned i=0;i<2;i++) {
        size_t n=strlen(suffixes[i]);
        if((size_t)(end-host)>n && !memcmp(end-n,suffixes[i],n))netease=true;
    }
    if((size_t)(end-host)==strlen("y.gtimg.cn") && !memcmp(host,"y.gtimg.cn",strlen("y.gtimg.cn"))) {
        const char *prefix="/music/photo_new/T002R90x90M000";
        if(!strncmp(end,prefix,strlen(prefix))) {
            const char *p=end+strlen(prefix),*mid=p;
            while((*p>='0' && *p<='9') || (*p>='a' && *p<='z') || (*p>='A' && *p<='Z'))++p;
            qq=p>mid && (size_t)(p-mid)<32 && !strcmp(p,".jpg");
        }
    }
    if(!netease && !qq)return false;
    int n=qq?snprintf(out,capacity,"https://%s",host):
        snprintf(out,capacity,"https://%s%sparam=60y60",host,strchr(end,'?')?"&":"?");
    if(n<0 || (size_t)n>=capacity) { out[0]=0;return false; }
    return true;
}
