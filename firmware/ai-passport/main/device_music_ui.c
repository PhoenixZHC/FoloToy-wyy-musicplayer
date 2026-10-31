#include "device_music_ui.h"

#include "bsp_battery.h"

#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(netease_ui_font_8);
LV_FONT_DECLARE(netease_ui_font_9);
LV_FONT_DECLARE(netease_ui_font_10);
LV_FONT_DECLARE(netease_ui_font_12);
LV_FONT_DECLARE(netease_ui_font_13);
LV_FONT_DECLARE(netease_ui_font_14);
LV_FONT_DECLARE(netease_ui_font_17);
LV_FONT_DECLARE(netease_ui_font_18);
LV_FONT_DECLARE(netease_ui_font_22);
LV_FONT_DECLARE(netease_ui_font_24);
LV_FONT_DECLARE(netease_ui_font_43);
LV_FONT_DECLARE(netease_ui_font_46);
LV_FONT_DECLARE(netease_ui_font_60);
LV_FONT_DECLARE(netease_ui_font_80);

static const lv_font_t *font_px(int size)
{
    switch (size) {
    case 8: return &netease_ui_font_8;
    case 9: return &netease_ui_font_9;
    case 10: return &netease_ui_font_10;
    case 12: return &netease_ui_font_12;
    case 13: return &netease_ui_font_13;
    case 14: return &netease_ui_font_14;
    case 17: return &netease_ui_font_17;
    case 18: return &netease_ui_font_18;
    case 22: return &netease_ui_font_22;
    case 24: return &netease_ui_font_24;
    case 43: return &netease_ui_font_43;
    case 46: return &netease_ui_font_46;
    case 60: return &netease_ui_font_60;
    case 80: return &netease_ui_font_80;
    default: return &netease_ui_font_14;
    }
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    if (parent == NULL) return NULL;
    lv_obj_t *obj = lv_obj_create(parent);
    if (obj == NULL) return NULL;
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent,
                       const char *text,
                       int x,
                       int y,
                       int w,
                       int h,
                       int size,
                       uint32_t color,
                       lv_text_align_t align)
{
    if (parent == NULL) return NULL;
    lv_obj_t *obj = lv_label_create(parent);
    if (obj == NULL) return NULL;
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_text_font(obj, font_px(size), 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, align, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    lv_label_set_text(obj, text ? text : "");
    return obj;
}

static void scroll_if_needed(lv_obj_t *obj,const char *text,int width,int size)
{
    if(!obj || !text)return;
    lv_point_t measured;
    lv_text_get_size(&measured,text,font_px(size),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    if(measured.x>width) {
        lv_label_set_long_mode(obj,LV_LABEL_LONG_MODE_SCROLL);
        lv_obj_set_style_anim_duration(obj,7000,LV_PART_MAIN);
    }
}

static void format_time(char *out, size_t out_len, int milliseconds)
{
    if (out == NULL || out_len == 0) return;
    if (milliseconds < 0) milliseconds = 0;
    int seconds = milliseconds / 1000;
    snprintf(out, out_len, "%02d:%02d", seconds / 60, seconds % 60);
}


static unsigned s_number;
static char s_playlist[64];
static int s_volume;
static bool s_buffering;
void device_music_ui_set_buffering(bool buffering) { s_buffering=buffering; }
static bool s_connected;
static unsigned s_signal_level;
static char s_clock_text[6]="--:--";
static lv_obj_t *s_clock_label;

static void clock_label_deleted(lv_event_t *event)
{
    if(lv_event_get_target(event)==s_clock_label)s_clock_label=NULL;
}

void device_music_ui_set_clock(int hour,int minute,bool valid)
{
    char text[6];
    if(valid && hour>=0 && hour<24 && minute>=0 && minute<60)
        snprintf(text,sizeof(text),"%02d:%02d",hour,minute);
    else memcpy(text,"--:--",sizeof(text));
    if(strcmp(text,s_clock_text)==0)return;
    memcpy(s_clock_text,text,sizeof(s_clock_text));
    if(s_clock_label)lv_label_set_text(s_clock_label,s_clock_text);
}
static char s_now_song[128];
static bool s_now_paused;
static const lv_image_dsc_t *s_artwork;
static const char *s_lyric;
void device_music_ui_set_media(const lv_image_dsc_t *image,const char *lyric)
{ s_artwork=image;s_lyric=lyric; }
void device_music_ui_update_lyric(const device_music_ui_player_widgets_t *w,const char *lyric)
{ if(w && w->lyric_label && strcmp(lv_label_get_text(w->lyric_label),lyric?lyric:""))lv_label_set_text(w->lyric_label,lyric?lyric:""); }
void device_music_ui_set_now_playing(const char *song, bool paused)
{ snprintf(s_now_song,sizeof(s_now_song),"%s",song? song: "");s_now_paused=paused; }
void device_music_ui_set_network(bool connected, unsigned signal_level)
{ s_connected=connected;s_signal_level=signal_level>3?3:signal_level; }
void device_music_ui_set_playback_context(unsigned number, const char *playlist, int volume)
{
    s_number=number;s_volume=volume;
    snprintf(s_playlist,sizeof(s_playlist),"%s",playlist?playlist:"");
}
static void line(lv_obj_t *screen,int y,int height,uint32_t color) { box(screen,15,y,210,height,color,0); }
static void eyeline(lv_obj_t *screen,const char *left,const char *right)
{
    const device_music_ui_theme_t *t=device_music_ui_theme();
    label(screen,left,15,35,145,14,9,t->netease_red,LV_TEXT_ALIGN_LEFT);
    label(screen,right,161,35,64,14,9,t->netease_red,LV_TEXT_ALIGN_RIGHT);
}
static void heading(lv_obj_t *s,const char *text) { label(s,text,15,54,210,31,24,device_music_ui_theme()->text,LV_TEXT_ALIGN_LEFT); }
static void folio(lv_obj_t *s,const char *text) { line(s,263,1,device_music_ui_theme()->divider);label(s,text,15,272,210,13,9,device_music_ui_theme()->muted_text,LV_TEXT_ALIGN_LEFT); }
static void draw_header(lv_obj_t *s,const device_music_ui_screen_spec_t *spec)
{
    const device_music_ui_theme_t *t=device_music_ui_theme();
    const int text_size=10,text_y=8,text_height=12;
    if(spec->screen!=DEVICE_MUSIC_SCREEN_PLAYLISTS && spec->screen!=DEVICE_MUSIC_SCREEN_TRACKS &&
       spec->screen!=DEVICE_MUSIC_SCREEN_PLAYING && spec->screen!=DEVICE_MUSIC_SCREEN_PAUSED)
        label(s,"FOLO MUSIC",14,text_y,80,text_height,text_size,t->text,LV_TEXT_ALIGN_LEFT);
    s_clock_label=label(s,s_clock_text,98,text_y,44,text_height,text_size,t->text,LV_TEXT_ALIGN_CENTER);
    if(s_clock_label)lv_obj_add_event_cb(s_clock_label,clock_label_deleted,LV_EVENT_DELETE,NULL);
    int soc=bsp_battery_soc();char status[40];
    if(s_connected && soc>=0)snprintf(status,sizeof(status),"%d%%",soc);
    else if(s_connected)snprintf(status,sizeof(status)," ");
    else if(soc>=0)snprintf(status,sizeof(status),"离线 · %d%%",soc);
    else snprintf(status,sizeof(status),"离线");
    label(s,status,146,text_y,80,text_height,text_size,t->muted_text,LV_TEXT_ALIGN_RIGHT);
    if(s_connected) {
        /* 信号条底部对齐 WiFi 字体基线，避免受字号行高留白影响。 */
        int x=177,bottom=text_y+font_px(text_size)->line_height-font_px(text_size)->base_line;
        label(s,"WiFi",147,text_y,27,text_height,text_size,t->muted_text,LV_TEXT_ALIGN_RIGHT);
        for(unsigned i=0;i<3;i++)box(s,x+(int)i*4,bottom-(3+(int)i*3),3,3+(int)i*3,
            i<s_signal_level?t->text:t->divider,0);
    }
    box(s,14,23,212,1,t->divider,0);
}
static void draw_footer(lv_obj_t *s,const device_music_ui_footer_t *f)
{
    const device_music_ui_theme_t *t=device_music_ui_theme();
    box(s,0,293,240,27,t->background,0);box(s,0,293,240,1,t->divider,0);
    const char *text[]={f->up_label,f->ok_label,f->down_label};
    for(int i=0;i<3;i++) {
        char hint[64];
        device_music_ui_format_key_hint(hint,sizeof(hint),(unsigned)i,text[i]);
        label(s,hint,i*80,302,80,14,9,i==1?t->text:t->muted_text,LV_TEXT_ALIGN_CENTER);
    }
}
static bool setup_screen(lv_obj_t *s,device_music_screen_t id)
{
    const device_music_ui_theme_t *t=device_music_ui_theme();const device_music_ui_screen_spec_t *spec=device_music_ui_screen(id);
    if(!s || !spec)return false;
    lv_obj_remove_style_all(s);lv_obj_set_size(s,240,320);lv_obj_set_style_bg_opa(s,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(s,lv_color_hex(t->background),0);lv_obj_clear_flag(s,LV_OBJ_FLAG_SCROLLABLE);
    draw_header(s,spec);draw_footer(s,&spec->footer);return true;
}
lv_obj_t *device_music_ui_create_screen(device_music_screen_t id)
{
    lv_obj_t *s=lv_obj_create(NULL);if(!s)return NULL;
    if(!setup_screen(s,id)) { lv_obj_delete(s);return NULL; }return s;
}
bool device_music_ui_reset_screen(lv_obj_t *s,device_music_screen_t id) { if(!s)return false;lv_obj_clean(s);return setup_screen(s,id); }
void device_music_ui_draw_setup(lv_obj_t *s,const char *ap)
{
    (void)ap;
    const device_music_ui_theme_t *t=device_music_ui_theme();
    eyeline(s,"WIFI / 选择网络","");heading(s,"连接 Wi-Fi。");
    label(s,"设备直接扫描附近网络",15,91,210,16,10,t->muted_text,LV_TEXT_ALIGN_LEFT);
    folio(s,"长按 OK · 重新扫描");
    device_music_ui_set_footer(s,"上一个","选择","下一个");
}
void device_music_ui_draw_wifi_scan(lv_obj_t *s,const char *const *ssids,
                                    const bool *secure,size_t count,size_t selected,const char *detail)
{
    const device_music_ui_theme_t *t=device_music_ui_theme();
    eyeline(s,"WIFI / 选择网络","");heading(s,"连接 Wi-Fi。");
    label(s,"设备直接扫描附近网络",15,91,210,16,10,t->muted_text,LV_TEXT_ALIGN_LEFT);
    size_t first=count>4 && selected>2?selected-2:0;
    if(count>4 && first>count-4)first=count-4;
    for(size_t row=0;row<4 && first+row<count;row++) {
        size_t i=first+row;int y=112+(int)row*34;
        if(i==selected)box(s,15,y,210,32,t->card,0);
        line(s,y,i==selected?2:1,i==selected?t->netease_red:t->divider);
        label(s,ssids[i],25,y+7,165,18,12,t->text,LV_TEXT_ALIGN_LEFT);
        if(secure && secure[i])label(s,"锁",195,y+7,20,18,10,t->muted_text,LV_TEXT_ALIGN_RIGHT);
    }
    if(!count)label(s,"没有找到网络。",15,132,210,20,12,t->muted_text,LV_TEXT_ALIGN_LEFT);
    if(detail && detail[0])label(s,detail,15,249,210,12,9,t->muted_text,LV_TEXT_ALIGN_LEFT);
    folio(s,"长按 OK · 重新扫描");
    device_music_ui_set_footer(s,"上一个","选择","下一个");
}
void device_music_ui_draw_wifi_password(lv_obj_t *s,const char *ssid,
                                         const char *password,const char *characters,
                                         size_t selected_character,bool showing,
                                         const char *group,const char *detail)
{
    const device_music_ui_theme_t *t=device_music_ui_theme();char masked[66];
    snprintf(masked,sizeof(masked),"%s",password?password:"");
    if(!showing)for(size_t i=0;masked[i];i++)masked[i]='*';
    size_t length=strlen(masked);
    if(length>23)memmove(masked,masked+length-23,24);
    char length_text[16];snprintf(length_text,sizeof(length_text),"%u / 64",(unsigned)length);
    eyeline(s,"WIFI / 密码",length_text);heading(s,"输入网络密码。");
    label(s,ssid?ssid:"未选择网络",15,94,210,18,12,t->green,LV_TEXT_ALIGN_LEFT);
    box(s,15,121,210,34,t->card,0);label(s,masked,25,130,190,18,12,t->text,LV_TEXT_ALIGN_LEFT);
    const char *keys=characters?characters:"qwertyuiopasdfghjklzxcvbnm";
    size_t key_count=strlen(keys);
    size_t row_counts[3]={10,9,7};
    if(key_count==10) { row_counts[0]=10;row_counts[1]=row_counts[2]=0; }
    else if(key_count>26) { row_counts[0]=11;row_counts[1]=11;row_counts[2]=key_count-22; }
    /* 三行矩阵复用字符存储，避免为每个按键创建对象和样式耗尽 C3 的 LVGL 堆。 */
    static char key_text[33][4];
    static const char *key_maps[3][12];
    size_t offset=0;
    for(size_t row=0;row<3;row++) {
        size_t count=row_counts[row];
        if(!count)continue;
        int key_width=count==11?18:count==10?19:count==9?20:22;
        int gap=count>9?1:2;
        int total=(int)count*key_width+(int)(count-1)*gap;
        int left=(240-total)/2;
        for(size_t col=0;col<count;col++) {
            size_t index=offset+col;
            if(keys[index]==' ')snprintf(key_text[index],sizeof(key_text[index]),"空");
            else {key_text[index][0]=keys[index];key_text[index][1]=0;}
            key_maps[row][col]=key_text[index];
        }
        key_maps[row][count]="";
        lv_obj_t *matrix=lv_buttonmatrix_create(s);
        lv_obj_remove_style_all(matrix);
        lv_obj_set_pos(matrix,left,157+(int)row*27);
        lv_obj_set_size(matrix,total,23);
        lv_obj_clear_flag(matrix,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_column(matrix,gap,0);
        lv_obj_set_style_bg_opa(matrix,LV_OPA_COVER,LV_PART_ITEMS);
        lv_obj_set_style_bg_color(matrix,lv_color_hex(t->card),LV_PART_ITEMS);
        lv_obj_set_style_radius(matrix,2,LV_PART_ITEMS);
        lv_obj_set_style_text_font(matrix,font_px(12),LV_PART_ITEMS);
        lv_obj_set_style_text_color(matrix,lv_color_hex(t->text),LV_PART_ITEMS);
        lv_obj_set_style_bg_color(matrix,lv_color_hex(t->netease_red),LV_PART_ITEMS|LV_STATE_CHECKED);
        lv_obj_set_style_text_color(matrix,lv_color_hex(t->background),LV_PART_ITEMS|LV_STATE_CHECKED);
        lv_buttonmatrix_set_map(matrix,key_maps[row]);
        if(selected_character>=offset && selected_character<offset+count)
            lv_buttonmatrix_set_button_ctrl(matrix,selected_character-offset,LV_BUTTONMATRIX_CTRL_CHECKED);
        offset+=count;
    }
    char group_hint[80];snprintf(group_hint,sizeof(group_hint),"%s · 长按 ↑↓ 换组",group?group:"小写字母");
    label(s,group_hint,15,238,210,13,9,t->muted_text,LV_TEXT_ALIGN_CENTER);
    if(detail && detail[0])label(s,detail,15,251,210,12,9,t->netease_red,LV_TEXT_ALIGN_CENTER);
    folio(s,"长按 OK · 连接 / 删除 / 显隐");
    device_music_ui_set_footer(s,"上一个","输入","下一个");
}
static void stamp(lv_obj_t *s,const char *text,int y,int height)
{
    const device_music_ui_theme_t *t=device_music_ui_theme();
    box(s,15,y,210,height,t->card,0);
    label(s,text,25,y+12,190,height-18,12,t->text,LV_TEXT_ALIGN_LEFT);
}
void device_music_ui_draw_message(lv_obj_t *s,device_music_screen_t id,const char *title,const char *detail)
{
    const device_music_ui_screen_spec_t *spec=device_music_ui_screen(id);
    const device_music_ui_theme_t *t=device_music_ui_theme();
    eyeline(s,"NOTICE / 状态","");
    label(s,"…",15,58,210,49,24,t->netease_red,LV_TEXT_ALIGN_LEFT);
    label(s,title?title:spec->title,15,114,210,33,24,t->text,LV_TEXT_ALIGN_LEFT);
    label(s,detail?detail:spec->detail,15,154,210,60,10,t->muted_text,LV_TEXT_ALIGN_LEFT);
    folio(s,"长按 OK · 返回");
}
void device_music_ui_draw_progress(lv_obj_t *s,device_music_screen_t id,const char *title,const char *detail,int steps)
{
    const device_music_ui_theme_t *t=device_music_ui_theme();
    eyeline(s,id==DEVICE_MUSIC_SCREEN_REGISTERING?"SETUP / 02":"CONTENTS / 读取","");heading(s,title?title:(id==DEVICE_MUSIC_SCREEN_REGISTERING?"正在连接。":"正在翻开目录。"));
    label(s,steps<2?"01 / 02":"02 / 02",15,94,210,49,24,t->netease_red,LV_TEXT_ALIGN_LEFT);
    stamp(s,detail?detail:(id==DEVICE_MUSIC_SCREEN_REGISTERING?"连接 Wi-Fi\n接下来：校准设备时间":"正在获取歌曲信息"),156,62);
    label(s,id==DEVICE_MUSIC_SCREEN_REGISTERING?"请保持设备通电。":"正在连接网易云，请稍候。",15,231,210,20,10,t->muted_text,LV_TEXT_ALIGN_LEFT);
    folio(s,id==DEVICE_MUSIC_SCREEN_REGISTERING?"网络连接":"长按 OK · 快捷导航");
    device_music_ui_set_footer(s,"—","等待","—");
}
void device_music_ui_draw_success(lv_obj_t *s,const char *title,const char *detail)
{
    eyeline(s,"WELCOME BACK","");heading(s,title?title:"音乐，已经就绪。");
    stamp(s,detail?detail:"登录状态已保存",110,62);folio(s,"下次开机自动恢复");
}
void device_music_ui_draw_error(lv_obj_t *s,device_music_screen_t id,const char *title,const char *detail)
{
    if(id==DEVICE_MUSIC_SCREEN_SETUP_FAILED) {
        device_music_ui_draw_notice(s,DEVICE_MUSIC_NOTICE_WIFI_FAILED,NULL);return;
    }
    device_music_ui_draw_message(s,id,title,detail);
    box(s,15,58,210,49,device_music_ui_theme()->background,0);
    label(s,id==DEVICE_MUSIC_SCREEN_PLAY_FAILED?"AUDIO":"LOAD",15,58,210,49,24,device_music_ui_theme()->netease_red,LV_TEXT_ALIGN_LEFT);
    box(s,15,216,210,29,device_music_ui_theme()->card,0);
    label(s,"OK · 重试",24,222,192,17,12,device_music_ui_theme()->text,LV_TEXT_ALIGN_LEFT);
}
lv_obj_t *device_music_ui_draw_qr(lv_obj_t *s,device_music_screen_t id,const char *title,const char *detail)
{
    (void)id;const device_music_ui_theme_t *t=device_music_ui_theme();
    eyeline(s,"SIGN IN / 03","");
    label(s,title?title:"扫码，让音乐进来。",15,54,210,30,22,t->text,LV_TEXT_ALIGN_LEFT);
    label(s,"使用网易云音乐 App 扫描",15,88,210,17,10,t->muted_text,LV_TEXT_ALIGN_LEFT);
    box(s,54,110,132,132,0xFFFFFF,0);
    lv_obj_t *qr=lv_qrcode_create(s);if(!qr)return NULL;
    lv_qrcode_set_size(qr,132);lv_qrcode_set_dark_color(qr,lv_color_black());lv_qrcode_set_light_color(qr,lv_color_white());
    lv_qrcode_set_quiet_zone(qr,true);lv_obj_set_pos(qr,54,110);
    label(s,detail?detail:"等待扫码 / OK 刷新二维码",15,265,210,17,9,t->muted_text,LV_TEXT_ALIGN_CENTER);
    return qr;
}
void device_music_ui_draw_confirm(lv_obj_t *s,const char *title,const char *detail,lv_obj_t **out)
{
    const device_music_ui_theme_t *t=device_music_ui_theme();if(out)*out=NULL;
    eyeline(s,"SIGN IN / 03","");heading(s,title?title:"在手机上，确认。");
    label(s,"已扫码",15,94,210,62,43,t->netease_red,LV_TEXT_ALIGN_LEFT);
    stamp(s,"请在手机确认登录",166,41);
    label(s,detail?detail:"确认后，设备会进入你的歌单。\n无需再次扫码。",15,220,210,34,10,t->muted_text,LV_TEXT_ALIGN_LEFT);
    folio(s,"等待手机确认");
}
void device_music_ui_draw_list(lv_obj_t *s,device_music_screen_t id,const char *title,const char *count,const device_music_ui_row_t *rows,size_t n)
{
    const device_music_ui_theme_t *t=device_music_ui_theme();
    eyeline(s,"CONTENTS / 目录",count?count:"");heading(s,title?title:device_music_ui_screen(id)->title);
    if(!n) {
        box(s,15,54,210,33,t->background,0);
        label(s,id==DEVICE_MUSIC_SCREEN_PLAYLISTS?"还没有歌单。":"这一页，暂时空着。",15,54,210,33,22,t->text,LV_TEXT_ALIGN_LEFT);
        label(s,"00",15,94,210,58,60,t->netease_red,LV_TEXT_ALIGN_LEFT);
        label(s,id==DEVICE_MUSIC_SCREEN_PLAYLISTS?"在手机网易云创建或收藏歌单，\n然后回到这里刷新。":"在手机给这个歌单添加歌曲，\n或返回选择其他歌单。",15,159,210,34,10,t->muted_text,LV_TEXT_ALIGN_LEFT);
        stamp(s,id==DEVICE_MUSIC_SCREEN_PLAYLISTS?"OK · 刷新歌单":"OK · 返回歌单",202,36);
        folio(s,"空内容，不是网络故障");return;
    }
    for(size_t i=0;i<n && i<4;i++) {
        int y=94+(int)i*42;const device_music_ui_row_t *r=&rows[i];
        if(r->selected)box(s,15,y,210,42,t->card,0);
        line(s,y,r->selected?2:1,r->selected?t->netease_red:t->divider);
        char number[16];snprintf(number,sizeof(number),"%02u",r->number);
        label(s,number,16,y+10,25,26,22,r->selected?t->netease_red:t->muted_text,LV_TEXT_ALIGN_LEFT);
        label(s,r->title,47,y+6,155,18,12,r->selected?t->netease_red:t->text,LV_TEXT_ALIGN_LEFT);
        label(s,r->sub,47,y+25,155,13,9,t->muted_text,LV_TEXT_ALIGN_LEFT);
        if(r->playing)label(s,"播放",203,y+15,22,14,8,t->netease_red,LV_TEXT_ALIGN_RIGHT);
    }
    line(s,263,2,t->text);
    char mini[180];snprintf(mini,sizeof(mini),"%s%s%s",s_now_song[0]?(s_now_paused?"暂停":"正在播放"):"长按 OK",s_now_song[0]?" · ":" · ",s_now_song[0]?s_now_song:"快捷导航");
    label(s,mini,15,272,210,13,9,t->muted_text,LV_TEXT_ALIGN_LEFT);
}
static void draw_player_progress(lv_obj_t *s,int percent,int elapsed,int duration,device_music_ui_player_widgets_t *w)
{
    const device_music_ui_theme_t *t=device_music_ui_theme();
    line(s,254,3,t->divider);int fill=device_music_ui_progress_width(210,percent);
    lv_obj_t *f=box(s,15,254,fill?fill:1,3,t->netease_red,0);if(!fill)lv_obj_set_style_bg_opa(f,LV_OPA_TRANSP,0);
    char a[12],b[12];format_time(a,sizeof(a),elapsed);format_time(b,sizeof(b),duration);
    lv_obj_t *al=label(s,a,15,263,80,13,9,t->muted_text,LV_TEXT_ALIGN_LEFT);
    lv_obj_t *bl=label(s,b,145,263,80,13,9,t->muted_text,LV_TEXT_ALIGN_RIGHT);
    if(w) { w->progress_fill=f;w->elapsed_label=al;w->duration_label=bl; }
}
void device_music_ui_draw_player(lv_obj_t *s,const char *song,const char *artist,int percent,int elapsed,int duration,bool paused,device_music_ui_player_widgets_t *w)
{
    const device_music_ui_theme_t *t=device_music_ui_theme();char meta[24];
    eyeline(s,"",s_now_paused?"已暂停":"正在播放");
    label(s,s_playlist,15,57,210,17,10,t->muted_text,LV_TEXT_ALIGN_LEFT);
    int text_width=s_artwork?140:210;
    if(s_artwork) {
        lv_obj_t *art=lv_image_create(s);lv_obj_remove_style_all(art);
        lv_obj_set_pos(art,165,92);lv_image_set_src(art,s_artwork);
    }
    lv_obj_t *song_label=label(s,song?song:"歌曲信息不可用",15,91,text_width,34,24,t->text,LV_TEXT_ALIGN_LEFT);
    scroll_if_needed(song_label,song?song:"歌曲信息不可用",text_width,24);
    lv_obj_t *artist_label=label(s,artist?artist:"",15,135,text_width,18,12,t->muted_text,LV_TEXT_ALIGN_LEFT);
    scroll_if_needed(artist_label,artist?artist:"",text_width,12);
    line(s,188,1,t->divider);
    lv_obj_t *lyric_label=label(s,s_buffering && !paused?"正在缓冲…":s_lyric,15,199,210,42,14,t->text,LV_TEXT_ALIGN_CENTER);
    if(w)w->lyric_label=lyric_label;
    draw_player_progress(s,percent,elapsed,duration,w);
    label(s,paused?"已暂停":s_buffering?"缓冲中":"正在播放",15,279,100,13,9,t->netease_red,LV_TEXT_ALIGN_LEFT);
    snprintf(meta,sizeof(meta),"音量 %d",s_volume);label(s,meta,125,279,100,13,9,t->muted_text,LV_TEXT_ALIGN_RIGHT);
}
void device_music_ui_update_player_progress(const device_music_ui_player_widgets_t *w,int percent,int elapsed,int duration)
{
    if(!w)return;
    int fill=device_music_ui_progress_width(210,percent);
    if(w->progress_fill) { lv_obj_set_width(w->progress_fill,fill?fill:1);lv_obj_set_style_bg_opa(w->progress_fill,fill?LV_OPA_COVER:LV_OPA_TRANSP,0); }
    char a[12],b[12];format_time(a,sizeof(a),elapsed);format_time(b,sizeof(b),duration);
    if(w->elapsed_label)lv_label_set_text(w->elapsed_label,a);
    if(w->duration_label)lv_label_set_text(w->duration_label,b);
}
static void menu_rows(lv_obj_t *s,const char *const *items,size_t count,size_t selected,int y)
{
    const device_music_ui_theme_t *t=device_music_ui_theme();
    size_t first=count>4 && selected>2?selected-2:0;
    if(count>4 && first>count-4)first=count-4;
    for(size_t row=0;row<4 && first+row<count;row++) {
        size_t i=first+row;int top=y+(int)row*40;
        if(i==selected)box(s,15,top,210,40,t->card,0);
        line(s,top,i==selected?2:1,i==selected?t->netease_red:t->divider);
        char no[12];snprintf(no,sizeof(no),"%02u",(unsigned)i+1);
        label(s,no,18,top+9,24,24,17,t->muted_text,LV_TEXT_ALIGN_LEFT);
        label(s,items[i],47,top+12,176,20,12,i==selected?t->netease_red:t->text,LV_TEXT_ALIGN_LEFT);
    }
}
void device_music_ui_draw_menu(lv_obj_t *s,const char *title,const char *detail,const char *const *items,size_t count,size_t selected)
{
    char position[24];snprintf(position,sizeof(position),"%u / %u",(unsigned)selected+1,(unsigned)count);
    eyeline(s,"GO TO / 导航",position);heading(s,title);
    if(detail)stamp(s,detail,97,62);
    menu_rows(s,items,count,selected,detail?172:94);
    folio(s,"长按 OK · 关闭导航");device_music_ui_footer_t f={"上移","确认","下移"};draw_footer(s,&f);
}
static void pair(lv_obj_t *s,const char *key,const char *value,int y)
{
    const device_music_ui_theme_t *t=device_music_ui_theme();line(s,y,1,t->divider);
    label(s,key,15,y+12,66,20,10,t->muted_text,LV_TEXT_ALIGN_LEFT);
    label(s,value,82,y+12,143,20,12,t->text,LV_TEXT_ALIGN_RIGHT);
}
void device_music_ui_draw_preferences(lv_obj_t *s,device_music_ui_preference_t page,const char *detail,
                                      const char *version,const char *const *items,size_t n,size_t selected)
{
    const device_music_ui_theme_t *t=device_music_ui_theme();int y=94;
    const char *eye="PREFERENCES / 设置",*title="按你的习惯。";
    switch(page) {
    case DEVICE_MUSIC_PREF_APPEARANCE:eye="DISPLAY / 显示";title="白天与夜晚。";break;
    case DEVICE_MUSIC_PREF_SCREEN_TIMEOUT:eye="SCREEN / 熄屏";title="让屏幕歇一会。";break;
    case DEVICE_MUSIC_PREF_NETWORK:eye="NETWORK / 网络";title="连接着的世界。";break;
    case DEVICE_MUSIC_PREF_ACCOUNT:eye="ACCOUNT / 账号";title="这本，属于你。";break;
    case DEVICE_MUSIC_PREF_LOGOUT:case DEVICE_MUSIC_PREF_RESET_WIFI:eye="CONFIRM / 确认";title=page==DEVICE_MUSIC_PREF_LOGOUT?"退出这个账号？":"重新配置网络？";break;
    case DEVICE_MUSIC_PREF_ABOUT:eye="FOLO MUSIC / 设备";title="关于 Folo Music。";break;
    case DEVICE_MUSIC_PREF_END:eye="END OF SIDE / 结束";title="这一轮，听完了。";break;
    default:break;
    }
    eyeline(s,eye,"");heading(s,title);
    if(page==DEVICE_MUSIC_PREF_NETWORK) { pair(s,"Wi-Fi",detail?detail:"未连接",94);pair(s,"连接状态",s_connected?"已连接":"未连接",132);y=170; }
    if(page==DEVICE_MUSIC_PREF_ACCOUNT) {
        line(s,97,2,t->text);label(s,detail?detail:"网易云音乐账号",15,110,210,20,12,t->green,LV_TEXT_ALIGN_LEFT);
        label(s,"登录状态已保存",15,135,210,16,10,t->muted_text,LV_TEXT_ALIGN_LEFT);
        line(s,159,1,t->divider);y=172;
    }
    if(page==DEVICE_MUSIC_PREF_LOGOUT || page==DEVICE_MUSIC_PREF_RESET_WIFI) {
        bool logout=page==DEVICE_MUSIC_PREF_LOGOUT;
        label(s,logout?"退出后需重新扫码。\nWi-Fi 配置将保留。":"重启进入网络列表，\n在设备输入 Wi-Fi 密码。",15,96,210,41,12,t->text,LV_TEXT_ALIGN_LEFT);
        label(s,logout?"仅退出账号，不清除网络。":"保留网易云登录状态。",15,149,210,18,10,t->muted_text,LV_TEXT_ALIGN_LEFT);y=175;
    }
    if(page==DEVICE_MUSIC_PREF_ABOUT) {
        pair(s,"设备","FoloToy Passport",94);pair(s,"屏幕","240 × 320",132);
        pair(s,"音乐连接",detail?detail:"开源协议直连",170);pair(s,"固件版本",version?version:"未提供",208);
    }
    if(page==DEVICE_MUSIC_PREF_END) {
        label(s,"FIN.",15,94,210,53,43,t->netease_red,LV_TEXT_ALIGN_LEFT);
        label(s,"已到列表末尾。",15,153,210,18,10,t->muted_text,LV_TEXT_ALIGN_LEFT);y=180;
    }
    menu_rows(s,items,n,selected,y);
    const char *hint=page==DEVICE_MUSIC_PREF_LOGOUT || page==DEVICE_MUSIC_PREF_RESET_WIFI?"默认取消 · OK 确认":"长按 OK · 返回";
    if(page==DEVICE_MUSIC_PREF_SCREEN_TIMEOUT)hint="按任意键唤醒 · 音乐继续";
    folio(s,hint);
    if(page==DEVICE_MUSIC_PREF_APPEARANCE) {
        label(s,"07:00-19:00 日间，其余夜间",15,229,210,18,9,t->muted_text,LV_TEXT_ALIGN_LEFT);
        box(s,15,263,210,28,t->background,0);line(s,263,1,t->divider);
        label(s,detail?detail:"日夜跟随北京时间自动切换",15,271,210,14,9,t->muted_text,LV_TEXT_ALIGN_LEFT);
    }
    device_music_ui_footer_t footer={"上移",page==DEVICE_MUSIC_PREF_ABOUT?"返回":"确认","下移"};draw_footer(s,&footer);
}
void device_music_ui_update_volume_overlay(const device_music_ui_volume_widgets_t *w,int percent)
{
    if(!w)return;
    if(percent<0)percent=0;
    if(percent>100)percent=100;
    char value[16];snprintf(value,sizeof(value),"%d",percent);
    if(w->value_label)lv_label_set_text(w->value_label,value);
    if(w->progress_fill) {
        int width=device_music_ui_progress_width(210,percent);
        lv_obj_set_width(w->progress_fill,width?width:1);
        lv_obj_set_style_bg_opa(w->progress_fill,width?LV_OPA_COVER:LV_OPA_TRANSP,0);
    }
}
static void draw_control(lv_obj_t *s,int percent,bool brightness,device_music_ui_volume_widgets_t *w)
{
    device_music_ui_reset_screen(s,DEVICE_MUSIC_SCREEN_PLAYING);
    const device_music_ui_theme_t *t=device_music_ui_theme();eyeline(s,brightness?"DISPLAY / 背光":"AUDIO / 声音","");heading(s,brightness?"亮得刚刚好。":"听得刚刚好。");
    lv_obj_t *v=label(s,"",15,113,155,82,80,t->netease_red,LV_TEXT_ALIGN_LEFT);
    label(s,"%",174,161,30,27,24,t->muted_text,LV_TEXT_ALIGN_LEFT);line(s,203,3,t->divider);
    lv_obj_t *f=box(s,15,203,1,3,t->netease_red,0);
    label(s,"上键增加 / 下键减少\nOK 保存并返回",15,216,210,34,10,t->muted_text,LV_TEXT_ALIGN_LEFT);
    folio(s,brightness?"亮度范围 10-100":"音量范围 0-100");
    device_music_ui_footer_t footer={brightness?"亮度 +":"音量 +","完成",brightness?"亮度 −":"音量 −"};draw_footer(s,&footer);
    if(w) { w->value_label=v;w->progress_fill=f;device_music_ui_update_volume_overlay(w,percent); }
}
void device_music_ui_draw_volume_overlay(lv_obj_t *s,int percent,device_music_ui_volume_widgets_t *w) { draw_control(s,percent,false,w); }
void device_music_ui_draw_brightness(lv_obj_t *s,int percent) { device_music_ui_volume_widgets_t w={0};draw_control(s,percent,true,&w); }

void device_music_ui_set_footer(lv_obj_t *s,const char *up,const char *ok,const char *down)
{
    device_music_ui_footer_t footer={up,ok,down};draw_footer(s,&footer);
}

void device_music_ui_draw_notice(lv_obj_t *s, device_music_ui_notice_t notice, const char *detail)
{
    const device_music_ui_theme_t *t=device_music_ui_theme();
    if(notice==DEVICE_MUSIC_NOTICE_WELCOME) {
        eyeline(s,"FOLO / VOL. 01","");
        label(s,"Folo Music",15,100,210,46,24,t->text,LV_TEXT_ALIGN_LEFT);
        label(s,"把日常翻过一页，\n让音乐慢慢进来。",15,166,210,48,14,t->text,LV_TEXT_ALIGN_LEFT);
        line(s,225,2,t->text);
        label(s,"连接 Wi-Fi → 手机扫码 → 开始听歌",15,238,210,18,9,t->muted_text,LV_TEXT_ALIGN_LEFT);
        folio(s,"OK · 开始");device_music_ui_set_footer(s,"—","开始","—");return;
    }
    if(notice==DEVICE_MUSIC_NOTICE_RESTORE || notice==DEVICE_MUSIC_NOTICE_LIBRARY || notice==DEVICE_MUSIC_NOTICE_SIGNED_IN) {
        bool restore=notice==DEVICE_MUSIC_NOTICE_RESTORE, signed_in=notice==DEVICE_MUSIC_NOTICE_SIGNED_IN;
        eyeline(s,restore?"SESSION / 恢复":signed_in?"WELCOME BACK":"LIBRARY / 读取","");
        heading(s,restore?"接着上次，听。":signed_in?"Folo Music，准备好了。":"正在翻开目录。");
        label(s,signed_in?"你好。":"…",15,94,210,53,43,t->netease_red,LV_TEXT_ALIGN_LEFT);
        line(s,150,2,t->text);
        label(s,signed_in?(detail && detail[0]?detail:"网易云音乐账号"):restore?"正在验证已保存的登录状态":"读取个人歌单",15,164,210,22,12,signed_in?t->green:t->text,LV_TEXT_ALIGN_LEFT);
        line(s,197,1,t->divider);
        label(s,signed_in?"登录状态已保存":restore?"有效时直接进入歌单。\n过期时会提示重新扫码。":"正在获取创建与收藏的歌单。\n可长按 OK 打开导航。",15,210,210,37,10,t->muted_text,LV_TEXT_ALIGN_LEFT);
        folio(s,signed_in?"下次开机自动恢复":restore?"无需重复配网":"正在读取网易云歌单");
        device_music_ui_set_footer(s,"—",signed_in?"进入歌单":"等待","—");return;
    }
    const char *code="LOAD",*title="歌单没有读出来。",*body=detail?detail:"网络请求失败，请稍后重试。\n长按 OK 可返回歌单。",*action="重试读取";
    switch(notice) {
    case DEVICE_MUSIC_NOTICE_WIFI_FAILED:code="Wi-Fi";title="网络还没连上。";body="检查密码、信号及 2.4 GHz 网络。\n已有登录状态会保留。";action="重试连接";break;
    case DEVICE_MUSIC_NOTICE_EXPIRED:code="QR";title="这张码过期了。";body="重新获取二维码，\n再用网易云音乐 App 扫描。";action="获取新码";break;
    case DEVICE_MUSIC_NOTICE_SESSION:code="LOGIN";title="需要重新登录。";body="已保存的登录状态失效。\nWi-Fi 配置仍然保留。";action="重新扫码";break;
    case DEVICE_MUSIC_NOTICE_OFFLINE:code="OFFLINE";title="网络暂时断开。";body="OK 重试当前网络。\n↓ 网络设置，可更换 Wi-Fi。";action="尝试重连";break;
    case DEVICE_MUSIC_NOTICE_UNAVAILABLE:code="TRACK";title="这首暂时不能播。";body="可能受账号权限或资源限制。\n可在手机确认，或换下一首。";action="下一首";break;
    case DEVICE_MUSIC_NOTICE_PLAY_FAILED:code="AUDIO";title="这次播放失败。";body=detail?detail:"音频请求失败，请重试。\n长按 OK 可回列表换一首。";action="重试播放";break;
    default:break;
    }
    eyeline(s,"NOTICE / 状态","");label(s,code,15,58,210,53,43,t->netease_red,LV_TEXT_ALIGN_LEFT);
    label(s,title,15,114,210,33,24,t->text,LV_TEXT_ALIGN_LEFT);
    label(s,body,15,154,210,55,10,t->muted_text,LV_TEXT_ALIGN_LEFT);
    box(s,15,216,210,29,t->card,0);line(s,216,2,t->netease_red);
    char button[64];snprintf(button,sizeof(button),"OK · %s",action);
    label(s,button,24,224,192,18,12,t->netease_red,LV_TEXT_ALIGN_LEFT);
    folio(s,"长按 OK · 返回");
    device_music_ui_set_footer(s,"—",action,notice==DEVICE_MUSIC_NOTICE_WIFI_FAILED?"换网络":"网络设置");
}
