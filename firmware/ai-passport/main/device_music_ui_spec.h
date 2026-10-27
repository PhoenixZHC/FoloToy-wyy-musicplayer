#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    DEVICE_MUSIC_SCREEN_SETUP = 0,
    DEVICE_MUSIC_SCREEN_REGISTERING,
    DEVICE_MUSIC_SCREEN_READY,
    DEVICE_MUSIC_SCREEN_SETUP_FAILED,
    DEVICE_MUSIC_SCREEN_QR_LOGIN,
    DEVICE_MUSIC_SCREEN_LOGIN_WAITING,
    DEVICE_MUSIC_SCREEN_PLAYLISTS,
    DEVICE_MUSIC_SCREEN_TRACKS,
    DEVICE_MUSIC_SCREEN_LOADING_TRACK,
    DEVICE_MUSIC_SCREEN_PLAYING,
    DEVICE_MUSIC_SCREEN_PAUSED,
    DEVICE_MUSIC_SCREEN_PLAY_FAILED,
    DEVICE_MUSIC_SCREEN_COUNT,
} device_music_screen_t;

typedef enum {
    DEVICE_MUSIC_SCREEN_KIND_MESSAGE = 0,
    DEVICE_MUSIC_SCREEN_KIND_PROGRESS,
    DEVICE_MUSIC_SCREEN_KIND_SUCCESS,
    DEVICE_MUSIC_SCREEN_KIND_ERROR,
    DEVICE_MUSIC_SCREEN_KIND_QR,
    DEVICE_MUSIC_SCREEN_KIND_CONFIRM,
    DEVICE_MUSIC_SCREEN_KIND_LIST,
    DEVICE_MUSIC_SCREEN_KIND_PLAYER,
    DEVICE_MUSIC_SCREEN_KIND_PAUSED,
} device_music_screen_kind_t;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} device_music_ui_rect_t;

typedef struct {
    const char *up_label;
    const char *ok_label;
    const char *down_label;
} device_music_ui_footer_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t background;
    uint32_t top_bar;
    uint32_t footer_bar;
    uint32_t divider;
    uint32_t card;
    uint32_t error_card;
    uint32_t text;
    uint32_t muted_text;
    uint32_t subtle_text;
    uint32_t netease_red;
    uint32_t green;
    uint32_t blue;
    uint32_t amber;
    uint32_t danger;
    uint16_t footer_y;
    uint16_t footer_h;
} device_music_ui_theme_t;

typedef struct {
    device_music_screen_t screen;
    device_music_screen_kind_t kind;
    const char *app_title;
    const char *state_chip;
    uint32_t chip_color;
    const char *title;
    const char *detail;
    device_music_ui_rect_t main;
    device_music_ui_rect_t rows;
    uint8_t row_count;
    device_music_ui_footer_t footer;
} device_music_ui_screen_spec_t;

const device_music_ui_theme_t *device_music_ui_theme(void);
size_t device_music_ui_screen_count(void);
const device_music_ui_screen_spec_t *device_music_ui_screen(device_music_screen_t screen);
int device_music_ui_progress_percent(int elapsed_ms, int duration_ms);
int device_music_ui_progress_width(int track_width, int progress_percent);

unsigned device_music_ui_signal_level(bool connected, bool valid, int rssi_dbm);
void device_music_ui_set_night(bool night);
bool device_music_ui_is_night(void);
void device_music_ui_format_key_hint(char *out, size_t size, unsigned key, const char *action);
