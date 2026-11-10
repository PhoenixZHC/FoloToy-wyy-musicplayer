#include "qqmusic_qr_png.h"
#include <stdlib.h>
#include <string.h>

static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static uint32_t crc32(const uint8_t *data,size_t size)
{
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<size;i++) {
        crc^=data[i];
        for(unsigned j=0;j<8;j++)crc=(crc>>1)^((0u-(crc&1))&UINT32_C(0xedb88320));
    }
    return ~crc;
}
static uint8_t paeth(uint8_t a,uint8_t b,uint8_t c)
{
    int p=(int)a+b-c,pa=abs(p-a),pb=abs(p-b),pc=abs(p-c);
    return pa<=pb && pa<=pc?a:pb<=pc?b:c;
}
bool qqmusic_qr_png_decode(const uint8_t *png,size_t size,qqmusic_qr_inflate_fn inflate,
                          uint8_t *pixels,size_t capacity,unsigned *width,unsigned *height)
{
    if(width)*width=0;
    if(height)*height=0;
    if(!png || !inflate || !pixels || !width || !height || size<57 || size>4096 || memcmp(png,"\x89PNG\r\n\x1a\n",8))return false;
    uint8_t *compressed=malloc(4096),*raw=NULL;
    if(!compressed)return false;
    bool valid=false,header=false,palette=false,end=false,started_data=false,ended_data=false;
    size_t pos=8,used=0,row_bytes=0,raw_size=0;unsigned w=0,h=0;uint8_t gray[2]={0};
    while(pos<size) {
        if(size-pos<12)goto done;
        uint32_t n=u32(png+pos);const uint8_t *type=png+pos+4,*data=png+pos+8;
        if(n>size-pos-12 || crc32(type,(size_t)n+4)!=u32(data+n))goto done;
        if(!header && memcmp(type,"IHDR",4))goto done;
        if(!memcmp(type,"IHDR",4)) {
            if(header || n!=13)goto done;
            w=u32(data);h=u32(data+4);
            if(!w || w>QQMUSIC_QR_SIDE_MAX || !h || h>QQMUSIC_QR_SIDE_MAX ||
               data[8]!=1 || data[9]!=3 || data[10] || data[11] || data[12] || capacity<(size_t)w*h)goto done;
            row_bytes=(w+7)/8;raw_size=(row_bytes+1)*h;header=true;
        } else if(!memcmp(type,"PLTE",4)) {
            if(palette || started_data || n!=6)goto done;
            for(unsigned i=0;i<2;i++) {
                if(data[i*3]!=data[i*3+1] || data[i*3]!=data[i*3+2] || (data[i*3]!=0 && data[i*3]!=255))goto done;
                gray[i]=data[i*3];
            }
            if(gray[0]==gray[1])goto done;
            palette=true;
        } else if(!memcmp(type,"IDAT",4)) {
            if(!palette || ended_data || n>4096-used)goto done;
            started_data=true;memcpy(compressed+used,data,n);used+=n;
        } else if(!memcmp(type,"IEND",4)) {
            if(n || !started_data || pos+12!=size)goto done;
            end=true;break;
        } else {
            if(!(type[0]&32) || !memcmp(type,"tRNS",4))goto done;
            if(started_data)ended_data=true;
        }
        pos+=(size_t)n+12;
    }
    if(!end || !used)goto done;
    raw=malloc(raw_size);if(!raw || !inflate(compressed,used,raw,raw_size))goto done;
    for(unsigned y=0;y<h;y++) {
        uint8_t *row=raw+(row_bytes+1)*y+1;unsigned filter=row[-1];
        uint8_t *previous=y?row-(row_bytes+1):NULL;
        if(filter>4)goto done;
        for(size_t x=0;x<row_bytes;x++) {
            uint8_t a=x?row[x-1]:0,b=previous?previous[x]:0,c=previous && x?previous[x-1]:0;
            if(filter==1)row[x]=(uint8_t)(row[x]+a);
            else if(filter==2)row[x]=(uint8_t)(row[x]+b);
            else if(filter==3)row[x]=(uint8_t)(row[x]+((unsigned)a+b)/2);
            else if(filter==4)row[x]=(uint8_t)(row[x]+paeth(a,b,c));
        }
        for(unsigned x=0;x<w;x++)pixels[(size_t)y*w+x]=gray[(row[x/8]>>(7-x%8))&1];
    }
    *width=w;*height=h;valid=true;
done:
    free(raw);free(compressed);return valid;
}
