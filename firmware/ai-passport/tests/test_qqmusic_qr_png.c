#include "qqmusic_qr_png.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* PNG结构与行过滤在主机测试；zlib由平台回调测试单独覆盖。 */
static uint32_t crc32(const uint8_t *data,size_t n)
{
    uint32_t c=UINT32_MAX;
    for(size_t i=0;i<n;i++) { c^=data[i];for(int j=0;j<8;j++)c=(c>>1)^((0u-(c&1))&UINT32_C(0xedb88320)); }
    return ~c;
}
static void put32(uint8_t *p,uint32_t n) { p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n; }
static size_t chunk(uint8_t *out,const char *type,const uint8_t *data,size_t n)
{
    put32(out,(uint32_t)n);memcpy(out+4,type,4);memcpy(out+8,data,n);put32(out+8+n,crc32(out+4,n+4));return n+12;
}
static bool inflate_rows(const uint8_t *in,size_t n,uint8_t *out,size_t cap)
{
    if(n!=cap)return false;
    memcpy(out,in,n);return true;
}
int main(void)
{
    uint8_t png[256],pixels[64],header[]={0,0,0,8,0,0,0,5,1,3,0,0,0};
    const uint8_t palette[]={255,255,255,0,0,0};
    /* 五行解滤后均为10101010，覆盖None/Sub/Up/Average/Paeth。 */
    uint8_t rows[]={0,0xaa,1,0xaa,2,0,3,0x55,4,0};
    memcpy(png,"\x89PNG\r\n\x1a\n",8);size_t n=8;
    n+=chunk(png+n,"IHDR",header,sizeof(header));n+=chunk(png+n,"PLTE",palette,sizeof(palette));
    n+=chunk(png+n,"IDAT",rows,sizeof(rows));n+=chunk(png+n,"IEND",rows,0);
    unsigned w,h;
    assert(qqmusic_qr_png_decode(png,n,inflate_rows,pixels,sizeof(pixels),&w,&h));
    assert(w==8 && h==5);
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)assert(pixels[y*w+x]==(x%2?255:0));
    for(size_t cap=0;cap<40;cap++)assert(!qqmusic_qr_png_decode(png,n,inflate_rows,pixels,cap,&w,&h));
    for(size_t length=0;length<n;length++)assert(!qqmusic_qr_png_decode(png,length,inflate_rows,pixels,sizeof(pixels),&w,&h));
    for(size_t i=8;i<n;i++) { png[i]^=1;assert(!qqmusic_qr_png_decode(png,n,inflate_rows,pixels,sizeof(pixels),&w,&h));png[i]^=1; }
    header[3]=181;n=8;n+=chunk(png+n,"IHDR",header,sizeof(header));n+=chunk(png+n,"PLTE",palette,sizeof(palette));n+=chunk(png+n,"IDAT",rows,sizeof(rows));n+=chunk(png+n,"IEND",rows,0);
    assert(!qqmusic_qr_png_decode(png,n,inflate_rows,pixels,sizeof(pixels),&w,&h));
    puts("qqmusic_qr_png: 4 groups passed");return 0;
}
