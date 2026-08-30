#pragma once

#include "device_music_ui_spec.h"
#include "lvgl.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *title;
    const char *sub;
    bool selected;
} device_music_ui_row_t;

typedef struct {
    lv_obj_t *progress_fill;
    lv_obj_t *elapsed_label;
    lv_obj_t *duration_label;
} device_music_ui_player_widgets_t;

typedef struct {
    lv_obj_t *value_label;
    lv_obj_t *progress_fill;
} device_music_ui_volume_widgets_t;

lv_obj_t *device_music_ui_create_screen(device_music_screen_t screen);
bool device_music_ui_reset_screen(lv_obj_t *screen, device_music_screen_t screen_id);
void device_music_ui_draw_setup(lv_obj_t *screen, const char *ap_ssid);
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
