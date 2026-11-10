#include "qqmusic_mqtt.h"

#include <string.h>

typedef struct { uint8_t *out;size_t capacity,used;bool valid; } writer_t;
static void bytes(writer_t *w,const void *data,size_t length)
{
    if(!w->valid || length>w->capacity-w->used) { w->valid=false;return; }
    memcpy(w->out+w->used,data,length);w->used+=length;
}
static void byte(writer_t *w,uint8_t value) { bytes(w,&value,1); }
static void u16(writer_t *w,uint16_t value) { byte(w,(uint8_t)(value>>8));byte(w,(uint8_t)value); }
static void varint(writer_t *w,size_t value)
{
    do { uint8_t b=value%128;value/=128;byte(w,b|(value?128:0)); } while(value);
}
static void string(writer_t *w,const char *value)
{
    size_t length=strlen(value);
    if(length>UINT16_MAX) { w->valid=false;return; }
    u16(w,(uint16_t)length);bytes(w,value,length);
}
static void property(writer_t *w,const char *key,const char *value)
{
    byte(w,0x26);string(w,key);string(w,value);
}
static bool safe_id(const char *value,size_t maximum)
{
    if(!value || !value[0])return false;
    size_t i=0;
    for(;value[i] && i<maximum;i++) {
        unsigned char c=(unsigned char)value[i];
        if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_'))return false;
    }
    return i<=maximum && value[i]==0;
}
static qqmusic_mqtt_result_t finish(uint8_t opcode,const uint8_t *body,size_t body_length,
                                    uint8_t *out,size_t capacity,size_t *length)
{
    writer_t w={out,capacity,0,true};byte(&w,opcode);varint(&w,body_length);bytes(&w,body,body_length);
    if(!w.valid)return QQMUSIC_MQTT_TOO_LARGE;
    *length=w.used;return QQMUSIC_MQTT_OK;
}
qqmusic_mqtt_result_t qqmusic_mqtt_connect(const char *client_id,const char *qr_id,
                                          uint8_t *out,size_t capacity,size_t *length)
{
    if(length)*length=0;
    if(!out || !length || !safe_id(client_id,64) || !safe_id(qr_id,QQMUSIC_QR_ID_MAX))return QQMUSIC_MQTT_INVALID;
    uint8_t properties[768],body[1024];writer_t p={properties,sizeof(properties),0,true};
    byte(&p,0x15);string(&p,"pass");
    property(&p,"tmeAppID","qqmusic");property(&p,"business","management");
    property(&p,"hashTag",qr_id);property(&p,"clientTag","management.user");property(&p,"userID",qr_id);
    writer_t w={body,sizeof(body),0,true};string(&w,"MQTT");byte(&w,5);byte(&w,2);u16(&w,45);
    varint(&w,p.used);bytes(&w,properties,p.used);string(&w,client_id);
    if(!p.valid || !w.valid)return QQMUSIC_MQTT_TOO_LARGE;
    return finish(0x10,body,w.used,out,capacity,length);
}
qqmusic_mqtt_result_t qqmusic_mqtt_subscribe(const char *qr_id,uint16_t packet_id,
                                            uint8_t *out,size_t capacity,size_t *length)
{
    if(length)*length=0;
    if(!out || !length || !packet_id || !safe_id(qr_id,QQMUSIC_QR_ID_MAX))return QQMUSIC_MQTT_INVALID;
    uint8_t properties[128],body[512];writer_t p={properties,sizeof(properties),0,true};
    property(&p,"authorization","tmelogin");property(&p,"pubsub","unicast");
    writer_t w={body,sizeof(body),0,true};u16(&w,packet_id);varint(&w,p.used);bytes(&w,properties,p.used);
    const char prefix[]="management.qrcode_login/";
    u16(&w,(uint16_t)(sizeof(prefix)-1+strlen(qr_id)));
    bytes(&w,prefix,sizeof(prefix)-1);bytes(&w,qr_id,strlen(qr_id));byte(&w,1);
    if(!p.valid || !w.valid)return QQMUSIC_MQTT_TOO_LARGE;
    return finish(0x82,body,w.used,out,capacity,length);
}

