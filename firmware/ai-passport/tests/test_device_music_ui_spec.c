#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "device_music_ui_spec.h"

static bool has_screen(device_music_screen_t screen)
{
    return device_music_ui_screen(screen) != NULL;
}

int main(void)
{
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    assert(theme != NULL);
    assert(theme->width == 240);
    assert(theme->height == 320);
    assert(theme->background == 0xF0E9DA);
    assert(theme->top_bar == 0xF0E9DA);
    assert(theme->netease_red == 0x8E482F);
    assert(theme->footer_y == 293);
    assert(theme->footer_h == 27);

    assert(device_music_ui_screen_count() == DEVICE_MUSIC_SCREEN_COUNT);
    for (int i = 0; i < DEVICE_MUSIC_SCREEN_COUNT; i++) {
        assert(has_screen((device_music_screen_t)i));
    }

    const device_music_ui_screen_spec_t *qr = device_music_ui_screen(DEVICE_MUSIC_SCREEN_QR_LOGIN);
    assert(strcmp(qr->app_title, "网易云音乐") == 0);
    assert(strcmp(qr->state_chip, "登录") == 0);
    assert(strcmp(qr->title, "扫码，让音乐进来。") == 0);
    assert(qr->kind == DEVICE_MUSIC_SCREEN_KIND_QR);
    assert(qr->main.x == 54);
    assert(qr->main.y == 110);
    assert(qr->main.w == 132);
    assert(qr->main.h == 132);

    const device_music_ui_screen_spec_t *list = device_music_ui_screen(DEVICE_MUSIC_SCREEN_PLAYLISTS);
    assert(strcmp(list->title, "我的歌单") == 0);
    assert(list->kind == DEVICE_MUSIC_SCREEN_KIND_LIST);
    assert(list->row_count == 4);
    assert(list->rows.x == 15);
    assert(list->rows.y == 94);
    assert(list->rows.w == 210);
    assert(list->rows.h == 42);

    const device_music_ui_screen_spec_t *playing = device_music_ui_screen(DEVICE_MUSIC_SCREEN_PLAYING);
    assert(strcmp(playing->state_chip, "播放中") == 0);
    assert(playing->kind == DEVICE_MUSIC_SCREEN_KIND_PLAYER);
    assert(playing->main.x == 58);
    assert(playing->main.y == 78);
    assert(device_music_ui_progress_percent(0, 210000) == 0);
    assert(device_music_ui_progress_percent(105000, 210000) == 50);
    assert(device_music_ui_progress_percent(300000, 210000) == 100);
    assert(device_music_ui_progress_width(176, 50) == 88);
    assert(device_music_ui_progress_width(176, -1) == 0);
    assert(device_music_ui_progress_width(176, 150) == 176);

    const device_music_ui_screen_spec_t *setup = device_music_ui_screen(DEVICE_MUSIC_SCREEN_SETUP);
    assert(strcmp(setup->footer.up_label, "无") == 0);
    assert(strcmp(setup->footer.ok_label, "确认") == 0);
    assert(strcmp(setup->footer.down_label, "无") == 0);
    assert(strstr(setup->detail, "云服务") == NULL);

    const device_music_ui_screen_spec_t *ready = device_music_ui_screen(DEVICE_MUSIC_SCREEN_READY);
    assert(strstr(ready->title, "云端") == NULL);

    device_music_ui_set_night(true);
    assert(device_music_ui_theme()->background==0x29251F);
    assert(device_music_ui_theme()->text==0xE7DDC9);
    device_music_ui_set_night(false);
    char hint[64];
    device_music_ui_format_key_hint(hint,sizeof(hint),0,"上一首");
    assert(strcmp(hint,"↑ 上一首")==0);
    device_music_ui_format_key_hint(hint,sizeof(hint),2,"下一首");
    assert(strcmp(hint,"↓ 下一首")==0);
    device_music_ui_format_key_hint(hint,sizeof(hint),1,"完成");
    assert(strcmp(hint,"OK 完成")==0);
    device_music_ui_format_key_hint(hint,sizeof(hint),0,"无");
    assert(strcmp(hint,"—")==0);
    device_music_ui_format_key_hint(hint,sizeof(hint),2,NULL);
    assert(strcmp(hint,"—")==0);
    assert(device_music_ui_signal_level(false,true,-40)==0);
    assert(device_music_ui_signal_level(true,false,0)==0);
    assert(device_music_ui_signal_level(true,true,-60)==3);
    assert(device_music_ui_signal_level(true,true,-61)==2);
    assert(device_music_ui_signal_level(true,true,-75)==2);
    assert(device_music_ui_signal_level(true,true,-76)==1);
    return 0;
}
