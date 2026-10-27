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
    logic->state = NETEASE_APP_PLAYLISTS;
    logic->playlist_count = count;
    if (logic->playlist_cursor >= count) logic->playlist_cursor = 0;
    logic->selected = logic->playlist_cursor;
    logic->item_count = count;
    /* Browsing preserves audio state. */
    logic->volume_overlay = false;
}

void netease_app_logic_set_tracks(netease_app_logic_t *logic, size_t count)
{
    if (logic == NULL) return;
    logic->state = NETEASE_APP_TRACKS;
    logic->track_count = count;
    if (logic->track_cursor >= count) logic->track_cursor = 0;
    logic->selected = logic->track_cursor;
    logic->item_count = count;
    /* Browsing preserves audio state. */
    logic->volume_overlay = false;
}

void netease_app_logic_set_playing(netease_app_logic_t *logic)
{
    if (logic == NULL) return;
    if (logic->state != NETEASE_APP_TRACKS && logic->state != NETEASE_APP_PLAYING) return;
    logic->track_cursor = logic->selected;
    logic->state = NETEASE_APP_PLAYING;
    logic->playing_index = logic->selected;
    logic->has_player = true;
    logic->playing_count = logic->track_count;
    logic->playing = true;
}

void netease_app_logic_set_error(netease_app_logic_t *logic)
{
    if (logic == NULL) return;
    if(logic->state!=NETEASE_APP_ERROR)logic->error_return_state = logic->state;
    logic->state = NETEASE_APP_ERROR;
    logic->volume_overlay = false;
}


