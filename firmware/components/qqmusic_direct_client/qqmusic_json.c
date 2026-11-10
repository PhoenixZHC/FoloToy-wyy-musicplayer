#include "qqmusic_json.h"
#include <string.h>

#define JSON_DEPTH_MAX 24

static bool space(char c) { return c==' ' || c=='\n' || c=='\r' || c=='\t'; }
static void spaces(const char **p, const char *end) { while(*p<end && space(**p)) ++*p; }
static int hex(char c)
{
    if(c>='0' && c<='9') return c-'0';
    if(c>='a' && c<='f') return c-'a'+10;
    if(c>='A' && c<='F') return c-'A'+10;
    return -1;
}
static bool unicode(const char **p,const char *end,uint32_t *out)
{
    if(end-*p<4) return false;
    uint32_t v=0;
    for(unsigned i=0;i<4;i++) { int digit=hex(*(*p)++); if(digit<0) return false; v=(v<<4)|(unsigned)digit; }
    if(v>=0xd800 && v<=0xdbff) {
        if(end-*p<6 || (*p)[0]!='\\' || (*p)[1]!='u') return false;
        *p+=2;
        uint32_t low=0;
        for(unsigned i=0;i<4;i++) { int digit=hex(*(*p)++); if(digit<0) return false; low=(low<<4)|(unsigned)digit; }
        if(low<0xdc00 || low>0xdfff) return false;
        v=0x10000+((v-0xd800)<<10)+(low-0xdc00);
    } else if(v>=0xdc00 && v<=0xdfff) return false;
    *out=v; return true;
}
/* 读取一个Unicode标量；同时验证原始UTF-8及转义，拒绝内嵌NUL。 */
static bool character(const char **p,const char *end,uint32_t *out)
{
    if(*p>=end) return false;
    unsigned char c=(unsigned char)*(*p)++;
    if(c=='\\') {
        if(*p>=end) return false;
        c=(unsigned char)*(*p)++;
        if(c=='u') return unicode(p,end,out) && *out!=0;
        const char *esc="\"\\/bfnrt", *decoded="\"\\/\b\f\n\r\t";
        const char *at=strchr(esc,c);
        if(!at || !c) return false;
        *out=(unsigned char)decoded[at-esc]; return true;
    }
    if(c<0x20 || c=='"') return false;
    if(c<0x80) { *out=c; return true; }
    unsigned extra; uint32_t value,min;
    if(c>=0xc2 && c<=0xdf) { extra=1;value=c&31;min=0x80; }
    else if(c>=0xe0 && c<=0xef) { extra=2;value=c&15;min=0x800; }
    else if(c>=0xf0 && c<=0xf4) { extra=3;value=c&7;min=0x10000; }
    else return false;
    if((size_t)(end-*p)<extra) return false;
    while(extra--) { c=(unsigned char)*(*p)++; if((c&0xc0)!=0x80) return false; value=(value<<6)|(c&63); }
    if(value<min || value>0x10ffff || (value>=0xd800 && value<=0xdfff)) return false;
    *out=value;return true;
}
static bool string_end(const char **p,const char *end)
{
    if(*p>=end || *(*p)++!='"') return false;
    while(*p<end && **p!='"') { uint32_t c; if(!character(p,end,&c)) return false; }
    if(*p>=end) return false;
    ++*p;return true;
}
static bool digit(char c) { return c>='0' && c<='9'; }
static bool value_end(const char **p,const char *end,unsigned depth)
{
    spaces(p,end);
    if(*p>=end || depth>JSON_DEPTH_MAX) return false;
    char c=**p;
    if(c=='"') return string_end(p,end);
    if(c=='{' || c=='[') {
        char close=c=='{'?'}':']'; ++*p; spaces(p,end);
        if(*p<end && **p==close) { ++*p;return true; }
        while(*p<end) {
            if(c=='{') {
                if(!string_end(p,end)) return false;
                spaces(p,end); if(*p>=end || *(*p)++!=':') return false;
            }
            if(!value_end(p,end,depth+1)) return false;
            spaces(p,end); if(*p>=end) return false;
            if(**p==close) { ++*p;return true; }
            if(*(*p)++!=',') return false;
            spaces(p,end);
        }
        return false;
    }
    const char *literal=c=='t'?"true":c=='f'?"false":c=='n'?"null":NULL;
    if(literal) {
        size_t n=strlen(literal);
        if((size_t)(end-*p)<n || memcmp(*p,literal,n)) return false;
        *p+=n;return true;
    }
    if(c=='-') ++*p;
    if(*p>=end || !digit(**p)) return false;
    if(**p=='0') ++*p;
    else while(*p<end && digit(**p)) ++*p;
    if(*p<end && **p=='.') {
        ++*p; if(*p>=end || !digit(**p)) return false;
        while(*p<end && digit(**p)) ++*p;
    }
    if(*p<end && (**p=='e' || **p=='E')) {
        ++*p; if(*p<end && (**p=='+' || **p=='-')) ++*p;
        if(*p>=end || !digit(**p)) return false;
        while(*p<end && digit(**p)) ++*p;
    }
    return true;
}
bool qqmusic_json_parse(const char *text,size_t length,qqmusic_json_t *out)
{
    if(!text || !out || !length) return false;
    const char *p=text,*end=text+length;spaces(&p,end);const char *start=p;
    if(!value_end(&p,end,0)) return false;
    const char *finish=p;spaces(&p,end);
    if(p!=end) return false;
    *out=(qqmusic_json_t){start,(size_t)(finish-start)};return true;
}
static bool decode_string(qqmusic_json_t value,char *out,size_t capacity,bool shorten)
{
    if(!out || !capacity) return false;
    out[0]=0;
    if(value.length<2 || !value.text || value.text[0]!='"' || value.text[value.length-1]!='"') return false;
    const char *p=value.text+1,*end=value.text+value.length-1;size_t used=0;bool full=false;
    while(p<end) {
        uint32_t cp;if(!character(&p,end,&cp)) return false;
        unsigned char encoded[4];size_t n;
        if(cp<0x80) { encoded[0]=cp;n=1; }
        else if(cp<0x800) { encoded[0]=0xc0|(cp>>6);encoded[1]=0x80|(cp&63);n=2; }
        else if(cp<0x10000) { encoded[0]=0xe0|(cp>>12);encoded[1]=0x80|((cp>>6)&63);encoded[2]=0x80|(cp&63);n=3; }
        else { encoded[0]=0xf0|(cp>>18);encoded[1]=0x80|((cp>>12)&63);encoded[2]=0x80|((cp>>6)&63);encoded[3]=0x80|(cp&63);n=4; }
        if(full || n>=capacity-used) {
            if(!shorten) { out[0]=0;return false; }
            full=true;
        } else { memcpy(out+used,encoded,n);used+=n; }
    }
    out[used]=0;return true;
}
bool qqmusic_json_string(qqmusic_json_t value,char *out,size_t capacity) { return decode_string(value,out,capacity,false); }
bool qqmusic_json_display_string(qqmusic_json_t value,char *out,size_t capacity) { return decode_string(value,out,capacity,true); }
bool qqmusic_json_uint(qqmusic_json_t value,uint64_t *out)
{
    if(!out || !value.text || !value.length) return false;
    uint64_t n=0;
    for(size_t i=0;i<value.length;i++) {
        char c=value.text[i];if(!digit(c) || (i && value.text[0]=='0')) return false;
        unsigned d=(unsigned)(c-'0');if(n>(UINT64_MAX-d)/10) return false;n=n*10+d;
    }
    *out=n;return true;
}
bool qqmusic_json_bool(qqmusic_json_t value,bool *out)
{
    if(!out || !value.text) return false;
    if(value.length==4 && !memcmp(value.text,"true",4)) { *out=true;return true; }
    if(value.length==5 && !memcmp(value.text,"false",5)) { *out=false;return true; }
    return false;
}
bool qqmusic_json_member(qqmusic_json_t object,const char *key,qqmusic_json_t *out)
{
    if(!object.text || object.length<2 || object.text[0]!='{' || !key || !out) return false;
    const char *p=object.text+1,*end=object.text+object.length;bool found=false;
    while(p<end) {
        spaces(&p,end);if(p>=end || *p=='}') break;
        const char *start=p;if(!string_end(&p,end)) return false;
        char name[64];bool match=qqmusic_json_string((qqmusic_json_t){start,(size_t)(p-start)},name,sizeof(name)) && !strcmp(name,key);
        spaces(&p,end);if(p>=end || *p++!=':') return false;
        spaces(&p,end);start=p;if(!value_end(&p,end,0)) return false;
        if(match) { if(found) return false;*out=(qqmusic_json_t){start,(size_t)(p-start)};found=true; }
        spaces(&p,end);if(p>=end || *p=='}') break;
        if(*p++!=',') return false;
    }
    return found;
}
bool qqmusic_json_index(qqmusic_json_t array,size_t index,qqmusic_json_t *out)
{
    if(!array.text || array.length<2 || array.text[0]!='[' || !out) return false;
    const char *p=array.text+1,*end=array.text+array.length;
    for(size_t i=0;p<end;i++) {
        spaces(&p,end);if(p>=end || *p==']') return false;
        const char *start=p;if(!value_end(&p,end,0)) return false;
        if(i==index) { *out=(qqmusic_json_t){start,(size_t)(p-start)};return true; }
        spaces(&p,end);if(p>=end || *p++!=',') return false;
    }
    return false;
}
