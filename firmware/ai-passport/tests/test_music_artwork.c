#include "music_artwork.h"
#include "esp_http_client.h"
#include "bounded_http.h"
#include "esp32c3/rom/tjpgd.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fake_http { int unused; };
static struct fake_http http;
static unsigned dimension=90,cleanups,decodes;
static int64_t body_length=100,clock_us;
static bool read_done;
static int status=200;
static bool guarded;
esp_err_t bounded_http_event_handler(esp_http_client_event_t *event) { (void)event;return ESP_OK; }
esp_err_t bounded_http_open(esp_http_client_handle_t h,int64_t deadline)
{ assert(deadline>clock_us);return esp_http_client_open(h,0); }
esp_err_t bounded_http_start(esp_http_client_handle_t h,int64_t deadline)
{ assert(h==&http && deadline>clock_us && !guarded);guarded=true;return ESP_OK; }
void bounded_http_stop(esp_http_client_handle_t h) { assert(h==&http);guarded=false; }
bool bounded_http_expired(esp_http_client_handle_t h) { assert(h==&http);return false; }
esp_err_t bounded_http_make_blocking(esp_http_client_handle_t h) { assert(h==&http);return ESP_OK; }
int64_t esp_timer_get_time(void) { return clock_us; }
esp_err_t esp_crt_bundle_attach(void *config) { (void)config;return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    assert(config->crt_bundle_attach && config->disable_auto_redirect && config->is_async);
    assert(config->event_handler==bounded_http_event_handler);
    assert(!strncmp(config->url,"https://",8));read_done=false;return &http;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t h,int ms) { (void)h;assert(ms>0);return ESP_OK; }
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int n) { (void)h;(void)n;return ESP_OK; }
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h) { (void)h;return body_length; }
int esp_http_client_get_status_code(esp_http_client_handle_t h) { (void)h;return status; }
int esp_http_client_read(esp_http_client_handle_t h,char *data,int n)
{
    (void)h;
    if(read_done)return 0;
    read_done=true;
    int size=body_length>0?(int)body_length:16384;
    if(size>n)size=n;memset(data,0,(size_t)size);return size;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h) { (void)h;return read_done; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h) { assert(h==&http && !guarded);cleanups++;return ESP_OK; }

int jd_prepare(JDEC *jd,UINT (*read_fn)(JDEC *,BYTE *,UINT),void *work,UINT length,void *device)
{
    (void)read_fn;assert(work && length==4096);jd->device=device;jd->width=dimension;jd->height=dimension;return JDR_OK;
}
int jd_decomp(JDEC *jd,UINT (*write_fn)(JDEC *,void *,JRECT *),BYTE scale)
{
    assert(scale==0);decodes++;
    /* 模拟 ROM 输出含边缘窄块的 RGB888 梯度，验证 90→60 的实际回调采样。 */
    for(unsigned y=0;y<dimension;y+=16)for(unsigned x=0;x<dimension;x+=16) {
        JRECT r={x,x+15<dimension?x+15:dimension-1,y,y+15<dimension?y+15:dimension-1};
        BYTE pixels[16*16*3];size_t i=0;
        for(unsigned sy=r.top;sy<=r.bottom;sy++)for(unsigned sx=r.left;sx<=r.right;sx++) {
            pixels[i++]=(BYTE)sx;pixels[i++]=(BYTE)sy;pixels[i++]=0x80;
        }
        if(!write_fn(jd,pixels,&r))return -1;
    }
    return JDR_OK;
}

int main(void)
{
    const char *qq="https://y.gtimg.cn/music/photo_new/T002R90x90M000000MkMni19ClKG.jpg";
    lv_image_dsc_t image;
    for(unsigned i=0;i<2;i++) {
        dimension=i?60:90;
        assert(music_artwork_load(qq,&image)==ESP_OK);
        assert(image.header.w==60 && image.header.h==60 && image.data_size==7200);
        const uint16_t *p=(const uint16_t *)image.data;
        for(unsigned y=0;y<60;y++)for(unsigned x=0;x<60;x++) {
            unsigned r=x*dimension/60,g=y*dimension/60;
            uint16_t expected=(uint16_t)(((r&0xf8)<<8)|((g&0xfc)<<3)|0x10);
            assert(p[y*60+x]==expected);
        }
        free((void *)image.data);
    }
    unsigned old_decodes=decodes;
    dimension=513;assert(music_artwork_load(qq,&image)==ESP_ERR_INVALID_SIZE && !image.data);
    dimension=1;assert(music_artwork_load(qq,&image)==ESP_ERR_INVALID_SIZE && !image.data);
    assert(decodes==old_decodes);
    dimension=90;body_length=16385;
    assert(music_artwork_load(qq,&image)==ESP_ERR_INVALID_SIZE && !image.data);
    body_length=0;assert(music_artwork_load(qq,&image)==ESP_ERR_INVALID_SIZE && !image.data);
    body_length=100;status=302;assert(music_artwork_load(qq,&image)==ESP_ERR_INVALID_RESPONSE && !image.data);
    assert(cleanups==7);
    puts("Artwork download/decode: 7 groups passed");return 0;
}
