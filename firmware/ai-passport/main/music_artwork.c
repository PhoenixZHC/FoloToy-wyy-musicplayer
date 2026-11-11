#include "music_artwork.h"
#include "music_artwork_policy.h"
#include "bounded_http.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
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
    if(rect->right>=jd->width || rect->bottom>=jd->height)return 0;
    /* ROM 解码器逐块输出；只保留 60×60，QQ 90×90 不另分配整张像素。 */
    unsigned width=rect->right-rect->left+1;
    for(unsigned y=0;y<COVER_SIZE;y++) {
        unsigned sy=y*jd->height/COVER_SIZE;
        if(sy<rect->top || sy>rect->bottom)continue;
        for(unsigned x=0;x<COVER_SIZE;x++) {
            unsigned sx=x*jd->width/COVER_SIZE;
            if(sx<rect->left || sx>rect->right)continue;
            const uint8_t *p=rgb+((sy-rect->top)*width+sx-rect->left)*3;
            io->pixels[y*COVER_SIZE+x]=(uint16_t)(((p[0]&0xf8)<<8)|((p[1]&0xfc)<<3)|(p[2]>>3));
        }
    }
    return 1;
}
esp_err_t music_artwork_load(const char *url,lv_image_dsc_t *image)
{
    if(!url || !image) return ESP_ERR_INVALID_ARG;
    memset(image,0,sizeof(*image));
    char resized[320];
    if(!music_artwork_request_url(url,resized,sizeof(resized)))return ESP_ERR_INVALID_ARG;
    uint8_t *jpeg=malloc(JPEG_LIMIT);if(!jpeg)return ESP_ERR_NO_MEM;
    int64_t deadline=esp_timer_get_time()+10000000;
    esp_http_client_config_t cfg={.url=resized,.timeout_ms=5000,.buffer_size=1024,.buffer_size_tx=512,
        .crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true,
        .is_async=true,.event_handler=bounded_http_event_handler};
    esp_http_client_handle_t h=esp_http_client_init(&cfg);esp_err_t err=h?ESP_OK:ESP_ERR_NO_MEM;size_t used=0;
    if(err==ESP_OK)err=bounded_http_open(h,deadline);
    if(err==ESP_OK)err=bounded_http_make_blocking(h);
    if(err==ESP_OK)err=bounded_http_start(h,deadline);
    if(err==ESP_OK) {
        int64_t size=esp_http_client_fetch_headers(h);
        if(size<0 || esp_http_client_get_status_code(h)!=200)err=ESP_ERR_INVALID_RESPONSE;
        else if(size>JPEG_LIMIT)err=ESP_ERR_INVALID_SIZE;
    }
    while(err==ESP_OK && used<JPEG_LIMIT) {
        if(esp_timer_get_time()>=deadline) { err=ESP_ERR_TIMEOUT;break; }
        int n=esp_http_client_read(h,(char *)jpeg+used,(int)(JPEG_LIMIT-used));
        if(n<0) { err=ESP_FAIL;break; }
        if(!n) { if(!esp_http_client_is_complete_data_received(h))err=ESP_ERR_INVALID_RESPONSE;break; }
        used+=(size_t)n;
    }
    if(h) {
        if(bounded_http_expired(h) || esp_timer_get_time()>=deadline)err=ESP_ERR_TIMEOUT;
        bounded_http_stop(h);
        esp_http_client_cleanup(h);
    }
    if(err==ESP_OK && (used==JPEG_LIMIT || used<4))err=ESP_ERR_INVALID_SIZE;
    void *work=err==ESP_OK?malloc(4096):NULL;uint16_t *pixels=err==ESP_OK?malloc(COVER_SIZE*COVER_SIZE*2):NULL;
    if(err==ESP_OK && (!work || !pixels))err=ESP_ERR_NO_MEM;
    decoder_io_t io={.jpeg=jpeg,.length=used,.pixels=pixels};JDEC decoder;
    if(err==ESP_OK && jd_prepare(&decoder,jpeg_read,work,4096,&io)!=JDR_OK)err=ESP_ERR_INVALID_RESPONSE;
    if(err==ESP_OK && (decoder.width<COVER_SIZE || decoder.height<COVER_SIZE ||
                      decoder.width>512 || decoder.height>512))err=ESP_ERR_INVALID_SIZE;
    if(err==ESP_OK && jd_decomp(&decoder,jpeg_write,0)!=JDR_OK)err=ESP_ERR_INVALID_RESPONSE;
    free(work);free(jpeg);
    if(err!=ESP_OK) { free(pixels);return err; }
    image->header.magic=LV_IMAGE_HEADER_MAGIC;image->header.cf=LV_COLOR_FORMAT_RGB565;
    image->header.w=COVER_SIZE;image->header.h=COVER_SIZE;image->header.stride=COVER_SIZE*2;
    image->data=(const uint8_t *)pixels;image->data_size=COVER_SIZE*COVER_SIZE*2;return ESP_OK;
}
