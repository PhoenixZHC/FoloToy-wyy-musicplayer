#include <assert.h>

#include "netease_app_logic.h"

int main(void)
{
    netease_app_logic_t logic = {0};
    netease_app_logic_init(&logic);
    assert(netease_app_logic_state(&logic) == NETEASE_APP_LOGIN_QR);
    assert(netease_app_logic_on_button(&logic, NETEASE_APP_BTN_OK, NETEASE_APP_BTN_LONG) == NETEASE_APP_ACTION_RESET_CLOUD_SETUP);
    assert(netease_app_logic_dispatch_button(&logic,
                                             NETEASE_APP_BTN_OK,
                                             NETEASE_APP_BTN_LONG,
                                             true) == NETEASE_APP_ACTION_RESET_CLOUD_SETUP);
    assert(netease_app_logic_dispatch_button(&logic,
                                             NETEASE_APP_BTN_OK,
                                             NETEASE_APP_BTN_CLICK,
                                             true) == NETEASE_APP_ACTION_NONE);

    netease_app_logic_set_playlists(&logic, 3);
    assert(netease_app_logic_state(&logic) == NETEASE_APP_PLAYLISTS);
    assert(netease_app_logic_selected(&logic) == 0);
    assert(netease_app_logic_dispatch_button(&logic,
                                             NETEASE_APP_BTN_OK,
                                             NETEASE_APP_BTN_LONG,
                                             true) == NETEASE_APP_ACTION_NONE);
    assert(netease_app_logic_state(&logic) == NETEASE_APP_PLAYLISTS);

    assert(netease_app_logic_on_button(&logic, NETEASE_APP_BTN_DOWN, NETEASE_APP_BTN_CLICK) == NETEASE_APP_ACTION_NONE);
    assert(netease_app_logic_selected(&logic) == 1);
    assert(netease_app_logic_on_button(&logic, NETEASE_APP_BTN_UP, NETEASE_APP_BTN_CLICK) == NETEASE_APP_ACTION_NONE);
    assert(netease_app_logic_selected(&logic) == 0);

    assert(netease_app_logic_on_button(&logic, NETEASE_APP_BTN_OK, NETEASE_APP_BTN_CLICK) == NETEASE_APP_ACTION_LOAD_TRACKS);
    assert(netease_app_logic_state(&logic) == NETEASE_APP_PLAYLISTS);

    netease_app_logic_set_tracks(&logic, 2);
    assert(netease_app_logic_on_button(&logic, NETEASE_APP_BTN_DOWN, NETEASE_APP_BTN_CLICK) == NETEASE_APP_ACTION_NONE);
    assert(netease_app_logic_selected(&logic) == 1);
    assert(netease_app_logic_on_button(&logic, NETEASE_APP_BTN_OK, NETEASE_APP_BTN_CLICK) == NETEASE_APP_ACTION_LOAD_TRACK_URL);
    assert(netease_app_logic_state(&logic) == NETEASE_APP_TRACKS);
    assert(!netease_app_logic_is_playing(&logic));
    netease_app_logic_set_playing(&logic);
    assert(netease_app_logic_state(&logic) == NETEASE_APP_PLAYING);
    assert(netease_app_logic_is_playing(&logic));

    assert(netease_app_logic_on_button(&logic, NETEASE_APP_BTN_UP, NETEASE_APP_BTN_LONG) == NETEASE_APP_ACTION_VOLUME_UP);
    assert(netease_app_logic_volume_overlay(&logic));
    assert(netease_app_logic_selected(&logic) == 1);
    assert(netease_app_logic_on_button(&logic, NETEASE_APP_BTN_DOWN, NETEASE_APP_BTN_CLICK) == NETEASE_APP_ACTION_VOLUME_DOWN);
    assert(netease_app_logic_selected(&logic) == 1);
    assert(netease_app_logic_on_button(&logic, NETEASE_APP_BTN_OK, NETEASE_APP_BTN_CLICK) == NETEASE_APP_ACTION_NONE);
    assert(!netease_app_logic_volume_overlay(&logic));
    assert(netease_app_logic_on_button(&logic, NETEASE_APP_BTN_UP, NETEASE_APP_BTN_CLICK) == NETEASE_APP_ACTION_LOAD_TRACK_URL);
    assert(netease_app_logic_selected(&logic) == 0);
    assert(netease_app_logic_on_button(&logic, NETEASE_APP_BTN_DOWN, NETEASE_APP_BTN_CLICK) == NETEASE_APP_ACTION_LOAD_TRACK_URL);
    assert(netease_app_logic_selected(&logic) == 1);

    assert(netease_app_logic_on_button(&logic, NETEASE_APP_BTN_OK, NETEASE_APP_BTN_CLICK) == NETEASE_APP_ACTION_TOGGLE_PLAYBACK);
    assert(!netease_app_logic_is_playing(&logic));
    assert(netease_app_logic_on_button(&logic, NETEASE_APP_BTN_OK, NETEASE_APP_BTN_LONG) == NETEASE_APP_ACTION_NONE);
    assert(netease_app_logic_state(&logic) == NETEASE_APP_TRACKS);
    assert(netease_app_logic_on_button(&logic, NETEASE_APP_BTN_OK, NETEASE_APP_BTN_LONG) == NETEASE_APP_ACTION_NONE);
    assert(netease_app_logic_state(&logic) == NETEASE_APP_PLAYLISTS);
    assert(netease_app_logic_on_button(&logic, NETEASE_APP_BTN_OK, NETEASE_APP_BTN_LONG) == NETEASE_APP_ACTION_SWITCH_ACCOUNT);
    assert(netease_app_logic_state(&logic) == NETEASE_APP_LOGIN_QR);

    netease_app_logic_set_error(&logic);
    assert(netease_app_logic_state(&logic) == NETEASE_APP_ERROR);
    assert(netease_app_logic_on_button(&logic, NETEASE_APP_BTN_OK, NETEASE_APP_BTN_LONG) == NETEASE_APP_ACTION_RESET_CLOUD_SETUP);
    assert(netease_app_logic_on_button(&logic, NETEASE_APP_BTN_OK, NETEASE_APP_BTN_CLICK) == NETEASE_APP_ACTION_START_QR);
    assert(netease_app_logic_state(&logic) == NETEASE_APP_LOGIN_QR);

    return 0;
}
