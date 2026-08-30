#include "device_music_ui.h"

#include "bsp_battery.h"

#include <stdio.h>

LV_FONT_DECLARE(netease_ui_font_8);
LV_FONT_DECLARE(netease_ui_font_9);
LV_FONT_DECLARE(netease_ui_font_10);
LV_FONT_DECLARE(netease_ui_font_12);
LV_FONT_DECLARE(netease_ui_font_13);
LV_FONT_DECLARE(netease_ui_font_14);
LV_FONT_DECLARE(netease_ui_font_17);
LV_FONT_DECLARE(netease_ui_font_18);
LV_FONT_DECLARE(netease_ui_font_22);
LV_FONT_DECLARE(netease_ui_font_24);

static const lv_font_t *font_px(int size)
{
    switch (size) {
    case 8: return &netease_ui_font_8;
    case 9: return &netease_ui_font_9;
    case 10: return &netease_ui_font_10;
    case 12: return &netease_ui_font_12;
    case 13: return &netease_ui_font_13;
    case 14: return &netease_ui_font_14;
    case 17: return &netease_ui_font_17;
    case 18: return &netease_ui_font_18;
    case 22: return &netease_ui_font_22;
    case 24: return &netease_ui_font_24;
    default: return &netease_ui_font_14;
    }
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    if (parent == NULL) return NULL;
    lv_obj_t *obj = lv_obj_create(parent);
    if (obj == NULL) return NULL;
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent,
                       const char *text,
                       int x,
                       int y,
                       int w,
                       int h,
                       int size,
                       uint32_t color,
                       lv_text_align_t align)
{
    if (parent == NULL) return NULL;
    lv_obj_t *obj = lv_label_create(parent);
    if (obj == NULL) return NULL;
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_text_font(obj, font_px(size), 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, align, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    lv_label_set_text(obj, text ? text : "");
    return obj;
}

static void format_time(char *out, size_t out_len, int milliseconds)
{
    if (out == NULL || out_len == 0) return;
    if (milliseconds < 0) milliseconds = 0;
    int seconds = milliseconds / 1000;
    snprintf(out, out_len, "%02d:%02d", seconds / 60, seconds % 60);
}

static void draw_player_progress(lv_obj_t *screen,
                                 int progress_percent,
                                 int elapsed_ms,
                                 int duration_ms,
                                 device_music_ui_player_widgets_t *widgets)
{
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    box(screen, 32, 230, 176, 5, 0x343A46, 3);
    int fill = device_music_ui_progress_width(176, progress_percent);
    lv_obj_t *fill_obj = box(screen, 32, 230, fill > 0 ? fill : 1, 5, theme->netease_red, 3);
    if (fill_obj != NULL && fill <= 0) {
        lv_obj_set_style_bg_opa(fill_obj, LV_OPA_TRANSP, 0);
    }

    char elapsed[8];
    char duration[8];
    format_time(elapsed, sizeof(elapsed), elapsed_ms);
    format_time(duration, sizeof(duration), duration_ms);
    lv_obj_t *elapsed_label = label(screen, elapsed, 32, 240, 52, 15, 12, theme->muted_text, LV_TEXT_ALIGN_LEFT);
    lv_obj_t *duration_label = label(screen, duration, 156, 240, 52, 15, 12, theme->muted_text, LV_TEXT_ALIGN_RIGHT);
    if (widgets != NULL) {
        widgets->progress_fill = fill_obj;
        widgets->elapsed_label = elapsed_label;
        widgets->duration_label = duration_label;
    }
}

static void draw_header(lv_obj_t *screen, const device_music_ui_screen_spec_t *spec)
{
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    box(screen, 0, 0, theme->width, 34, theme->top_bar, 0);
    box(screen, 10, 10, 12, 12, theme->netease_red, 6);
    lv_obj_t *dot = lv_obj_create(screen);
    if (dot == NULL) return;
    lv_obj_remove_style_all(dot);
    lv_obj_set_pos(dot, 14, 14);
    lv_obj_set_size(dot, 4, 4);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, lv_color_white(), 0);

    label(screen, spec->app_title, 28, 5, 116, 20, 17, theme->text, LV_TEXT_ALIGN_LEFT);
    label(screen, spec->state_chip, 142, 7, 60, 15, 12, spec->chip_color, LV_TEXT_ALIGN_RIGHT);

    box(screen, 210, 10, 18, 9, 0x2A2F3A, 2);
    box(screen, 228, 13, 2, 3, 0x6B7280, 0);
    int soc = bsp_battery_soc();
    int fill = 12;
    if (soc >= 0) {
        fill = (soc * 14) / 100;
        if (fill < 1) fill = 1;
        if (fill > 14) fill = 14;
    }
    box(screen, 212, 12, fill, 5, theme->green, 1);
}

