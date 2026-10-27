#include "device_music_ui_spec.h"
#include <stdio.h>
#include <string.h>

void device_music_ui_format_key_hint(char *out, size_t size, unsigned key, const char *action)
{
    static const char *const names[] = {"↑", "OK", "↓"};
    if (!out || !size) return;
    if (!action || !action[0] || !strcmp(action, "无") || !strcmp(action, "—") || key >= 3) {
        snprintf(out, size, "—");
        return;
    }
    snprintf(out, size, "%s %s", names[key], action);
}

unsigned device_music_ui_signal_level(bool connected, bool valid, int rssi_dbm)
{
    if (!connected || !valid) return 0;
    return rssi_dbm >= -60 ? 3 : rssi_dbm >= -75 ? 2 : 1;
}

static bool s_night;
static const device_music_ui_theme_t s_themes[2] = {
    { .width=240,.height=320,.background=0xF0E9DA,.top_bar=0xF0E9DA,.footer_bar=0xF0E9DA,
      .divider=0xC3AD91,.card=0xE7DCC9,.error_card=0xE7DCC9,.text=0x3C3026,
      .muted_text=0x77624C,.subtle_text=0x77624C,.netease_red=0x8E482F,.green=0x465C3B,
      .blue=0x8E482F,.amber=0x8E482F,.danger=0x8E482F,.footer_y=293,.footer_h=27 },
    { .width=240,.height=320,.background=0x29251F,.top_bar=0x29251F,.footer_bar=0x29251F,
      .divider=0x635242,.card=0x373027,.error_card=0x373027,.text=0xE7DDC9,
      .muted_text=0xBAAC96,.subtle_text=0xBAAC96,.netease_red=0xD4A47B,.green=0xB2BD8B,
      .blue=0xD4A47B,.amber=0xD4A47B,.danger=0xD4A47B,.footer_y=293,.footer_h=27 }
};
void device_music_ui_set_night(bool night) { s_night=night; }
bool device_music_ui_is_night(void) { return s_night; }

#define RECT(x_, y_, w_, h_) { .x = (x_), .y = (y_), .w = (w_), .h = (h_) }
#define FOOTER(up_, ok_, down_) { .up_label = (up_), .ok_label = (ok_), .down_label = (down_) }

