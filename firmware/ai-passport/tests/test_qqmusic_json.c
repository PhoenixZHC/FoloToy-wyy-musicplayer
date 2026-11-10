#include "qqmusic_json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    qqmusic_json_t root,field,item;
    const char *document=" {\"id\":18446744073709551615,\"items\":[{\"mid\":\"00MID\",\"name\":\"\\u4e2d\\u6587\\ud83d\\ude00\"}],\"valid\":true} ";
    assert(qqmusic_json_parse(document,strlen(document),&root));
    assert(qqmusic_json_member(root,"id",&field));
    uint64_t id=0;assert(qqmusic_json_uint(field,&id) && id==UINT64_MAX);
    assert(qqmusic_json_member(root,"items",&field));
    assert(qqmusic_json_index(field,0,&item));
    assert(!qqmusic_json_index(field,1,&item));
    assert(qqmusic_json_member(item,"mid",&field));
    char text[32];assert(qqmusic_json_string(field,text,sizeof(text)) && !strcmp(text,"00MID"));
    assert(qqmusic_json_member(item,"name",&field));
    assert(qqmusic_json_string(field,text,sizeof(text)) && !strcmp(text,"中文😀"));
    assert(!qqmusic_json_string(field,text,4) && !text[0]);
    assert(qqmusic_json_display_string(field,text,4) && !strcmp(text,"中"));
    assert(qqmusic_json_member(root,"valid",&field));
    bool valid=false;assert(qqmusic_json_bool(field,&valid) && valid);
    const char *invalid[]={"{\"a\":1,}","[1,]","01","1.","1e","true false","{\"a\":\"\\u0000\"}",
        "\"\\ud800\"","\"\\udc00\"","\"\xc0\x80\"","\"\xed\xa0\x80\"","\"\xf4\x90\x80\x80\"","{x:1}","[null,,false]"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) assert(!qqmusic_json_parse(invalid[i],strlen(invalid[i]),&root));
    assert(qqmusic_json_parse("{\"id\":1,\"id\":2}",15,&root));
    assert(!qqmusic_json_member(root,"id",&field));
    assert(!qqmusic_json_uint((qqmusic_json_t){"18446744073709551616",20},&id));
    char nested[80];memset(nested,'[',39);memset(nested+39,']',39);
    assert(!qqmusic_json_parse(nested,78,&root));
    puts("qqmusic_json: 4 groups passed");
    return 0;
}
