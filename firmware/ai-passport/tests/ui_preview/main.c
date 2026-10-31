#include "device_music_ui.h"
#include "src/misc/lv_text_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static lv_obj_t *find_label(lv_obj_t *parent, const char *text)
{
    for(uint32_t i=0;i<lv_obj_get_child_count(parent);i++) {
        lv_obj_t *child=lv_obj_get_child(parent,i);
        if(lv_obj_check_type(child,&lv_label_class) && !strcmp(lv_label_get_text(child),text))return child;
    }
    return NULL;
}
static unsigned char rgb[240*320*3];
int bsp_battery_soc(void) { return 82; }
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels)
{
    const uint16_t *p=(const uint16_t *)pixels;
    for(int y=area->y1;y<=area->y2;y++)for(int x=area->x1;x<=area->x2;x++) {
        uint16_t c=*p++;unsigned i=(y*240+x)*3;
        rgb[i]=((c>>11)&31)*255/31;rgb[i+1]=((c>>5)&63)*255/63;rgb[i+2]=(c&31)*255/31;
    }
    lv_display_flush_ready(display);
}
static unsigned missing_glyphs(lv_obj_t *obj)
{
    unsigned missing=0;
    if(lv_obj_check_type(obj,&lv_label_class)) {
        const char *text=lv_label_get_text(obj);const lv_font_t *font=lv_obj_get_style_text_font(obj,0);
        for(uint32_t i=0;text[i];) {
            uint32_t letter=lv_text_encoded_next(text,&i);
            if(letter=='\n' || letter=='\r')continue;
            lv_font_glyph_dsc_t glyph;
            if(!lv_font_get_glyph_dsc(font,&glyph,letter,0) || glyph.is_placeholder) {
                fprintf(stderr,"Missing glyph U+%04X in %s\n",letter,text);missing++;
            }
        }
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(obj);i++)missing+=missing_glyphs(lv_obj_get_child(obj,i));
    return missing;
}
int main(int argc,char **argv)
{
    if(argc<4)return 2;
    device_music_ui_set_now_playing("城市灯光",false);
    lv_init();lv_display_t *display=lv_display_create(240,320);
    static uint16_t buffer[240*32];lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    device_music_ui_set_night(strcmp(argv[2],"night")==0);device_music_ui_set_network(strcmp(argv[1],"offline")!=0 && strcmp(argv[1],"wififail")!=0,3);
    device_music_screen_t id=strcmp(argv[1],"qr")==0?DEVICE_MUSIC_SCREEN_QR_LOGIN:strcmp(argv[1],"scanned")==0?DEVICE_MUSIC_SCREEN_LOGIN_WAITING:strcmp(argv[1],"loading")==0?DEVICE_MUSIC_SCREEN_LOADING_TRACK:strcmp(argv[1],"error")==0?DEVICE_MUSIC_SCREEN_PLAY_FAILED:strcmp(argv[1],"list")==0?DEVICE_MUSIC_SCREEN_TRACKS:strcmp(argv[1],"paused")==0?DEVICE_MUSIC_SCREEN_PAUSED:DEVICE_MUSIC_SCREEN_PLAYING;
    lv_obj_t *screen=device_music_ui_create_screen(id);
    if(!strcmp(argv[1],"appearance")) {
        device_music_ui_set_clock(23,59,true);assert(find_label(screen,"23:59"));
        device_music_ui_set_clock(0,0,true);assert(find_label(screen,"00:00"));
        assert(device_music_ui_reset_screen(screen,id));
        assert(find_label(screen,"00:00"));
        device_music_ui_set_clock(0,1,true);assert(find_label(screen,"00:01"));
        lv_obj_delete(screen);
        device_music_ui_set_clock(0,2,true);
        screen=device_music_ui_create_screen(id);assert(find_label(screen,"00:02"));
        lv_obj_t *previous=screen;
        screen=device_music_ui_create_screen(id);
        lv_obj_delete(previous);
        device_music_ui_set_clock(0,3,true);assert(find_label(screen,"00:03"));
    }
    device_music_ui_set_clock(16,42,strcmp(argv[1],"clockwaiting")!=0);
    const char *const notices[]={"welcome","restore","library","signedin","expired","session","offline","unavailable","loadfail","playfail"};
    int notice=-1;for(size_t i=0;i<sizeof(notices)/sizeof(notices[0]);i++)if(strcmp(argv[1],notices[i])==0)notice=(int)i;
    if(notice>=0)device_music_ui_draw_notice(screen,(device_music_ui_notice_t)notice,notice==DEVICE_MUSIC_NOTICE_SIGNED_IN?"预览听众":NULL);
    else if(strcmp(argv[1],"player")==0 || strcmp(argv[1],"paused")==0 || strcmp(argv[1],"buffering")==0 || strcmp(argv[1],"longtitle")==0 || strcmp(argv[1],"longartist")==0 || strcmp(argv[1],"artwork")==0 || strcmp(argv[1],"lyrics")==0 || strcmp(argv[1],"longlyrics")==0 || strcmp(argv[1],"expandedfont")==0) {
        device_music_ui_set_playback_context(1,"夜行电台",55);
        device_music_ui_set_buffering(strcmp(argv[1],"buffering")==0);
        const char *song=!strcmp(argv[1],"longtitle")?"雨后的街角与那些没有说完的话":"城市灯光";
        const char *artist=!strcmp(argv[1],"longartist")?"演示歌手 / 演示乐队 / 夜行专辑 / 城市灯光 / 沿海公路":"演示歌手 / 夜行专辑";
        bool expanded=!strcmp(argv[1],"expandedfont");
        if(expanded) {
            song="祎喆的音乐";artist="Folo Music / The Beatles";
            device_music_ui_set_playback_context(1,"简体音乐 · 每日精选",55);
        }
        static uint16_t cover_pixels[60*60];
        static lv_image_dsc_t cover;
        bool has_cover=!strcmp(argv[1],"artwork");
        if(has_cover) {
            for(unsigned i=0;i<60*60;i++)cover_pixels[i]=0xa329;
            cover.header.magic=LV_IMAGE_HEADER_MAGIC;cover.header.cf=LV_COLOR_FORMAT_RGB565;
            cover.header.w=60;cover.header.h=60;cover.header.stride=120;
            cover.data=(const uint8_t *)cover_pixels;cover.data_size=sizeof(cover_pixels);
        }
        const char *lyric=!strcmp(argv[1],"longlyrics")?"雨后的街角与那些没有说完的话，沿海公路上晚风经过":!strcmp(argv[1],"lyrics")?"让音乐慢慢进来":NULL;
        if(expanded)lyric="听音乐，看见每一个简体字\nABCDEFGHIJKLMNOPQRSTUVWXYZ";
        device_music_ui_set_media(has_cover?&cover:NULL,lyric);
        device_music_ui_player_widgets_t widgets={0};
        device_music_ui_draw_player(screen,song,artist,strcmp(argv[1],"buffering")==0?0:32,strcmp(argv[1],"buffering")==0?0:72000,222000,strcmp(argv[1],"paused")==0,&widgets);
        lv_obj_update_layout(screen);
        lv_obj_t *title=find_label(screen,song),*singer=find_label(screen,artist);
        assert(title && singer);
        assert(lv_obj_get_width(title)==(has_cover?140:210));
        assert(lv_obj_get_width(singer)==(has_cover?140:210));
        assert((lv_label_get_long_mode(title)==LV_LABEL_LONG_MODE_SCROLL)==!strcmp(argv[1],"longtitle"));
        assert((lv_label_get_long_mode(singer)==LV_LABEL_LONG_MODE_SCROLL)==!strcmp(argv[1],"longartist"));
        unsigned images=0;
        for(uint32_t i=0;i<lv_obj_get_child_count(screen);i++)images+=lv_obj_check_type(lv_obj_get_child(screen,i),&lv_image_class);
        assert(images==(unsigned)has_cover);
        assert(lv_obj_get_height(widgets.lyric_label)==42);
        assert(lv_obj_get_y(widgets.lyric_label)==199);
        assert(lv_obj_get_style_text_font(widgets.lyric_label,0)->line_height==17);
        assert(lv_obj_get_y(widgets.lyric_label)+lv_obj_get_height(widgets.lyric_label)<lv_obj_get_y(widgets.progress_fill));
        if(lyric) {
            device_music_ui_update_lyric(&widgets,"晚风经过，音乐慢慢进来");
            assert(!strcmp(lv_label_get_text(widgets.lyric_label),"晚风经过，音乐慢慢进来"));
            device_music_ui_update_lyric(&widgets,lyric);
        }
        if(strcmp(argv[1],"buffering")==0)device_music_ui_set_footer(screen,"—","等待","—");
    } else if(strcmp(argv[1],"list")==0 || strcmp(argv[1],"expandedlist")==0) {
        device_music_ui_row_t rows[]={
            {.title="晚风经过",.sub="演示歌手 / 风的方向",.selected=true,.number=1},
            {.title="慢慢来",.sub="演示歌手 / 周末录音",.number=2},
            {.title="沿海公路",.sub="演示乐队 / 出发",.number=3},
            {.title="雨后的街角与那些没有说完的话",.sub="演示乐队 / 午后",.number=4,.playing=true}};
        if(!strcmp(argv[1],"expandedlist")) {
            rows[0].title="祎喆珺昀 · 简体歌名";rows[0].sub="ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            rows[1].title="晞灏玥垚 · 音乐时光";rows[1].sub="abcdefghijklmnopqrstuvwxyz";
            device_music_ui_set_now_playing("祎喆 · Folo Music 0123456789",false);
        }
        device_music_ui_draw_list(screen,DEVICE_MUSIC_SCREEN_TRACKS,"夜行电台","1 / 15",rows,4);
    } else if(strcmp(argv[1],"menu")==0) {
        const char *const items[]={"返回播放","当前歌曲列表","我的歌单","音量","设置"};
        device_music_ui_draw_menu(screen,"翻到哪一页？",NULL,items,5,2);
    } else if(strcmp(argv[1],"volume")==0) { device_music_ui_volume_widgets_t w;device_music_ui_draw_volume_overlay(screen,55,&w); }
    else if(strcmp(argv[1],"scanned")==0) { device_music_ui_draw_confirm(screen,NULL,NULL,NULL);device_music_ui_set_footer(screen,"—","等待确认","—"); }
    else if(strcmp(argv[1],"settings")==0 || strcmp(argv[1],"network")==0 || strcmp(argv[1],"logout")==0) {
        const char *const settings[]={"显示与日夜","网络","账号","设备信息"};
        const char *const network[]={"重新配网","返回设置"};
        const char *const logout[]={"取消，保留登录","确认退出账号"};
        bool net=strcmp(argv[1],"network")==0,confirm=strcmp(argv[1],"logout")==0;
        device_music_ui_draw_preferences(screen,net?DEVICE_MUSIC_PREF_NETWORK:confirm?DEVICE_MUSIC_PREF_LOGOUT:DEVICE_MUSIC_PREF_SETTINGS,"Home Wi-Fi","preview",net?network:confirm?logout:settings,net||confirm?2:4,0);
        if(!net && !confirm) {
            assert(find_label(screen,"显示与日夜"));
            assert(!find_label(screen,"熄屏"));
        }
    }
    else if(strcmp(argv[1],"timeout")==0 || strcmp(argv[1],"timeoutnever")==0 || strcmp(argv[1],"timeoutback")==0) {
        const char *const items[]={"永不","15 秒","30 秒","1 分钟 · 当前","2 分钟","5 分钟","返回显示"};
        size_t selected=!strcmp(argv[1],"timeoutnever")?0:!strcmp(argv[1],"timeoutback")?6:3;
        device_music_ui_draw_preferences(screen,DEVICE_MUSIC_PREF_SCREEN_TIMEOUT,NULL,"preview",items,7,selected);
        assert(find_label(screen,items[selected]));
        assert(find_label(screen,"按任意键唤醒 · 音乐继续"));
    }
    else if(strcmp(argv[1],"wifi")==0 || strcmp(argv[1],"wifiempty")==0) {
        const char *names[]={"Home Wi-Fi", "客厅无线网络", "Studio-Guest", "Music Room", "第五个网络"};
        const bool secure[]={true,true,false,true,true};
        bool empty=!strcmp(argv[1],"wifiempty");
        device_music_ui_draw_wifi_scan(screen,names,secure,empty?0:5,empty?0:4,empty?"未找到网络，长按 OK 重试":"仅支持 2.4 GHz 网络");
    }
    else if(strcmp(argv[1],"wifipassword")==0 || strcmp(argv[1],"wifivisible")==0 || strcmp(argv[1],"wifiinvalid")==0 || strncmp(argv[1],"wifikey",7)==0) {
        const char *chars="!@#$%^&*()-_=+[]{};:\",.<>?/\\|`~' ";
        const char *group="符号与空格";size_t selected=9;
        if(strcmp(argv[1],"wifikeylower")==0){chars="qwertyuiopasdfghjklzxcvbnm";group="小写字母";selected=12;}
        if(strcmp(argv[1],"wifikeyupper")==0){chars="QWERTYUIOPASDFGHJKLZXCVBNM";group="大写字母";selected=12;}
        if(strcmp(argv[1],"wifikeynumber")==0){chars="1234567890";group="数字";selected=4;}
        device_music_ui_draw_wifi_password(screen,"客厅无线网络","Example Password 123!",chars,selected,!strcmp(argv[1],"wifivisible"),group,!strcmp(argv[1],"wifiinvalid")?"密码需 8–63 字符或 64 位十六进制":"");
    }
    else if(strcmp(argv[1],"wifiactions")==0) {
        const char *items[]={"连接此网络","删除末尾字符","显示 / 隐藏密码","继续输入","返回网络列表"};
        device_music_ui_draw_menu(screen,"密码操作。",NULL,items,5,4);
    }
    else if(strcmp(argv[1],"connecting")==0)device_music_ui_draw_progress(screen,DEVICE_MUSIC_SCREEN_REGISTERING,NULL,NULL,1);
    else if(strcmp(argv[1],"wififail")==0)device_music_ui_draw_error(screen,DEVICE_MUSIC_SCREEN_SETUP_FAILED,"网络还没连上。","检查密码、信号及 2.4 GHz 网络。\n已有登录状态会保留。");
    else if(strcmp(argv[1],"playlists")==0) {
        const device_music_ui_row_t rows[]={{"夜行电台","32 首",true,false,1},{"散步的时候","18 首",false,false,2},{"周末慢放","26 首",false,false,3},{"在路上","41 首",false,false,4}};
        device_music_ui_draw_list(screen,DEVICE_MUSIC_SCREEN_PLAYLISTS,"我的歌单","1 / 5",rows,4);
        device_music_ui_set_footer(screen,"上移","打开","下移");
    }
    else if(strcmp(argv[1],"emptylibrary")==0) {device_music_ui_draw_list(screen,DEVICE_MUSIC_SCREEN_PLAYLISTS,NULL,NULL,NULL,0);device_music_ui_set_footer(screen,"—","刷新","—");}
    else if(strcmp(argv[1],"end")==0 || strcmp(argv[1],"appearance")==0 || strcmp(argv[1],"clockwaiting")==0 || strcmp(argv[1],"account")==0 || strcmp(argv[1],"resetwifi")==0 || strcmp(argv[1],"about")==0) {
        const char *const appearance[]={"背光亮度","熄屏","返回设置"};
        const char *const end[]={"从头播放","返回歌曲列表"};
        const char *const account[]={"退出当前账号","返回设置"};
        const char *const reset[]={"取消，保留网络","确认重新配网"};
        bool a=strcmp(argv[1],"appearance")==0 || strcmp(argv[1],"clockwaiting")==0,e=strcmp(argv[1],"end")==0,r=strcmp(argv[1],"resetwifi")==0,b=strcmp(argv[1],"about")==0;
        const char *clock=!strcmp(argv[1],"clockwaiting")?"等待联网校时 · 暂用上次配色":device_music_ui_is_night()?"北京时间 · 自动夜间":"北京时间 · 自动日间";
        device_music_ui_draw_preferences(screen,a?DEVICE_MUSIC_PREF_APPEARANCE:e?DEVICE_MUSIC_PREF_END:r?DEVICE_MUSIC_PREF_RESET_WIFI:b?DEVICE_MUSIC_PREF_ABOUT:DEVICE_MUSIC_PREF_ACCOUNT,a?clock:b?"开源协议直连":"预览听众","2.1",a?appearance:e?end:r?reset:b?NULL:account,b?0:a?3:2,a?1:0);
        if(a) { assert(find_label(screen,"熄屏"));assert(find_label(screen,clock)); }
    }
    else if(strcmp(argv[1],"empty")==0) { device_music_ui_draw_list(screen,DEVICE_MUSIC_SCREEN_TRACKS,"夜行电台","0 / 0",NULL,0);device_music_ui_set_footer(screen,"—","返回歌单","—"); }
    else if(strcmp(argv[1],"brightness")==0) { device_music_ui_draw_brightness(screen,45); }
    else if(strcmp(argv[1],"loading")==0) { device_music_ui_draw_progress(screen,DEVICE_MUSIC_SCREEN_LOADING_TRACK,"夜行电台","读取歌曲编号\n随后获取歌名与歌手",1); }
    else if(strcmp(argv[1],"qr")==0) {
        lv_obj_t *qr=device_music_ui_draw_qr(screen,id,NULL,NULL);
        const char *payload="https://music.163.com/login?codekey=preview-only";
        lv_qrcode_update(qr,payload,strlen(payload));
    } else device_music_ui_draw_error(screen,DEVICE_MUSIC_SCREEN_PLAY_FAILED,"读取歌曲失败","连接超时，请检查网络\nOK 重试，长按返回");
    if(!strcmp(argv[1],"player") || !strcmp(argv[1],"paused") || !strcmp(argv[1],"playlists") || !strcmp(argv[1],"list")) {
        assert(find_label(screen,"FOLO MUSIC")==NULL);
        assert(find_label(screen,"顺序播放")==NULL);
    }
    if(!strcmp(argv[1],"about"))assert(find_label(screen,"2.1")!=NULL);
    lv_obj_update_layout(screen);
    lv_obj_t *clock=find_label(screen,strcmp(argv[1],"clockwaiting")==0?"--:--":"16:42");
    assert(clock && lv_obj_get_x(clock)+lv_obj_get_width(clock)/2==120);
    const lv_font_t *status_font=lv_obj_get_style_text_font(clock,0);
    for(uint32_t i=0;i<lv_obj_get_child_count(screen);i++) {
        lv_obj_t *child=lv_obj_get_child(screen,i);
        if(lv_obj_check_type(child,&lv_label_class) && lv_obj_get_y(child)<24) {
            assert(lv_obj_get_style_text_font(child,0)==status_font);
            assert(lv_obj_get_y(child)==lv_obj_get_y(clock));
            assert(lv_obj_get_height(child)==lv_obj_get_height(clock));
        }
    }
    lv_obj_t *wifi=find_label(screen,"WiFi");
    if(wifi) {
        const lv_font_t *font=lv_obj_get_style_text_font(wifi,0);
        int bottom=lv_obj_get_y(wifi)+font->line_height-font->base_line;
        unsigned bars=0;
        for(uint32_t i=0;i<lv_obj_get_child_count(screen);i++) {
            lv_obj_t *child=lv_obj_get_child(screen,i);
            if(lv_obj_get_width(child)==3 && lv_obj_get_x(child)>=177 && lv_obj_get_x(child)<=185 && lv_obj_get_y(child)<24) {
                assert(lv_obj_get_y(child)+lv_obj_get_height(child)==bottom);bars++;
            }
        }
        assert(bars==3);
    }
    if(missing_glyphs(screen))return 4;
    lv_screen_load(screen);lv_refr_now(display);
    FILE *f=fopen(argv[3],"wb");if(!f)return 3;
    fprintf(f,"P6\n240 320\n255\n");fwrite(rgb,1,sizeof(rgb),f);fclose(f);
    lv_deinit();return 0;
}