static const device_music_ui_screen_spec_t s_screens[DEVICE_MUSIC_SCREEN_COUNT] = {
    [DEVICE_MUSIC_SCREEN_SETUP] = {
        .screen = DEVICE_MUSIC_SCREEN_SETUP,
        .kind = DEVICE_MUSIC_SCREEN_KIND_MESSAGE,
        .app_title = "网易云音乐",
        .state_chip = "未配置",
        .chip_color = 0xF59E0B,
        .title = "选择 Wi-Fi",
        .detail = "设备扫描附近网络\n使用三个按键输入密码",
        .main = RECT(16, 100, 208, 84),
        .footer = FOOTER("无", "确认", "无"),
    },
    [DEVICE_MUSIC_SCREEN_REGISTERING] = {
        .screen = DEVICE_MUSIC_SCREEN_REGISTERING,
        .kind = DEVICE_MUSIC_SCREEN_KIND_PROGRESS,
        .app_title = "网易云音乐",
        .state_chip = "注册中",
        .chip_color = 0x60A5FA,
        .title = "正在连接",
        .detail = "加入 Wi-Fi\n注册设备\n检查音乐接口",
        .main = RECT(89, 72, 62, 62),
        .footer = FOOTER("无", "等待", "无"),
    },
    [DEVICE_MUSIC_SCREEN_READY] = {
        .screen = DEVICE_MUSIC_SCREEN_READY,
        .kind = DEVICE_MUSIC_SCREEN_KIND_SUCCESS,
        .app_title = "网易云音乐",
        .state_chip = "就绪",
        .chip_color = 0x19C37D,
        .title = "设备已绑定",
        .detail = "设备注册成功\n按 OK 进入音乐应用",
        .main = RECT(82, 62, 76, 76),
        .footer = FOOTER("无", "进入", "无"),
    },
    [DEVICE_MUSIC_SCREEN_SETUP_FAILED] = {
        .screen = DEVICE_MUSIC_SCREEN_SETUP_FAILED,
        .kind = DEVICE_MUSIC_SCREEN_KIND_ERROR,
        .app_title = "网易云音乐",
        .state_chip = "失败",
        .chip_color = 0xEF4444,
        .title = "配置失败",
        .detail = "Wi-Fi 或服务不可用\n短按重试\n长按返回",
        .main = RECT(18, 58, 204, 154),
        .footer = FOOTER("—", "重试", "—"),
    },
    [DEVICE_MUSIC_SCREEN_QR_LOGIN] = {
        .screen = DEVICE_MUSIC_SCREEN_QR_LOGIN,
        .kind = DEVICE_MUSIC_SCREEN_KIND_QR,
        .app_title = "网易云音乐",
        .state_chip = "登录",
        .chip_color = 0xF87171,
        .title = "扫码，让音乐进来。",
        .detail = "手机打开网易云音乐扫一扫",
        .main = RECT(54, 110, 132, 132),
        .footer = FOOTER("—", "刷新码", "—"),
    },
    [DEVICE_MUSIC_SCREEN_LOGIN_WAITING] = {
        .screen = DEVICE_MUSIC_SCREEN_LOGIN_WAITING,
        .kind = DEVICE_MUSIC_SCREEN_KIND_CONFIRM,
        .app_title = "网易云音乐",
        .state_chip = "等待",
        .chip_color = 0xFBBF24,
        .title = "请在手机上确认登录",
        .detail = "设备会自动刷新状态",
        .main = RECT(54, 110, 132, 132),
        .footer = FOOTER("—", "等待确认", "—"),
    },
    [DEVICE_MUSIC_SCREEN_PLAYLISTS] = {
        .screen = DEVICE_MUSIC_SCREEN_PLAYLISTS,
        .kind = DEVICE_MUSIC_SCREEN_KIND_LIST,
        .app_title = "网易云音乐",
        .state_chip = "歌单",
        .chip_color = 0xF87171,
        .title = "我的歌单",
        .detail = "",
        .rows = RECT(15, 94, 210, 42),
        .row_count = 4,
        .footer = FOOTER("上移", "打开", "下移"),
    },
    [DEVICE_MUSIC_SCREEN_TRACKS] = {
        .screen = DEVICE_MUSIC_SCREEN_TRACKS,
        .kind = DEVICE_MUSIC_SCREEN_KIND_LIST,
        .app_title = "网易云音乐",
        .state_chip = "歌曲",
        .chip_color = 0xF87171,
        .title = "歌曲列表",
        .detail = "",
        .rows = RECT(15, 94, 210, 42),
        .row_count = 4,
        .footer = FOOTER("上移", "播放", "下移"),
    },
    [DEVICE_MUSIC_SCREEN_LOADING_TRACK] = {
        .screen = DEVICE_MUSIC_SCREEN_LOADING_TRACK,
        .kind = DEVICE_MUSIC_SCREEN_KIND_PROGRESS,
        .app_title = "网易云音乐",
        .state_chip = "加载",
        .chip_color = 0x60A5FA,
        .title = "准备播放",
        .detail = "获取播放地址\n启动音频流",
        .main = RECT(89, 70, 62, 62),
        .footer = FOOTER("无", "等待", "无"),
    },
    [DEVICE_MUSIC_SCREEN_PLAYING] = {
        .screen = DEVICE_MUSIC_SCREEN_PLAYING,
        .kind = DEVICE_MUSIC_SCREEN_KIND_PLAYER,
        .app_title = "网易云音乐",
        .state_chip = "播放中",
        .chip_color = 0x19C37D,
        .title = "正在播放",
        .detail = "",
        .main = RECT(58, 78, 118, 54),
        .footer = FOOTER("上一首", "暂停", "下一首"),
    },
    [DEVICE_MUSIC_SCREEN_PAUSED] = {
        .screen = DEVICE_MUSIC_SCREEN_PAUSED,
        .kind = DEVICE_MUSIC_SCREEN_KIND_PAUSED,
        .app_title = "网易云音乐",
        .state_chip = "暂停",
        .chip_color = 0xFBBF24,
        .title = "正在播放",
        .detail = "播放已暂停",
        .main = RECT(72, 58, 96, 96),
        .footer = FOOTER("上一首", "继续", "下一首"),
    },
    [DEVICE_MUSIC_SCREEN_PLAY_FAILED] = {
        .screen = DEVICE_MUSIC_SCREEN_PLAY_FAILED,
        .kind = DEVICE_MUSIC_SCREEN_KIND_ERROR,
        .app_title = "网易云音乐",
        .state_chip = "失败",
        .chip_color = 0xEF4444,
        .title = "播放失败",
        .detail = "播放地址不可用\n短按重试\n长按返回",
        .main = RECT(18, 58, 204, 154),
        .footer = FOOTER("—", "重试", "—"),
    },
};

const device_music_ui_theme_t *device_music_ui_theme(void)
{
    return &s_themes[s_night ? 1 : 0];
}

size_t device_music_ui_screen_count(void)
{
    return DEVICE_MUSIC_SCREEN_COUNT;
}

const device_music_ui_screen_spec_t *device_music_ui_screen(device_music_screen_t screen)
{
    if (screen < 0 || screen >= DEVICE_MUSIC_SCREEN_COUNT) {
        return 0;
    }
    return &s_screens[screen];
}

int device_music_ui_progress_percent(int elapsed_ms, int duration_ms)
{
    if (elapsed_ms <= 0 || duration_ms <= 0) {
        return 0;
    }
    if (elapsed_ms >= duration_ms) {
        return 100;
    }
    return (int)(((int64_t)elapsed_ms * 100) / duration_ms);
}

int device_music_ui_progress_width(int track_width, int progress_percent)
{
    if (track_width <= 0 || progress_percent <= 0) {
        return 0;
    }
    if (progress_percent >= 100) {
        return track_width;
    }
    return (track_width * progress_percent) / 100;
}
