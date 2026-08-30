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
    assert(theme->background == 0x151821);
    assert(theme->top_bar == 0x101216);
    assert(theme->netease_red == 0xE60026);
    assert(theme->footer_y <= 268);
    assert(theme->footer_h >= 52);

    assert(device_music_ui_screen_count() == DEVICE_MUSIC_SCREEN_COUNT);
    for (int i = 0; i < DEVICE_MUSIC_SCREEN_COUNT; i++) {
        assert(has_screen((device_music_screen_t)i));
    }

    const device_music_ui_screen_spec_t *qr = device_music_ui_screen(DEVICE_MUSIC_SCREEN_QR_LOGIN);
    assert(strcmp(qr->app_title, "网易云音乐") == 0);
    assert(strcmp(qr->state_chip, "登录") == 0);
    assert(strcmp(qr->title, "扫码登录网易云") == 0);
    assert(qr->kind == DEVICE_MUSIC_SCREEN_KIND_QR);
    assert(qr->main.x == 52);
    assert(qr->main.y == 50);
    assert(qr->main.w == 136);
    assert(qr->main.h == 136);

    const device_music_ui_screen_spec_t *list = device_music_ui_screen(DEVICE_MUSIC_SCREEN_PLAYLISTS);
    assert(strcmp(list->title, "选择歌单") == 0);
    assert(list->kind == DEVICE_MUSIC_SCREEN_KIND_LIST);
    assert(list->row_count == 5);
    assert(list->rows.x == 12);
    assert(list->rows.y == 78);
    assert(list->rows.w == 216);
    assert(list->rows.h == 33);

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

    return 0;
}