static void draw_footer(lv_obj_t *screen, const device_music_ui_footer_t *footer)
{
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    box(screen, 0, theme->footer_y, theme->width, theme->footer_h, theme->footer_bar, 0);
    box(screen, 0, theme->footer_y, theme->width, 1, theme->divider, 0);

    const char *keys[3] = {"UP", "OK", "DN"};
    const char *labels[3] = {footer->up_label, footer->ok_label, footer->down_label};
    const int xs[3] = {10, 86, 164};
    const int widths[3] = {66, 70, 66};
    for (int i = 0; i < 3; i++) {
        box(screen, xs[i] + 18, 280, 30, 17, 0xF3F4F6, 4);
        label(screen, keys[i], xs[i] + 18, 281, 30, 13, 10, 0x111318, LV_TEXT_ALIGN_CENTER);
        label(screen, labels[i], xs[i], 299, widths[i], 18, 14, theme->text, LV_TEXT_ALIGN_CENTER);
    }
}

static void draw_step_dots(lv_obj_t *screen, int active)
{
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    for (int i = 0; i < 4; i++) {
        box(screen, 84 + i * 18, 245, 10, 4, i < active ? theme->netease_red : 0x343A46, 2);
    }
}

static bool setup_screen(lv_obj_t *obj, device_music_screen_t screen)
{
    const device_music_ui_screen_spec_t *spec = device_music_ui_screen(screen);
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    if (obj == NULL || spec == NULL || theme == NULL) return false;

    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, theme->width, theme->height);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(theme->background), 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    draw_header(obj, spec);
    draw_footer(obj, &spec->footer);
    return true;
}

lv_obj_t *device_music_ui_create_screen(device_music_screen_t screen)
{
    lv_obj_t *obj = lv_obj_create(NULL);
    if (obj == NULL) return NULL;
    if (!setup_screen(obj, screen)) {
        lv_obj_delete(obj);
        return NULL;
    }
    return obj;
}

bool device_music_ui_reset_screen(lv_obj_t *screen, device_music_screen_t screen_id)
{
    if (screen == NULL) return false;
    lv_obj_clean(screen);
    return setup_screen(screen, screen_id);
}

