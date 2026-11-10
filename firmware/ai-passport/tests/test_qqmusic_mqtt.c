#include "qqmusic_mqtt.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void parse_packets(void)
{
    qqmusic_mqtt_packet_t p;
    const uint8_t ack[]={0x20,0x06,0x00,0x00,0x03,0x13,0,45};
    for(size_t n=0;n<sizeof(ack);n++)assert(qqmusic_mqtt_parse(ack,n,&p)==QQMUSIC_MQTT_MORE);
    assert(qqmusic_mqtt_parse(ack,sizeof(ack),&p)==QQMUSIC_MQTT_OK);
    assert(p.type==2 && p.reason==0 && p.server_keepalive==45 && p.has_server_keepalive && p.consumed==sizeof(ack));
    const uint8_t redirect[]={0x20,0x0b,0,0x9d,8,0x1c,0,5,'a',':','4','4','3'};
    assert(qqmusic_mqtt_parse(redirect,sizeof(redirect),&p)==QQMUSIC_MQTT_OK);
    assert(p.reason==0x9d && !strcmp(p.server_reference,"a:443"));
    const uint8_t suback[]={0x90,4,0,1,0,1};
    assert(qqmusic_mqtt_parse(suback,sizeof(suback),&p)==QQMUSIC_MQTT_OK && p.packet_id==1 && p.reason==1);
    const uint8_t publish[]={0x32,0x1b,0,1,'t',0,7,0x11,0x26,0,4,'t','y','p','e',0,8,'s','c','a','n','n','e','d','!', '{','}'};
    uint8_t valid[sizeof(publish)];memcpy(valid,publish,sizeof(valid));
    /* 真实协议事件类型使用字符串；此处锁定属性后载荷边界与QoS1 ID。 */
    valid[1]=(uint8_t)(sizeof(valid)-2);
    assert(qqmusic_mqtt_parse(valid,sizeof(valid),&p)==QQMUSIC_MQTT_OK);
    assert(p.type==3 && p.qos==1 && p.packet_id==7 && !strcmp(p.event,"scanned!"));
    assert(p.topic_length==1 && p.topic[0]=='t' && p.payload_length==2 && !memcmp(p.payload,"{}",2));
    const uint8_t ping[]={0xd0,0,0xd0,0};
    assert(qqmusic_mqtt_parse(ping,sizeof(ping),&p)==QQMUSIC_MQTT_OK && p.consumed==2);
}
static void invalid_packets(void)
{
    qqmusic_mqtt_packet_t p;
    const uint8_t bad_remaining[]={0x20,0x80,0x80,0x80,0x80};
    assert(qqmusic_mqtt_parse(bad_remaining,sizeof(bad_remaining),&p)==QQMUSIC_MQTT_INVALID);
    const uint8_t overlong[]={0x20,0x80,0};
    assert(qqmusic_mqtt_parse(overlong,sizeof(overlong),&p)==QQMUSIC_MQTT_INVALID);
    const uint8_t large[]={0x30,0xff,0x7f};
    assert(qqmusic_mqtt_parse(large,sizeof(large),&p)==QQMUSIC_MQTT_TOO_LARGE);
    const uint8_t flag[]={0x21,3,0,0,0};
    assert(qqmusic_mqtt_parse(flag,sizeof(flag),&p)==QQMUSIC_MQTT_INVALID);
    const uint8_t ack_property[]={0x20,3,0,0,1};
    assert(qqmusic_mqtt_parse(ack_property,sizeof(ack_property),&p)==QQMUSIC_MQTT_INVALID);
    const uint8_t qos2[]={0x34,0};
    assert(qqmusic_mqtt_parse(qos2,sizeof(qos2),&p)==QQMUSIC_MQTT_UNSUPPORTED);
    const uint8_t extra_ping[]={0xd0,1,0};
    assert(qqmusic_mqtt_parse(extra_ping,sizeof(extra_ping),&p)==QQMUSIC_MQTT_INVALID);
    const uint8_t empty_topic[]={0x30,3,0,0,0};
    assert(qqmusic_mqtt_parse(empty_topic,sizeof(empty_topic),&p)==QQMUSIC_MQTT_INVALID);
}
static bool contains(const uint8_t *bytes,size_t length,const char *text)
{
    size_t n=strlen(text);
    for(size_t i=0;i+n<=length;i++)if(!memcmp(bytes+i,text,n))return true;
    return false;
}
static void encode_packets(void)
{
    uint8_t out[2048];size_t length=99;
    assert(qqmusic_mqtt_connect("1234","abc_123",out,sizeof(out),&length)==QQMUSIC_MQTT_OK);
    assert(out[0]==0x10 && contains(out,length,"pass") && contains(out,length,"tmeAppID") && contains(out,length,"hashTag"));
    assert(contains(out,length,"management.user") && contains(out,length,"userID"));
    assert(qqmusic_mqtt_connect("1234","abc_123",out,3,&length)==QQMUSIC_MQTT_TOO_LARGE && length==0);
    assert(qqmusic_mqtt_connect("1234","abc/#",out,sizeof(out),&length)==QQMUSIC_MQTT_INVALID);
    assert(qqmusic_mqtt_subscribe("abc_123",1,out,sizeof(out),&length)==QQMUSIC_MQTT_OK);
    assert(out[0]==0x82 && contains(out,length,"management.qrcode_login/abc_123") && contains(out,length,"tmelogin"));
    assert(qqmusic_mqtt_subscribe("abc",0,out,sizeof(out),&length)==QQMUSIC_MQTT_INVALID);
}
int main(void)
{
    encode_packets();parse_packets();invalid_packets();
    puts("qqmusic_mqtt: 3 groups passed");return 0;
}
