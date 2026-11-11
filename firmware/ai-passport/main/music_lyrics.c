#include "music_lyrics.h"
#include <ctype.h>
#include <stdbool.h>
#include <string.h>

static const char *timestamp(const char *p, uint32_t *ms)
{
    if(*p++!='[' || !isdigit((unsigned char)*p))return NULL;
    unsigned minutes=0,seconds=0,fraction=0,scale=100;
    while(isdigit((unsigned char)*p)) { if(minutes>1000)return NULL;minutes=minutes*10+(unsigned)(*p++-'0'); }
    if(*p!=':')return NULL;
    ++p;
    if(!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1]))return NULL;
    seconds=(unsigned)(p[0]-'0')*10+(unsigned)(p[1]-'0');p+=2;if(seconds>=60)return NULL;
    if(*p=='.') { ++p;if(!isdigit((unsigned char)*p))return NULL;
        while(isdigit((unsigned char)*p)) { fraction+=(unsigned)(*p++-'0')*scale;scale/=10; } }
    if(*p!=']')return NULL;
    ++p;
    *ms=minutes*60000+seconds*1000+fraction;return p;
}

music_lyrics_kind_t music_lyrics_classify(const char *lrc)
{
    if(!lrc || !lrc[0])return MUSIC_LYRICS_EMPTY;
    if(strstr(lrc,"纯音乐，请欣赏") || strstr(lrc,"没有填词的纯音乐"))return MUSIC_LYRICS_INSTRUMENTAL;
    bool content=false;
    for(const char *p=lrc;*p;) {
        const char *end=strchr(p,'\n');if(!end)end=p+strlen(p);
        const char *text=p,*next;uint32_t ms;
        bool timed=false;
        while((next=timestamp(text,&ms)) && next<=end) { timed=true;text=next; }
        while(text<end && isspace((unsigned char)*text))++text;
        if(timed && text<end)return MUSIC_LYRICS_TIMED;
        if(text<end && *text!='[')content=true;
        p=*end?end+1:end;
    }
    return content?MUSIC_LYRICS_UNTIMED:MUSIC_LYRICS_EMPTY;
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