void device_music_ui_draw_setup(lv_obj_t *screen, const char *ap_ssid)
{
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    const device_music_ui_screen_spec_t *spec = device_music_ui_screen(DEVICE_MUSIC_SCREEN_SETUP);
    label(screen, spec->title, 14, 56, 212, 28, 22, theme->text, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *card = box(screen, 16, 100, 208, 84, theme->card, 8);
    label(card, "设备热点", 14, 12, 180, 16, 12, 0x8B94A7, LV_TEXT_ALIGN_LEFT);
    label(card, ap_ssid && ap_ssid[0] ? ap_ssid : "WY-MUSIC-7A3F", 14, 34, 180, 23, 18, theme->text, LV_TEXT_ALIGN_LEFT);
    label(screen, spec->detail, 20, 204, 200, 36, 13, theme->muted_text, LV_TEXT_ALIGN_CENTER);
}

void device_music_ui_draw_message(lv_obj_t *screen,
                                  device_music_screen_t screen_id,
                                  const char *title,
                                  const char *detail)
{
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    const device_music_ui_screen_spec_t *spec = device_music_ui_screen(screen_id);
    label(screen, title ? title : spec->title, 16, 154, 208, 28, 22, theme->text, LV_TEXT_ALIGN_CENTER);
    label(screen, detail ? detail : spec->detail, 20, 194, 200, 60, 14, theme->muted_text, LV_TEXT_ALIGN_CENTER);
}

void device_music_ui_draw_progress(lv_obj_t *screen,
                                   device_music_screen_t screen_id,
                                   const char *title,
                                   const char *detail,
                                   int active_steps)
{
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    const device_music_ui_screen_spec_t *spec = device_music_ui_screen(screen_id);
    device_music_ui_rect_t r = spec->main;
    lv_obj_t *ring = lv_spinner_create(screen);
    if (ring == NULL) return;
    lv_obj_set_pos(ring, r.x, r.y);
    lv_obj_set_size(ring, r.w, r.h);
    lv_obj_set_style_arc_color(ring, lv_color_hex(0x343A46), LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, lv_color_hex(theme->netease_red), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ring, 5, LV_PART_INDICATOR);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    label(screen, title ? title : spec->title, 16, 154, 208, 28, 22, theme->text, LV_TEXT_ALIGN_CENTER);
    label(screen, detail ? detail : spec->detail, 20, 190, 200, 60, 14, theme->muted_text, LV_TEXT_ALIGN_CENTER);
    draw_step_dots(screen, active_steps);
}

void device_music_ui_draw_success(lv_obj_t *screen, const char *title, const char *detail)
{
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    lv_obj_t *circle = lv_obj_create(screen);
    if (circle == NULL) return;
    lv_obj_remove_style_all(circle);
    lv_obj_set_pos(circle, 82, 62);
    lv_obj_set_size(circle, 76, 76);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(theme->green), 0);
    box(screen, 105, 101, 10, 24, theme->text, 5);
    box(screen, 118, 83, 10, 42, theme->text, 5);
    device_music_ui_draw_message(screen, DEVICE_MUSIC_SCREEN_READY, title, detail);
}

void device_music_ui_draw_error(lv_obj_t *screen,
                                device_music_screen_t screen_id,
                                const char *title,
                                const char *detail)
{
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    const device_music_ui_screen_spec_t *spec = device_music_ui_screen(screen_id);
    device_music_ui_rect_t r = spec->main;
    lv_obj_t *card = box(screen, r.x, r.y, r.w, r.h, theme->error_card, 8);
    label(card, title ? title : spec->title, 14, 16, 176, 28, 22, 0xFCA5A5, LV_TEXT_ALIGN_LEFT);
    label(card, detail ? detail : spec->detail, 14, 60, 176, 60, 14, 0xF3D3D8, LV_TEXT_ALIGN_LEFT);
}

static lv_obj_t *draw_qr_frame(lv_obj_t *screen, device_music_ui_rect_t r)
{
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    lv_obj_t *container = box(screen, r.x, r.y, r.w, r.h, 0xF8FAFC, 8);
    if (container == NULL) return NULL;
    box(container, 12, 12, 112, 112, 0xFFFFFF, 0);
    lv_obj_t *qr = lv_qrcode_create(container);
    if (qr == NULL) return NULL;
    lv_qrcode_set_size(qr, 112);
    lv_qrcode_set_dark_color(qr, lv_color_hex(0x111318));
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_set_quiet_zone(qr, false);
    lv_obj_set_pos(qr, 12, 12);
    lv_obj_set_style_bg_color(qr, lv_color_white(), 0);
    (void)theme;
    return qr;
}

