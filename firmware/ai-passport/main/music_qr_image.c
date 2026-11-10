#include "music_qr_image.h"
#include "qqmusic_qr_png.h"
#include "miniz.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <stdlib.h>
#include <string.h>
static uint32_t big32(const unsigned char *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static bool inflate(const uint8_t *in,size_t length,uint8_t *out,size_t capacity)
{
    tinfl_decompressor *state=malloc(sizeof(*state));if(!state)return false;
    tinfl_init(state);size_t consumed=length,produced=capacity;
    tinfl_status result=tinfl_decompress(state,in,&consumed,out,out,&produced,
        TINFL_FLAG_PARSE_ZLIB_HEADER|TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(state);return result==TINFL_STATUS_DONE && consumed==length && produced==capacity;
}
void music_qr_image_clear(lv_image_dsc_t *image)
{
    lv_image_cache_drop(image);free((void *)image->data);memset(image,0,sizeof(*image));
}
esp_err_t music_qr_image_decode(const unsigned char *png,size_t length,lv_image_dsc_t *out)
{
    if(length<24)return ESP_ERR_INVALID_RESPONSE;
    unsigned w=big32(png+16),h=big32(png+20);
    if(!w || !h || w>QQMUSIC_QR_SIDE_MAX || h>QQMUSIC_QR_SIDE_MAX)return ESP_ERR_INVALID_SIZE;
    size_t stride=(w+7)/8,bytes=8+stride*h;
    uint8_t *gray=malloc((size_t)w*h),*packed=calloc(1,bytes);
    if(!gray || !packed) { free(gray);free(packed);return ESP_ERR_NO_MEM; }
    if(!qqmusic_qr_png_decode(png,length,inflate,gray,(size_t)w*h,&w,&h)) {
        free(gray);free(packed);return ESP_ERR_INVALID_RESPONSE;
    }
    packed[3]=255;memset(packed+4,255,4);
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)
        if(gray[(size_t)y*w+x])packed[8+(size_t)y*stride+x/8]|=(uint8_t)(0x80>>(x%8));
    free(gray);
    *out=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_I1,.w=w,.h=h,.stride=stride},.data=packed,.data_size=bytes};
    return ESP_OK;
}
