#include "device_music_ui_spec.h"

static const device_music_ui_theme_t s_theme = {
    .width = 240,
    .height = 320,
    .background = 0x151821,
    .top_bar = 0x101216,
    .footer_bar = 0x101216,
    .divider = 0x242A35,
    .card = 0x202637,
    .error_card = 0x251B22,
    .text = 0xFFFFFF,
    .muted_text = 0xC7CEDB,
    .subtle_text = 0x9CA3AF,
    .netease_red = 0xE60026,
    .green = 0x19C37D,
    .blue = 0x60A5FA,
    .amber = 0xFBBF24,
    .danger = 0xEF4444,
    .footer_y = 268,
    .footer_h = 52,
};

#define RECT(x_, y_, w_, h_) { .x = (x_), .y = (y_), .w = (w_), .h = (h_) }
#define FOOTER(up_, ok_, down_) { .up_label = (up_), .ok_label = (ok_), .down_label = (down_) }

static const device_music_ui_screen_spec_t s_screens[DEVICE_MUSIC_SCREEN_COUNT] = {
    [DEVICE_MUSIC_SCREEN_SETUP] = {
        .screen = DEVICE_MUSIC_SCREEN_SETUP,
        .kind = DEVICE_MUSIC_SCREEN_KIND_MESSAGE,
        .app_title = "网易云音乐",
        .state_chip = "未配置",
        .chip_color = 0xF59E0B,
        .title = "连接设备热点",
        .detail = "打开浏览器进入 192.168.4.1\n填写 Wi-Fi、服务地址和注册码",
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
        .detail = "Wi-Fi 或服务不可用\n短按重试\n长按清空配置",
        .main = RECT(18, 58, 204, 154),
        .footer = FOOTER("无", "重试", "清空"),
    },
    [DEVICE_MUSIC_SCREEN_QR_LOGIN] = {
        .screen = DEVICE_MUSIC_SCREEN_QR_LOGIN,
        .kind = DEVICE_MUSIC_SCREEN_KIND_QR,
        .app_title = "网易云音乐",
        .state_chip = "登录",
        .chip_color = 0xF87171,
        .title = "扫码登录网易云",
        .detail = "手机打开网易云音乐扫一扫",
        .main = RECT(52, 50, 136, 136),
        .footer = FOOTER("无", "刷新", "清空"),
    },
    [DEVICE_MUSIC_SCREEN_LOGIN_WAITING] = {
        .screen = DEVICE_MUSIC_SCREEN_LOGIN_WAITING,
        .kind = DEVICE_MUSIC_SCREEN_KIND_CONFIRM,
        .app_title = "网易云音乐",
        .state_chip = "等待",
        .chip_color = 0xFBBF24,
        .title = "请在手机上确认登录",
        .detail = "设备会自动刷新状态",
        .main = RECT(52, 46, 136, 136),
        .footer = FOOTER("无", "等待", "清空"),
    },
    [DEVICE_MUSIC_SCREEN_PLAYLISTS] = {
        .screen = DEVICE_MUSIC_SCREEN_PLAYLISTS,
        .kind = DEVICE_MUSIC_SCREEN_KIND_LIST,
        .app_title = "网易云音乐",
        .state_chip = "歌单",
        .chip_color = 0xF87171,
        .title = "选择歌单",
        .detail = "",
        .rows = RECT(12, 78, 216, 33),
        .row_count = 5,
        .footer = FOOTER("上移", "打开", "下移"),
    },
    [DEVICE_MUSIC_SCREEN_TRACKS] = {
        .screen = DEVICE_MUSIC_SCREEN_TRACKS,
        .kind = DEVICE_MUSIC_SCREEN_KIND_LIST,
        .app_title = "网易云音乐",
        .state_chip = "歌曲",
        .chip_color = 0xF87171,
        .title = "每日推荐",
        .detail = "",
        .rows = RECT(12, 78, 216, 33),
        .row_count = 5,
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
        .title = "城市灯光",
        .detail = "落日飞车 · 128 kbps",
        .main = RECT(58, 78, 118, 54),
        .footer = FOOTER("上一首", "暂停", "下一首"),
    },
    [DEVICE_MUSIC_SCREEN_PAUSED] = {
        .screen = DEVICE_MUSIC_SCREEN_PAUSED,
        .kind = DEVICE_MUSIC_SCREEN_KIND_PAUSED,
        .app_title = "网易云音乐",
        .state_chip = "暂停",
        .chip_color = 0xFBBF24,
        .title = "城市灯光",
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
        .detail = "播放地址不可用\n短按重新登录\n长按清空配置",
        .main = RECT(18, 58, 204, 154),
        .footer = FOOTER("无", "重试", "清空"),
    },
};

const device_music_ui_theme_t *device_music_ui_theme(void)
{
    return &s_theme;
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