lv_obj_t *device_music_ui_draw_qr(lv_obj_t *screen,
                                  device_music_screen_t screen_id,
                                  const char *title,
                                  const char *detail)
{
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    const device_music_ui_screen_spec_t *spec = device_music_ui_screen(screen_id);
    lv_obj_t *qr = draw_qr_frame(screen, spec->main);
    label(screen, title ? title : spec->title, 16, 196, 208, 23, 18, theme->text, LV_TEXT_ALIGN_CENTER);
    label(screen, detail ? detail : spec->detail, 20, 224, 200, 16, 12, theme->muted_text, LV_TEXT_ALIGN_CENTER);
    return qr;
}

void device_music_ui_draw_confirm(lv_obj_t *screen,
                                  const char *title,
                                  const char *detail,
                                  lv_obj_t **qr_out)
{
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    const device_music_ui_screen_spec_t *spec = device_music_ui_screen(DEVICE_MUSIC_SCREEN_LOGIN_WAITING);
    lv_obj_t *qr = draw_qr_frame(screen, spec->main);
    box(screen, 36, 188, 168, 34, 0x2A2130, 17);
    label(screen, title ? title : spec->title, 46, 194, 148, 18, 14, theme->text, LV_TEXT_ALIGN_CENTER);
    label(screen, detail ? detail : spec->detail, 20, 236, 200, 16, 12, theme->muted_text, LV_TEXT_ALIGN_CENTER);
    if (qr_out != NULL) {
        *qr_out = qr;
    }
}

void device_music_ui_draw_list(lv_obj_t *screen,
                               device_music_screen_t screen_id,
                               const char *heading,
                               const char *count,
                               const device_music_ui_row_t *rows,
                               size_t row_count)
{
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    const device_music_ui_screen_spec_t *spec = device_music_ui_screen(screen_id);
    label(screen, heading ? heading : spec->title, 14, 46, 150, 24, 18, theme->text, LV_TEXT_ALIGN_LEFT);
    label(screen, count ? count : "", 180, 50, 42, 15, 12, theme->muted_text, LV_TEXT_ALIGN_RIGHT);

    size_t max_rows = spec->row_count;
    if (row_count < max_rows) {
        max_rows = row_count;
    }
    for (size_t i = 0; i < max_rows; i++) {
        int y = spec->rows.y + (int)i * 36;
        uint32_t bg = rows[i].selected ? theme->netease_red : theme->card;
        uint32_t title_color = rows[i].selected ? theme->text : 0xE5E7EB;
        uint32_t sub_color = rows[i].selected ? 0xFFE1E7 : theme->subtle_text;
        box(screen, spec->rows.x, y, spec->rows.w, spec->rows.h, bg, 6);
        label(screen, rows[i].title, 24, y + 4, 142, 18, 14, title_color, LV_TEXT_ALIGN_LEFT);
        label(screen, rows[i].sub, 24, y + 21, 142, 12, 10, sub_color, LV_TEXT_ALIGN_LEFT);
        label(screen, rows[i].selected ? "播放" : " ", 176, y + 8, 40, 15, 12, rows[i].selected ? theme->text : theme->subtle_text, LV_TEXT_ALIGN_RIGHT);
    }
}

