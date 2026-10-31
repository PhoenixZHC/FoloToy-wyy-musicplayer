#pragma once

#include "device_music_ui_spec.h"
#include "lvgl.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *title;
    const char *sub;
    bool selected;
    bool playing;
    unsigned number;
} device_music_ui_row_t;

typedef struct {
    lv_obj_t *progress_fill;
    lv_obj_t *elapsed_label;
    lv_obj_t *duration_label;
    lv_obj_t *lyric_label;
} device_music_ui_player_widgets_t;

typedef struct {
    lv_obj_t *value_label;
    lv_obj_t *progress_fill;
} device_music_ui_volume_widgets_t;

lv_obj_t *device_music_ui_create_screen(device_music_screen_t screen);
bool device_music_ui_reset_screen(lv_obj_t *screen, device_music_screen_t screen_id);
void device_music_ui_draw_setup(lv_obj_t *screen, const char *ap_ssid);
void device_music_ui_draw_wifi_scan(lv_obj_t *screen, const char *const *ssids,
                                    const bool *secure, size_t count, size_t selected,
                                    const char *detail);
void device_music_ui_draw_wifi_password(lv_obj_t *screen, const char *ssid,
                                         const char *password, const char *characters,
                                         size_t selected_character,
                                         bool showing, const char *group,
                                         const char *detail);
void device_music_ui_draw_message(lv_obj_t *screen,
                                  device_music_screen_t screen_id,
                                  const char *title,
                                  const char *detail);
void device_music_ui_draw_progress(lv_obj_t *screen,
                                   device_music_screen_t screen_id,
                                   const char *title,
                                   const char *detail,
                                   int active_steps);
void device_music_ui_draw_success(lv_obj_t *screen, const char *title, const char *detail);
void device_music_ui_draw_error(lv_obj_t *screen,
                                device_music_screen_t screen_id,
                                const char *title,
                                const char *detail);
lv_obj_t *device_music_ui_draw_qr(lv_obj_t *screen,
                                  device_music_screen_t screen_id,
                                  const char *title,
                                  const char *detail);
void device_music_ui_draw_confirm(lv_obj_t *screen,
                                  const char *title,
                                  const char *detail,
                                  lv_obj_t **qr_out);
void device_music_ui_draw_list(lv_obj_t *screen,
                               device_music_screen_t screen_id,
                               const char *heading,
                               const char *count,
                               const device_music_ui_row_t *rows,
                               size_t row_count);
void device_music_ui_draw_player(lv_obj_t *screen,
                                 const char *song,
                                 const char *artist,
                                 int progress_percent,
                                 int elapsed_ms,
                                 int duration_ms,
                                 bool paused,
                                 device_music_ui_player_widgets_t *widgets);
void device_music_ui_update_player_progress(const device_music_ui_player_widgets_t *widgets,
                                            int progress_percent,
                                            int elapsed_ms,
                                            int duration_ms);
void device_music_ui_draw_volume_overlay(lv_obj_t *screen,
                                         int volume_percent,
                                         device_music_ui_volume_widgets_t *widgets);
void device_music_ui_update_volume_overlay(const device_music_ui_volume_widgets_t *widgets,
                                           int volume_percent);

void device_music_ui_draw_menu(lv_obj_t *screen, const char *heading, const char *detail,
                               const char *const *items, size_t count, size_t selected);
void device_music_ui_draw_brightness(lv_obj_t *screen, int percent);
void device_music_ui_set_playback_context(unsigned number, const char *playlist, int volume);
void device_music_ui_set_network(bool connected, unsigned signal_level);
/* 调用方须持 LVGL 锁；仅更新状态条，未校时显示 --:--。 */
void device_music_ui_set_clock(int hour, int minute, bool valid);
/* Call while holding the UI lock; image data must outlive its widgets. */
void device_music_ui_set_media(const lv_image_dsc_t *image, const char *lyric);
void device_music_ui_update_lyric(const device_music_ui_player_widgets_t *widgets, const char *lyric);

typedef enum {
    DEVICE_MUSIC_PREF_SETTINGS,DEVICE_MUSIC_PREF_APPEARANCE,DEVICE_MUSIC_PREF_NETWORK,
    DEVICE_MUSIC_PREF_ACCOUNT,DEVICE_MUSIC_PREF_LOGOUT,DEVICE_MUSIC_PREF_RESET_WIFI,
    DEVICE_MUSIC_PREF_ABOUT,DEVICE_MUSIC_PREF_END,
    DEVICE_MUSIC_PREF_SCREEN_TIMEOUT,
} device_music_ui_preference_t;
void device_music_ui_draw_preferences(lv_obj_t *screen,device_music_ui_preference_t page,const char *detail,
                                      const char *version,const char *const *items,size_t count,size_t selected);
void device_music_ui_set_now_playing(const char *song,bool paused);

void device_music_ui_set_footer(lv_obj_t *screen,const char *up,const char *ok,const char *down);

void device_music_ui_set_buffering(bool buffering);

typedef enum {
    DEVICE_MUSIC_NOTICE_WELCOME, DEVICE_MUSIC_NOTICE_RESTORE,
    DEVICE_MUSIC_NOTICE_LIBRARY, DEVICE_MUSIC_NOTICE_SIGNED_IN,
    DEVICE_MUSIC_NOTICE_EXPIRED, DEVICE_MUSIC_NOTICE_SESSION,
    DEVICE_MUSIC_NOTICE_OFFLINE, DEVICE_MUSIC_NOTICE_UNAVAILABLE,
    DEVICE_MUSIC_NOTICE_LOAD_FAILED, DEVICE_MUSIC_NOTICE_PLAY_FAILED,
    DEVICE_MUSIC_NOTICE_WIFI_FAILED,
} device_music_ui_notice_t;
void device_music_ui_draw_notice(lv_obj_t *screen, device_music_ui_notice_t notice,
                                const char *detail);
