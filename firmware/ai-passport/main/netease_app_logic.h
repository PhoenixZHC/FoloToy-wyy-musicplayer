#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    NETEASE_APP_LOGIN_QR = 0,
    NETEASE_APP_LOGIN_WAITING,
    NETEASE_APP_PLAYLISTS,
    NETEASE_APP_TRACKS,
    NETEASE_APP_PLAYING,
    NETEASE_APP_ERROR,
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
} netease_app_action_t;

typedef struct {
    netease_app_state_t state;
    size_t selected;
    size_t item_count;
    bool playing;
    bool volume_overlay;
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