static void view(netease_app_logic_t *l, netease_app_state_t state)
{
    l->state=state; l->volume_overlay=false;
    if(state==NETEASE_APP_PLAYLISTS) { l->selected=l->playlist_cursor; l->item_count=l->playlist_count; }
    else if(state==NETEASE_APP_TRACKS) { l->selected=l->track_cursor; l->item_count=l->track_count; }
    else if(state==NETEASE_APP_PLAYING) { l->selected=l->playing_index; l->item_count=l->playing_count; }
    else { l->selected=0; l->item_count=0; }
}
static void move_selection(netease_app_logic_t *l, int delta, size_t count)
{
    if(!count)return;
    int pos=(int)l->selected+delta;
    if(pos<0)pos=0;
    if((size_t)pos>=count)pos=(int)count-1;
    l->selected=(size_t)pos;
    if(l->state==NETEASE_APP_PLAYLISTS)l->playlist_cursor=l->selected;
    if(l->state==NETEASE_APP_TRACKS)l->track_cursor=l->selected;
}
static size_t menu_count(netease_app_state_t s)
{
    switch(s) {
    case NETEASE_APP_NAVIGATION:return 5;
    case NETEASE_APP_SETTINGS:case NETEASE_APP_APPEARANCE:return 4;
    case NETEASE_APP_NETWORK:case NETEASE_APP_ACCOUNT:case NETEASE_APP_LOGOUT_CONFIRM:case NETEASE_APP_WIFI_CONFIRM:case NETEASE_APP_ENDED:return 2;
    default:return 0;
    }
}
netease_app_action_t netease_app_logic_on_button(netease_app_logic_t *l, netease_app_button_t b, netease_app_button_event_t e)
{
    if(!l)return NETEASE_APP_ACTION_NONE;
    if(l->volume_overlay) {
        if(b==NETEASE_APP_BTN_OK) { view(l,l->return_state);return NETEASE_APP_ACTION_NONE; }
        return b==NETEASE_APP_BTN_UP?NETEASE_APP_ACTION_VOLUME_UP:NETEASE_APP_ACTION_VOLUME_DOWN;
    }
    if(e==NETEASE_APP_BTN_LONG && b==NETEASE_APP_BTN_OK) {
        switch(l->state) {
        case NETEASE_APP_NAVIGATION:case NETEASE_APP_SETTINGS:view(l,l->return_state);break;
        case NETEASE_APP_APPEARANCE:case NETEASE_APP_NETWORK:case NETEASE_APP_ACCOUNT:case NETEASE_APP_ABOUT:view(l,NETEASE_APP_SETTINGS);break;
        case NETEASE_APP_BRIGHTNESS:view(l,NETEASE_APP_APPEARANCE);break;
        case NETEASE_APP_LOGOUT_CONFIRM:view(l,NETEASE_APP_ACCOUNT);break;
        case NETEASE_APP_WIFI_CONFIRM:view(l,NETEASE_APP_NETWORK);break;
        case NETEASE_APP_ERROR:view(l,l->error_return_state==NETEASE_APP_PLAYING?NETEASE_APP_TRACKS:l->error_return_state);break;
        default:l->return_state=l->state;view(l,NETEASE_APP_NAVIGATION);l->selected=l->return_state==NETEASE_APP_PLAYING?2:0;break;
        }
        return NETEASE_APP_ACTION_NONE;
    }
    if(l->state==NETEASE_APP_BRIGHTNESS) {
        if(b==NETEASE_APP_BTN_OK) { view(l,NETEASE_APP_APPEARANCE);return NETEASE_APP_ACTION_NONE; }
        return b==NETEASE_APP_BTN_UP?NETEASE_APP_ACTION_BRIGHTNESS_UP:NETEASE_APP_ACTION_BRIGHTNESS_DOWN;
    }
    if(l->state==NETEASE_APP_ERROR && b==NETEASE_APP_BTN_DOWN) {
        l->return_state=NETEASE_APP_ERROR;view(l,NETEASE_APP_NETWORK);
        return NETEASE_APP_ACTION_NONE;
    }
    size_t n=menu_count(l->state);
    if(n) {
        if(b!=NETEASE_APP_BTN_OK) { move_selection(l,b==NETEASE_APP_BTN_UP?-1:1,n);return NETEASE_APP_ACTION_NONE; }
        size_t i=l->selected;
        switch(l->state) {
        case NETEASE_APP_NAVIGATION:
            if(i==0) { if(l->has_player)view(l,NETEASE_APP_PLAYING); }
            else if(i==1) { if(l->has_player || l->track_count)view(l,NETEASE_APP_TRACKS); }
            else if(i==2)view(l,NETEASE_APP_PLAYLISTS);
            else if(i==3) { view(l,l->return_state);l->volume_overlay=true; }
            else view(l,NETEASE_APP_SETTINGS);
            break;
        case NETEASE_APP_ENDED:
            if(i==0) { view(l,NETEASE_APP_PLAYING);l->selected=0;return NETEASE_APP_ACTION_LOAD_TRACK_URL; }
            view(l,NETEASE_APP_TRACKS);break;
        case NETEASE_APP_SETTINGS: { const netease_app_state_t dest[]={NETEASE_APP_APPEARANCE,NETEASE_APP_NETWORK,NETEASE_APP_ACCOUNT,NETEASE_APP_ABOUT};view(l,dest[i]);break; }
        case NETEASE_APP_APPEARANCE:
            if(i<2)return i==0?NETEASE_APP_ACTION_SET_DAY:NETEASE_APP_ACTION_SET_NIGHT;
            view(l,i==2?NETEASE_APP_BRIGHTNESS:NETEASE_APP_SETTINGS);break;
        case NETEASE_APP_NETWORK:view(l,i==0?NETEASE_APP_WIFI_CONFIRM:NETEASE_APP_SETTINGS);break;
        case NETEASE_APP_ACCOUNT:view(l,i==0?NETEASE_APP_LOGOUT_CONFIRM:NETEASE_APP_SETTINGS);break;
        case NETEASE_APP_LOGOUT_CONFIRM:if(i==1)return NETEASE_APP_ACTION_SWITCH_ACCOUNT;view(l,NETEASE_APP_ACCOUNT);break;
        case NETEASE_APP_WIFI_CONFIRM:if(i==1)return NETEASE_APP_ACTION_RESET_CLOUD_SETUP;view(l,NETEASE_APP_NETWORK);break;
        default:break;
        }
        return NETEASE_APP_ACTION_NONE;
    }
    if(l->state==NETEASE_APP_PLAYING) {
        if(b!=NETEASE_APP_BTN_OK) {
            if(e==NETEASE_APP_BTN_LONG) { l->return_state=l->state;l->volume_overlay=true;return b==NETEASE_APP_BTN_UP?NETEASE_APP_ACTION_VOLUME_UP:NETEASE_APP_ACTION_VOLUME_DOWN; }
            size_t before=l->selected;
            move_selection(l,b==NETEASE_APP_BTN_UP?-1:1,l->playing_count);
            return l->selected==before?NETEASE_APP_ACTION_NONE:NETEASE_APP_ACTION_LOAD_TRACK_URL;
        }
        l->playing=!l->playing;return NETEASE_APP_ACTION_TOGGLE_PLAYBACK;
    }
    if(l->state==NETEASE_APP_PLAYLISTS || l->state==NETEASE_APP_TRACKS) {
        if(b!=NETEASE_APP_BTN_OK) { move_selection(l,(b==NETEASE_APP_BTN_UP?-1:1)*(e==NETEASE_APP_BTN_LONG?5:1),l->item_count);return NETEASE_APP_ACTION_NONE; }
        if(l->item_count==0)return NETEASE_APP_ACTION_NONE;
        return l->state==NETEASE_APP_PLAYLISTS?NETEASE_APP_ACTION_LOAD_TRACKS:NETEASE_APP_ACTION_LOAD_TRACK_URL;
    }
    if(b==NETEASE_APP_BTN_OK && e==NETEASE_APP_BTN_CLICK) {
        if(l->state==NETEASE_APP_SIGNED_IN) { view(l,NETEASE_APP_PLAYLISTS);return NETEASE_APP_ACTION_NONE; }
        if(l->state==NETEASE_APP_ERROR)return NETEASE_APP_ACTION_RETRY;
        if(l->state==NETEASE_APP_ABOUT)view(l,NETEASE_APP_SETTINGS);
        else if(l->state==NETEASE_APP_LOGIN_QR)return NETEASE_APP_ACTION_START_QR;
    }
    return NETEASE_APP_ACTION_NONE;
}
netease_app_action_t netease_app_logic_dispatch_button(netease_app_logic_t *l, netease_app_button_t b, netease_app_button_event_t e, bool pending)
{
    if(pending)return NETEASE_APP_ACTION_NONE;
    return netease_app_logic_on_button(l,b,e);
}

bool netease_app_prefetch_offset(uint32_t offset, size_t count, size_t selected,
                                 bool has_next, uint32_t next, uint32_t page_size,
                                 uint32_t *target)
{
    if(!target || !count || !page_size)return false;
    if(selected==0 && offset) { *target=offset>=page_size?offset-page_size:0;return true; }
    if(has_next && next>offset) { *target=next;return true; }
    if(offset) { *target=offset>=page_size?offset-page_size:0;return true; }
    return false;
}
