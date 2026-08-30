#include "netease_app_logic.h"

#include <string.h>

void netease_app_logic_init(netease_app_logic_t *logic)
{
    if (logic == NULL) return;
    memset(logic, 0, sizeof(*logic));
    logic->state = NETEASE_APP_LOGIN_QR;
}

netease_app_state_t netease_app_logic_state(const netease_app_logic_t *logic)
{
    return logic == NULL ? NETEASE_APP_ERROR : logic->state;
}

size_t netease_app_logic_selected(const netease_app_logic_t *logic)
{
    return logic == NULL ? 0 : logic->selected;
}

bool netease_app_logic_is_playing(const netease_app_logic_t *logic)
{
    return logic != NULL && logic->playing;
}

bool netease_app_logic_volume_overlay(const netease_app_logic_t *logic)
{
    return logic != NULL && logic->volume_overlay;
}

void netease_app_logic_set_playlists(netease_app_logic_t *logic, size_t count)
{
    if (logic == NULL) return;
    logic->state = count == 0 ? NETEASE_APP_ERROR : NETEASE_APP_PLAYLISTS;
    logic->selected = 0;
    logic->item_count = count;
    logic->playing = false;
    logic->volume_overlay = false;
}

void netease_app_logic_set_tracks(netease_app_logic_t *logic, size_t count)
{
    if (logic == NULL) return;
    logic->state = count == 0 ? NETEASE_APP_ERROR : NETEASE_APP_TRACKS;
    logic->selected = 0;
    logic->item_count = count;
    logic->playing = false;
    logic->volume_overlay = false;
}

void netease_app_logic_set_playing(netease_app_logic_t *logic)
{
    if (logic == NULL) return;
    if (logic->state != NETEASE_APP_TRACKS && logic->state != NETEASE_APP_PLAYING) return;
    logic->state = NETEASE_APP_PLAYING;
    logic->playing = true;
}

void netease_app_logic_set_error(netease_app_logic_t *logic)
{
    if (logic == NULL) return;
    logic->state = NETEASE_APP_ERROR;
    logic->playing = false;
    logic->volume_overlay = false;
}

static void move_selection(netease_app_logic_t *logic, int delta)
{
    if (logic->item_count == 0) return;
    if (delta < 0) {
        logic->selected = (logic->selected + logic->item_count - 1) % logic->item_count;
    } else {
        logic->selected = (logic->selected + 1) % logic->item_count;
    }
}

static bool setup_reset_requested(const netease_app_logic_t *logic,
                                  netease_app_button_t button,
                                  netease_app_button_event_t event)
{
    if (logic == NULL || event != NETEASE_APP_BTN_LONG || button != NETEASE_APP_BTN_OK) {
        return false;
    }
    return logic->state == NETEASE_APP_LOGIN_QR ||
           logic->state == NETEASE_APP_LOGIN_WAITING ||
           logic->state == NETEASE_APP_ERROR;
}

netease_app_action_t netease_app_logic_on_button(netease_app_logic_t *logic,
                                                 netease_app_button_t button,
                                                 netease_app_button_event_t event)
{
    if (logic == NULL) return NETEASE_APP_ACTION_NONE;

    if (logic->volume_overlay) {
        if (button == NETEASE_APP_BTN_OK) {
            logic->volume_overlay = false;
            return NETEASE_APP_ACTION_NONE;
        }
        if (button == NETEASE_APP_BTN_UP) {
            return NETEASE_APP_ACTION_VOLUME_UP;
        }
        if (button == NETEASE_APP_BTN_DOWN) {
            return NETEASE_APP_ACTION_VOLUME_DOWN;
        }
        return NETEASE_APP_ACTION_NONE;
    }

    if (event == NETEASE_APP_BTN_LONG && button == NETEASE_APP_BTN_OK) {
        if (setup_reset_requested(logic, button, event)) {
            return NETEASE_APP_ACTION_RESET_CLOUD_SETUP;
        } else if (logic->state == NETEASE_APP_PLAYING) {
            logic->volume_overlay = false;
            logic->state = NETEASE_APP_TRACKS;
            logic->playing = false;
        } else if (logic->state == NETEASE_APP_TRACKS) {
            logic->state = NETEASE_APP_PLAYLISTS;
            logic->selected = 0;
        } else if (logic->state == NETEASE_APP_PLAYLISTS) {
            logic->state = NETEASE_APP_LOGIN_QR;
            logic->selected = 0;
            logic->item_count = 0;
            logic->playing = false;
            return NETEASE_APP_ACTION_SWITCH_ACCOUNT;
        }
        return NETEASE_APP_ACTION_NONE;
    }

    if (logic->state == NETEASE_APP_PLAYING) {
        if (event == NETEASE_APP_BTN_LONG && button == NETEASE_APP_BTN_UP) {
            logic->volume_overlay = true;
            return NETEASE_APP_ACTION_VOLUME_UP;
        }
        if (event == NETEASE_APP_BTN_LONG && button == NETEASE_APP_BTN_DOWN) {
            logic->volume_overlay = true;
            return NETEASE_APP_ACTION_VOLUME_DOWN;
        }
        if (event == NETEASE_APP_BTN_CLICK &&
            (button == NETEASE_APP_BTN_UP || button == NETEASE_APP_BTN_DOWN)) {
            move_selection(logic, button == NETEASE_APP_BTN_UP ? -1 : 1);
            logic->playing = true;
            return NETEASE_APP_ACTION_LOAD_TRACK_URL;
        }
    }

    if (event != NETEASE_APP_BTN_CLICK) return NETEASE_APP_ACTION_NONE;

    if (logic->state == NETEASE_APP_PLAYLISTS || logic->state == NETEASE_APP_TRACKS) {
        if (button == NETEASE_APP_BTN_UP) {
            move_selection(logic, -1);
            return NETEASE_APP_ACTION_NONE;
        }
        if (button == NETEASE_APP_BTN_DOWN) {
            move_selection(logic, 1);
            return NETEASE_APP_ACTION_NONE;
        }
    }

    if (button != NETEASE_APP_BTN_OK) return NETEASE_APP_ACTION_NONE;

    switch (logic->state) {
    case NETEASE_APP_LOGIN_QR:
    case NETEASE_APP_ERROR:
        logic->state = NETEASE_APP_LOGIN_QR;
        return NETEASE_APP_ACTION_START_QR;
    case NETEASE_APP_PLAYLISTS:
        return NETEASE_APP_ACTION_LOAD_TRACKS;
    case NETEASE_APP_TRACKS:
        return NETEASE_APP_ACTION_LOAD_TRACK_URL;
    case NETEASE_APP_PLAYING:
        logic->playing = !logic->playing;
        return NETEASE_APP_ACTION_TOGGLE_PLAYBACK;
    default:
        return NETEASE_APP_ACTION_NONE;
    }
}

netease_app_action_t netease_app_logic_dispatch_button(netease_app_logic_t *logic,
                                                       netease_app_button_t button,
                                                       netease_app_button_event_t event,
                                                       bool work_pending)
{
    if (setup_reset_requested(logic, button, event)) {
        return NETEASE_APP_ACTION_RESET_CLOUD_SETUP;
    }
    if (work_pending) {
        return NETEASE_APP_ACTION_NONE;
    }
    return netease_app_logic_on_button(logic, button, event);
}
