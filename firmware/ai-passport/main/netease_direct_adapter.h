#pragma once
#include "netease_client.h"
#include "netease_direct_client.h"

esp_err_t direct_adapter_init(void);
esp_err_t direct_adapter_restore(void);
esp_err_t direct_adapter_qr_start(netease_qr_start_t *out);
esp_err_t direct_adapter_qr_poll(const char *key, netease_qr_status_t *out);
esp_err_t direct_adapter_logout(void);
esp_err_t direct_adapter_playlists(netease_playlist_list_t *out);
esp_err_t direct_adapter_tracks(int64_t id, uint32_t limit, uint32_t offset, netease_track_list_t *out);
esp_err_t direct_adapter_url(int64_t id, int bitrate, netease_track_url_t *out);

void direct_adapter_nickname(char *out,size_t capacity);
