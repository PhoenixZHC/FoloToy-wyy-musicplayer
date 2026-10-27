#include "music_lyrics.h"
#include <ctype.h>
#include <stdbool.h>
#include <string.h>

static const char *timestamp(const char *p, uint32_t *ms)
{
    if(*p++!='[' || !isdigit((unsigned char)*p))return NULL;
    unsigned minutes=0,seconds=0,fraction=0,scale=100;
    while(isdigit((unsigned char)*p)) { if(minutes>1000)return NULL;minutes=minutes*10+(unsigned)(*p++-'0'); }
    if(*p++!=':' || !isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1]))return NULL;
    seconds=(unsigned)(p[0]-'0')*10+(unsigned)(p[1]-'0');p+=2;if(seconds>=60)return NULL;
    if(*p=='.') { ++p;if(!isdigit((unsigned char)*p))return NULL;
        while(isdigit((unsigned char)*p)) { fraction+=(unsigned)(*p++-'0')*scale;scale/=10; } }
    if(*p++!=']')return NULL;
    *ms=minutes*60000+seconds*1000+fraction;return p;
}

void music_lyrics_line(const char *lrc, uint32_t elapsed_ms, char *out, size_t capacity)
{
    if(!out || !capacity) return;
    out[0]=0;
    if(!lrc) return;
    bool found=false;uint32_t best=0;const char *chosen=NULL;size_t length=0;
    for(const char *p=lrc;*p;) {
        const char *end=strchr(p,'\n');if(!end)end=p+strlen(p);
        const char *text=p,*next;uint32_t at=0,line_best=0;bool eligible=false;
        while((next=timestamp(text,&at))!=NULL && next<=end) {
            if(at<=elapsed_ms && (!eligible || at>=line_best)) { eligible=true;line_best=at; }
            text=next;
        }
        if(eligible && (!found || line_best>=best)) {
            found=true;best=line_best;chosen=text;length=(size_t)(end-text);
            if(length && text[length-1]=='\r')--length;
        }
        p=*end?end+1:end;
    }
    if(!chosen)return;
    if(length>=capacity) { length=capacity-1;while(length && ((unsigned char)chosen[length]&0xc0)==0x80)--length; }
    memcpy(out,chosen,length);out[length]=0;
}