typedef struct { const uint8_t *data;size_t length,pos; } reader_t;
static bool take(reader_t *r,size_t n,const uint8_t **out)
{
    if(n>r->length-r->pos)return false;
    if(out)*out=r->data+r->pos;
    r->pos+=n;return true;
}
static bool read_u16(reader_t *r,uint16_t *value)
{
    const uint8_t *p;if(!take(r,2,&p))return false;
    *value=(uint16_t)((uint16_t)p[0]*256+p[1]);return true;
}
static qqmusic_mqtt_result_t read_varint(reader_t *r,size_t *value)
{
    *value=0;
    for(unsigned i=0;i<4;i++) {
        const uint8_t *p;if(!take(r,1,&p))return QQMUSIC_MQTT_MORE;
        *value|=(size_t)(p[0]&127)<<(7*i);
        if(!(p[0]&128))return i && p[0]==0?QQMUSIC_MQTT_INVALID:QQMUSIC_MQTT_OK;
    }
    return QQMUSIC_MQTT_INVALID;
}
static bool read_string(reader_t *r,const uint8_t **data,size_t *length)
{
    uint16_t n;if(!read_u16(r,&n) || !take(r,n,data))return false;
    *length=n;return true;
}
static bool copy_string(char *out,size_t capacity,const uint8_t *data,size_t length)
{
    if(length>=capacity || memchr(data,0,length))return false;
    memcpy(out,data,length);out[length]=0;return true;
}
static qqmusic_mqtt_result_t properties(reader_t *r,qqmusic_mqtt_packet_t *out)
{
    size_t n;
    if(read_varint(r,&n)!=QQMUSIC_MQTT_OK || n>r->length-r->pos)return QQMUSIC_MQTT_INVALID;
    reader_t p={r->data+r->pos,n,0};r->pos+=n;
    while(p.pos<p.length) {
        const uint8_t *id,*data,*key;size_t length,key_length,ignored;
        uint16_t number;
        if(!take(&p,1,&id))return QQMUSIC_MQTT_INVALID;
        switch(*id) {
        case 0x13:
            if(!read_u16(&p,&number) || out->has_server_keepalive)return QQMUSIC_MQTT_INVALID;
            out->has_server_keepalive=true;out->server_keepalive=number;break;
        case 0x1c:
            if(!read_string(&p,&data,&length) || out->server_reference[0] ||
               !copy_string(out->server_reference,sizeof(out->server_reference),data,length))return QQMUSIC_MQTT_INVALID;
            break;
        case 0x26:
            if(!read_string(&p,&key,&key_length) || !read_string(&p,&data,&length))return QQMUSIC_MQTT_INVALID;
            if(key_length==4 && memcmp(key,"type",4)==0) {
                if(out->event[0] || !copy_string(out->event,sizeof(out->event),data,length))return QQMUSIC_MQTT_INVALID;
            }
            break;
        case 0x01:case 0x17:case 0x19:case 0x24:case 0x25:case 0x28:case 0x29:case 0x2a:
            if(!take(&p,1,NULL))return QQMUSIC_MQTT_INVALID;
            break;
        case 0x21:case 0x22:case 0x23:
            if(!take(&p,2,NULL))return QQMUSIC_MQTT_INVALID;
            break;
        case 0x02:case 0x11:case 0x18:case 0x27:
            if(!take(&p,4,NULL))return QQMUSIC_MQTT_INVALID;
            break;
        case 0x0b:
            if(read_varint(&p,&ignored)!=QQMUSIC_MQTT_OK)return QQMUSIC_MQTT_INVALID;
            break;
        case 0x03:case 0x08:case 0x09:case 0x12:case 0x15:case 0x16:case 0x1a:case 0x1f:
            if(!read_string(&p,&data,&length))return QQMUSIC_MQTT_INVALID;
            break;
        default:return QQMUSIC_MQTT_UNSUPPORTED;
        }
    }
    return QQMUSIC_MQTT_OK;
}
qqmusic_mqtt_result_t qqmusic_mqtt_parse(const uint8_t *data,size_t length,qqmusic_mqtt_packet_t *out)
{
    if(!data || !out)return QQMUSIC_MQTT_INVALID;
    memset(out,0,sizeof(*out));
    if(!length)return QQMUSIC_MQTT_MORE;
    reader_t r={data,length,1};size_t remaining;
    qqmusic_mqtt_result_t result=read_varint(&r,&remaining);
    if(result!=QQMUSIC_MQTT_OK)return result;
    if(remaining>QQMUSIC_MQTT_PACKET_MAX-r.pos)return QQMUSIC_MQTT_TOO_LARGE;
    if(remaining>length-r.pos)return QQMUSIC_MQTT_MORE;
    out->consumed=r.pos+remaining;r.length=out->consumed;out->type=data[0]>>4;
    if(out->type!=3 && (data[0]&15))return QQMUSIC_MQTT_INVALID;
    if(out->type==2) {
        const uint8_t *ack;if(!take(&r,2,&ack) || (ack[0]&0xfe) || (ack[1] && ack[0]))return QQMUSIC_MQTT_INVALID;
        out->reason=ack[1];result=properties(&r,out);
    } else if(out->type==9) {
        if(!read_u16(&r,&out->packet_id) || !out->packet_id)return QQMUSIC_MQTT_INVALID;
        result=properties(&r,out);
        const uint8_t *reason;if(result==QQMUSIC_MQTT_OK && !take(&r,1,&reason))return QQMUSIC_MQTT_INVALID;
        if(result==QQMUSIC_MQTT_OK)out->reason=*reason;
    } else if(out->type==3) {
        out->qos=(data[0]>>1)&3;
        if(out->qos>1)return QQMUSIC_MQTT_UNSUPPORTED;
        if(!out->qos && (data[0]&8))return QQMUSIC_MQTT_INVALID;
        if(!read_string(&r,&out->topic,&out->topic_length) || !out->topic_length)return QQMUSIC_MQTT_INVALID;
        if(out->qos && (!read_u16(&r,&out->packet_id) || !out->packet_id))return QQMUSIC_MQTT_INVALID;
        result=properties(&r,out);
        out->payload=data+r.pos;out->payload_length=r.length-r.pos;r.pos=r.length;
    } else if(out->type==13) {
        result=QQMUSIC_MQTT_OK;
    } else if(out->type==14) {
        result=QQMUSIC_MQTT_OK;
        if(remaining) {
            const uint8_t *reason;if(!take(&r,1,&reason))return QQMUSIC_MQTT_INVALID;
            out->reason=*reason;
            if(r.pos<r.length)result=properties(&r,out);
        }
    } else return QQMUSIC_MQTT_UNSUPPORTED;
    if(result!=QQMUSIC_MQTT_OK)return result;
    return r.pos==r.length?QQMUSIC_MQTT_OK:QQMUSIC_MQTT_INVALID;
}
