#include <assert.h>
#include "netease_app_logic.h"
#define CLICK(b) netease_app_logic_on_button(&l,NETEASE_APP_BTN_##b,NETEASE_APP_BTN_CLICK)
#define LONG(b) netease_app_logic_on_button(&l,NETEASE_APP_BTN_##b,NETEASE_APP_BTN_LONG)
static void test_screen_timeout_menu(void)
{
    for(int playing=0;playing<2;playing++) {
        netease_app_logic_t l;netease_app_logic_init(&l);
        netease_app_logic_set_tracks(&l,5);l.selected=2;
        netease_app_logic_set_playing(&l);l.playing=playing!=0;
        LONG(OK);CLICK(DOWN);CLICK(DOWN);CLICK(OK);
        assert(l.state==NETEASE_APP_SETTINGS);
        CLICK(OK);assert(l.state==NETEASE_APP_APPEARANCE);
        CLICK(DOWN);assert(CLICK(OK)==NETEASE_APP_ACTION_NONE);
        assert(l.state==NETEASE_APP_SCREEN_TIMEOUT && l.selected==3);
        CLICK(DOWN);CLICK(DOWN);
        assert(l.timeout_option==3); /* 浏览不改变已保存选项。 */
        assert(CLICK(OK)==NETEASE_APP_ACTION_SET_SCREEN_TIMEOUT && l.timeout_option==5);
        assert(l.state==NETEASE_APP_SCREEN_TIMEOUT && l.playing==(playing!=0));
        LONG(OK);assert(l.state==NETEASE_APP_APPEARANCE);
        CLICK(DOWN);CLICK(OK);assert(l.state==NETEASE_APP_SCREEN_TIMEOUT && l.selected==5);
        CLICK(DOWN);CLICK(DOWN);assert(l.selected==6);CLICK(OK);
        assert(l.state==NETEASE_APP_APPEARANCE);LONG(OK);
        for(int i=0;i<6;i++)CLICK(DOWN);
        assert(l.selected==3);CLICK(OK);assert(l.state==NETEASE_APP_ABOUT);
        LONG(OK);LONG(OK);assert(l.state==NETEASE_APP_PLAYING && l.selected==2);
        assert(l.playing==(playing!=0) && l.playing_index==2 && !l.volume_overlay);
    }
}
int main(void)
{
    test_screen_timeout_menu();
    netease_app_logic_t l;netease_app_logic_init(&l);
    assert(netease_app_logic_dispatch_button(&l,NETEASE_APP_BTN_OK,NETEASE_APP_BTN_LONG,true)==NETEASE_APP_ACTION_NONE);
    netease_app_logic_set_playlists(&l,10);LONG(DOWN);assert(l.selected==5);
    assert(CLICK(OK)==NETEASE_APP_ACTION_LOAD_TRACKS);
    netease_app_logic_set_tracks(&l,10);CLICK(DOWN);assert(l.selected==1);
    assert(CLICK(OK)==NETEASE_APP_ACTION_LOAD_TRACK_URL);netease_app_logic_set_playing(&l);
    assert(l.playing_index==1 && l.playing);
    CLICK(OK);assert(!l.playing);LONG(UP);assert(l.volume_overlay);
    CLICK(OK);assert(!l.volume_overlay && !l.playing);
    LONG(OK);assert(l.state==NETEASE_APP_NAVIGATION && l.selected==2);
    CLICK(OK);assert(l.state==NETEASE_APP_PLAYLISTS && l.selected==5 && !l.playing);
    LONG(OK);assert(l.selected==0);CLICK(OK);assert(l.state==NETEASE_APP_PLAYING && l.selected==1 && !l.playing);
    LONG(OK);CLICK(UP);CLICK(OK);assert(l.state==NETEASE_APP_TRACKS && l.selected==1);
    LONG(DOWN);assert(l.selected==6 && l.playing_index==1);
    LONG(OK);CLICK(OK);assert(l.selected==1 && l.state==NETEASE_APP_PLAYING);
    LONG(OK);CLICK(DOWN);CLICK(DOWN);CLICK(OK);assert(l.state==NETEASE_APP_SETTINGS);
    CLICK(OK);assert(l.state==NETEASE_APP_APPEARANCE);
    CLICK(OK);assert(l.state==NETEASE_APP_BRIGHTNESS);
    assert(CLICK(DOWN)==NETEASE_APP_ACTION_BRIGHTNESS_DOWN);LONG(OK);
    LONG(OK);CLICK(DOWN);CLICK(DOWN);CLICK(OK);assert(l.state==NETEASE_APP_ACCOUNT);
    CLICK(OK);assert(l.state==NETEASE_APP_LOGOUT_CONFIRM && l.selected==0);
    CLICK(OK);assert(l.state==NETEASE_APP_ACCOUNT);
    CLICK(OK);CLICK(DOWN);assert(CLICK(OK)==NETEASE_APP_ACTION_SWITCH_ACCOUNT);
    netease_app_logic_set_playlists(&l,0);assert(l.state==NETEASE_APP_PLAYLISTS);
    netease_app_logic_set_tracks(&l,0);assert(l.state==NETEASE_APP_TRACKS);
    netease_app_logic_set_tracks(&l,2);l.selected=1;netease_app_logic_set_playing(&l);
    assert(CLICK(DOWN)==NETEASE_APP_ACTION_NONE);
    l.state=NETEASE_APP_ENDED;l.selected=0;
    assert(CLICK(OK)==NETEASE_APP_ACTION_LOAD_TRACK_URL && l.selected==0);
    netease_app_logic_init(&l);
    LONG(OK);CLICK(DOWN);CLICK(OK);
    assert(l.state==NETEASE_APP_NAVIGATION); /* no empty current-player page */
    l.state=NETEASE_APP_SIGNED_IN;l.playlist_count=3;l.playlist_cursor=2;
    assert(CLICK(OK)==NETEASE_APP_ACTION_NONE && l.state==NETEASE_APP_PLAYLISTS && l.selected==2);
    netease_app_logic_set_tracks(&l,15);l.selected=8;netease_app_logic_set_playing(&l);
    assert(l.track_cursor==8 && l.playing_index==8);
    l.playing=false;LONG(OK);CLICK(OK); /* default playlist */
    assert(l.state==NETEASE_APP_PLAYLISTS && !l.playing);
    LONG(OK);CLICK(DOWN);CLICK(DOWN);CLICK(DOWN);CLICK(DOWN);CLICK(OK);
    assert(l.state==NETEASE_APP_SETTINGS);
    LONG(OK);assert(l.state==NETEASE_APP_PLAYLISTS && !l.playing);
    l.state=NETEASE_APP_TRACKS;netease_app_logic_set_error(&l);netease_app_logic_set_error(&l);
    assert(l.error_return_state==NETEASE_APP_TRACKS);
    LONG(OK);assert(l.state==NETEASE_APP_TRACKS);
    netease_app_logic_set_error(&l);
    assert(CLICK(OK)==NETEASE_APP_ACTION_RETRY);
    CLICK(DOWN);assert(l.state==NETEASE_APP_NETWORK);
    CLICK(OK);assert(l.state==NETEASE_APP_WIFI_CONFIRM && l.selected==0);
    CLICK(OK);assert(l.state==NETEASE_APP_NETWORK);
    LONG(OK);LONG(OK);assert(l.state==NETEASE_APP_ERROR);
    CLICK(DOWN);CLICK(OK);CLICK(DOWN);
    assert(CLICK(OK)==NETEASE_APP_ACTION_RESET_CLOUD_SETUP);
    uint32_t target=0;
    assert(netease_app_prefetch_offset(0,4,0,true,4,4,&target) && target==4);
    assert(netease_app_prefetch_offset(44,4,2,true,48,4,&target) && target==48);
    assert(netease_app_prefetch_offset(48,4,0,true,52,4,&target) && target==44);
    assert(!netease_app_prefetch_offset(0,0,0,false,0,4,&target));
    assert(!netease_app_prefetch_offset(0,4,0,false,0,4,&target));
    return 0;
}
