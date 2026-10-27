#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    NETEASE_APP_LOGIN_QR = 0,
    NETEASE_APP_LOGIN_WAITING,
    NETEASE_APP_PLAYLISTS,
    NETEASE_APP_TRACKS,
    NETEASE_APP_PLAYING,
    NETEASE_APP_ERROR,
    NETEASE_APP_SIGNED_IN,
    NETEASE_APP_NAVIGATION,
    NETEASE_APP_SETTINGS,
    NETEASE_APP_APPEARANCE,
    NETEASE_APP_BRIGHTNESS,
    NETEASE_APP_NETWORK,
    NETEASE_APP_ACCOUNT,
    NETEASE_APP_LOGOUT_CONFIRM,
    NETEASE_APP_WIFI_CONFIRM,
    NETEASE_APP_ABOUT,
    NETEASE_APP_ENDED,
} netease_app_state_t;

typedef enum {
    NETEASE_APP_BTN_UP = 0,
    NETEASE_APP_BTN_DOWN,
    NETEASE_APP_BTN_OK,
} netease_app_button_t;

typedef enum {
    NETEASE_APP_BTN_CLICK = 0,
    NETEASE_APP_BTN_LONG,
} netease_app_button_event_t;

typedef enum {
    NETEASE_APP_ACTION_NONE = 0,
    NETEASE_APP_ACTION_START_QR,
    NETEASE_APP_ACTION_LOAD_TRACKS,
    NETEASE_APP_ACTION_LOAD_TRACK_URL,
    NETEASE_APP_ACTION_TOGGLE_PLAYBACK,
    NETEASE_APP_ACTION_VOLUME_UP,
    NETEASE_APP_ACTION_VOLUME_DOWN,
    NETEASE_APP_ACTION_SWITCH_ACCOUNT,
    NETEASE_APP_ACTION_RESET_CLOUD_SETUP,
    NETEASE_APP_ACTION_SET_DAY,
    NETEASE_APP_ACTION_SET_NIGHT,
    NETEASE_APP_ACTION_BRIGHTNESS_UP,
    NETEASE_APP_ACTION_BRIGHTNESS_DOWN,
    NETEASE_APP_ACTION_RETRY,
} netease_app_action_t;

typedef struct {
    netease_app_state_t state;
    size_t selected;
    size_t item_count;
    bool playing;
    bool volume_overlay;
    bool has_player;
    size_t playlist_cursor, playlist_count;
    size_t track_cursor, track_count, playing_index, playing_count;
    netease_app_state_t return_state, error_return_state;
} netease_app_logic_t;

void netease_app_logic_init(netease_app_logic_t *logic);
netease_app_state_t netease_app_logic_state(const netease_app_logic_t *logic);
size_t netease_app_logic_selected(const netease_app_logic_t *logic);
bool netease_app_logic_is_playing(const netease_app_logic_t *logic);
bool netease_app_logic_volume_overlay(const netease_app_logic_t *logic);
void netease_app_logic_set_playlists(netease_app_logic_t *logic, size_t count);
void netease_app_logic_set_tracks(netease_app_logic_t *logic, size_t count);
void netease_app_logic_set_playing(netease_app_logic_t *logic);
void netease_app_logic_set_error(netease_app_logic_t *logic);
netease_app_action_t netease_app_logic_on_button(netease_app_logic_t *logic,
                                                 netease_app_button_t button,
                                                 netease_app_button_event_t event);
netease_app_action_t netease_app_logic_dispatch_button(netease_app_logic_t *logic,
                                                       netease_app_button_t button,
                                                       netease_app_button_event_t event,
                                                       bool work_pending);

/* Choose an adjacent metadata page; no network or allocation in this helper. */
bool netease_app_prefetch_offset(uint32_t offset, size_t count, size_t selected,
                                 bool has_next, uint32_t next, uint32_t page_size,
                                 uint32_t *target);