void device_music_ui_draw_player(lv_obj_t *screen,
                                 const char *song,
                                 const char *artist,
                                 int progress_percent,
                                 int elapsed_ms,
                                 int duration_ms,
                                 bool paused,
                                 device_music_ui_player_widgets_t *widgets)
{
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    if (widgets != NULL) {
        widgets->progress_fill = NULL;
        widgets->elapsed_label = NULL;
        widgets->duration_label = NULL;
    }
    if (paused) {
        lv_obj_t *ring = lv_obj_create(screen);
        if (ring == NULL) return;
        lv_obj_remove_style_all(ring);
        lv_obj_set_pos(ring, 72, 58);
        lv_obj_set_size(ring, 96, 96);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ring, 5, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(theme->netease_red), 0);
        box(screen, 101, 90, 12, 38, theme->text, 4);
        box(screen, 126, 90, 12, 38, theme->text, 4);
        label(screen, song ? song : "城市灯光", 16, 164, 208, 32, 24, theme->text, LV_TEXT_ALIGN_CENTER);
        label(screen, artist ? artist : "播放已暂停", 20, 202, 200, 19, 14, theme->muted_text, LV_TEXT_ALIGN_CENTER);
        draw_player_progress(screen, progress_percent, elapsed_ms, duration_ms, widgets);
        return;
    }

    const int xs[7] = {58, 76, 94, 112, 130, 148, 166};
    const int ys[7] = {112, 90, 104, 78, 98, 84, 108};
    const int hs[7] = {20, 42, 28, 54, 34, 48, 24};
    for (int i = 0; i < 7; i++) {
        box(screen, xs[i], ys[i], 10, hs[i], i % 2 == 0 ? theme->text : theme->netease_red, 5);
    }
    label(screen, song ? song : "城市灯光", 16, 150, 208, 32, 24, theme->text, LV_TEXT_ALIGN_CENTER);
    label(screen, artist ? artist : "落日飞车 · 128 kbps", 20, 188, 200, 19, 14, theme->muted_text, LV_TEXT_ALIGN_CENTER);
    draw_player_progress(screen, progress_percent, elapsed_ms, duration_ms, widgets);
}

void device_music_ui_update_player_progress(const device_music_ui_player_widgets_t *widgets,
                                            int progress_percent,
                                            int elapsed_ms,
                                            int duration_ms)
{
    if (widgets == NULL) return;
    int fill = device_music_ui_progress_width(176, progress_percent);
    if (widgets->progress_fill != NULL) {
        lv_obj_set_width(widgets->progress_fill, fill > 0 ? fill : 1);
        lv_obj_set_style_bg_opa(widgets->progress_fill, fill > 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    char elapsed[8];
    char duration[8];
    format_time(elapsed, sizeof(elapsed), elapsed_ms);
    format_time(duration, sizeof(duration), duration_ms);
    if (widgets->elapsed_label != NULL) {
        lv_label_set_text(widgets->elapsed_label, elapsed);
    }
    if (widgets->duration_label != NULL) {
        lv_label_set_text(widgets->duration_label, duration);
    }
}

void device_music_ui_update_volume_overlay(const device_music_ui_volume_widgets_t *widgets,
                                           int volume_percent)
{
    if (widgets == NULL) return;
    if (volume_percent < 0) volume_percent = 0;
    if (volume_percent > 100) volume_percent = 100;
    char value[8];
    snprintf(value, sizeof(value), "%d%%", volume_percent);
    if (widgets->value_label != NULL) {
        lv_label_set_text(widgets->value_label, value);
    }
    int fill = device_music_ui_progress_width(120, volume_percent);
    if (widgets->progress_fill != NULL) {
        lv_obj_set_width(widgets->progress_fill, fill > 0 ? fill : 1);
        lv_obj_set_style_bg_opa(widgets->progress_fill, fill > 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
}

void device_music_ui_draw_volume_overlay(lv_obj_t *screen,
                                         int volume_percent,
                                         device_music_ui_volume_widgets_t *widgets)
{
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    if (widgets != NULL) {
        widgets->value_label = NULL;
        widgets->progress_fill = NULL;
    }

    box(screen, 34, 86, 172, 112, 0x101216, 8);
    box(screen, 38, 90, 164, 104, 0x202637, 6);
    label(screen, "VOL", 54, 104, 132, 20, 18, theme->muted_text, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *value_label = label(screen, "0%", 54, 128, 132, 34, 24, theme->text, LV_TEXT_ALIGN_CENTER);

    box(screen, 60, 166, 120, 7, 0x343A46, 4);
    lv_obj_t *progress_fill = box(screen, 60, 166, 1, 7, theme->netease_red, 4);
    label(screen, "UP/DN   OK", 52, 178, 136, 14, 10, theme->subtle_text, LV_TEXT_ALIGN_CENTER);
    if (widgets != NULL) {
        widgets->value_label = value_label;
        widgets->progress_fill = progress_fill;
        device_music_ui_update_volume_overlay(widgets, volume_percent);
    }
}
