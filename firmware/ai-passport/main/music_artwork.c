#include "music_artwork.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp32c3/rom/tjpgd.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define COVER_SIZE 60
#define JPEG_LIMIT 16384
typedef struct { const uint8_t *jpeg;size_t length,position;uint16_t *pixels; } decoder_io_t;
static UINT jpeg_read(JDEC *jd,BYTE *dest,UINT count)
{
    decoder_io_t *io=jd->device;size_t available=io->length-io->position;
    if(count>available)count=(UINT)available;
    if(dest) memcpy(dest,io->jpeg+io->position,count);
    io->position+=count;
    return count;
}
static UINT jpeg_write(JDEC *jd,void *bitmap,JRECT *rect)
{
    decoder_io_t *io=jd->device;const uint8_t *rgb=bitmap;
    if(rect->right>=COVER_SIZE || rect->bottom>=COVER_SIZE)return 0;
    for(unsigned y=rect->top;y<=rect->bottom;y++)for(unsigned x=rect->left;x<=rect->right;x++) {
        io->pixels[y*COVER_SIZE+x]=(uint16_t)(((rgb[0]&0xf8)<<8)|((rgb[1]&0xfc)<<3)|(rgb[2]>>3));rgb+=3;
    }
    return 1;
}
static bool allowed_url(const char *url)
{
    const char *host=strstr(url,"://");if(!host)return false;host+=3;
    const char *end=strchr(host,'/');if(!end || memchr(host,'@',(size_t)(end-host)) || memchr(host,':',(size_t)(end-host)))return false;
    const char *suffixes[]={".music.126.net",".music.163.com"};
    for(unsigned i=0;i<2;i++) { size_t n=strlen(suffixes[i]);if((size_t)(end-host)>n && !memcmp(end-n,suffixes[i],n))return true; }
    return false;
}
esp_err_t music_artwork_load(const char *url,lv_image_dsc_t *image)
{
    if(!url || !image) return ESP_ERR_INVALID_ARG;
    memset(image,0,sizeof(*image));
    if(!allowed_url(url) || (strncmp(url,"https://",8) && strncmp(url,"http://",7)))return ESP_ERR_INVALID_ARG;
    char resized[320];const char *path=strstr(url,"://")+3;
    int written=snprintf(resized,sizeof(resized),"https://%s%sparam=60y60",path,strchr(path,'?')?"&":"?");
    if(written<0 || (size_t)written>=sizeof(resized))return ESP_ERR_INVALID_SIZE;
    uint8_t *jpeg=malloc(JPEG_LIMIT);if(!jpeg)return ESP_ERR_NO_MEM;
    esp_http_client_config_t cfg={.url=resized,.timeout_ms=5000,.buffer_size=1024,.buffer_size_tx=512,
        .crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true};
    esp_http_client_handle_t h=esp_http_client_init(&cfg);esp_err_t err=h?ESP_OK:ESP_ERR_NO_MEM;size_t used=0;
    if(err==ESP_OK)err=esp_http_client_open(h,0);
    if(err==ESP_OK) {
        int64_t size=esp_http_client_fetch_headers(h);
        if(size<0 || size>JPEG_LIMIT || esp_http_client_get_status_code(h)!=200)err=ESP_ERR_INVALID_RESPONSE;
    }
    while(err==ESP_OK && used<JPEG_LIMIT) {
        int n=esp_http_client_read(h,(char *)jpeg+used,(int)(JPEG_LIMIT-used));
        if(n<0) { err=ESP_FAIL;break; }
        if(!n) { if(!esp_http_client_is_complete_data_received(h))err=ESP_ERR_INVALID_RESPONSE;break; }
        used+=(size_t)n;
    }
    if(h)esp_http_client_cleanup(h);
    if(err==ESP_OK && (used==JPEG_LIMIT || used<4))err=ESP_ERR_INVALID_SIZE;
    void *work=err==ESP_OK?malloc(4096):NULL;uint16_t *pixels=err==ESP_OK?malloc(COVER_SIZE*COVER_SIZE*2):NULL;
    if(err==ESP_OK && (!work || !pixels))err=ESP_ERR_NO_MEM;
    decoder_io_t io={.jpeg=jpeg,.length=used,.pixels=pixels};JDEC decoder;
    if(err==ESP_OK && jd_prepare(&decoder,jpeg_read,work,4096,&io)!=JDR_OK)err=ESP_ERR_INVALID_RESPONSE;
    if(err==ESP_OK && (decoder.width!=COVER_SIZE || decoder.height!=COVER_SIZE))err=ESP_ERR_INVALID_SIZE;
    if(err==ESP_OK && jd_decomp(&decoder,jpeg_write,0)!=JDR_OK)err=ESP_ERR_INVALID_RESPONSE;
    free(work);free(jpeg);
    if(err!=ESP_OK) { free(pixels);return err; }
    image->header.magic=LV_IMAGE_HEADER_MAGIC;image->header.cf=LV_COLOR_FORMAT_RGB565;
    image->header.w=COVER_SIZE;image->header.h=COVER_SIZE;image->header.stride=COVER_SIZE*2;
    image->data=(const uint8_t *)pixels;image->data_size=COVER_SIZE*COVER_SIZE*2;return ESP_OK;
}
