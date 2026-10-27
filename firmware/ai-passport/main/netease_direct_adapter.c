#include "netease_direct_adapter.h"
#include "netease_direct_client.h"
#include "nvs.h"
#include <stdlib.h>
#include <string.h>

esp_err_t direct_adapter_init(void)
{
    const netease_direct_client_config_t config = {.timeout_ms = 15000};
    return netease_direct_client_init(&config);
}

esp_err_t direct_adapter_restore(void)
{
    netease_direct_session_t *s = malloc(sizeof(*s)); if (!s) return ESP_ERR_NO_MEM;
    esp_err_t err = netease_direct_session_load(s);
    memset(s, 0, sizeof(*s)); free(s);
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_INVALID_RESPONSE) return ESP_ERR_NOT_FOUND;
    if (err == ESP_OK) err = netease_direct_session_verify();
    if (err == NETEASE_DIRECT_ERR_AUTH) {
        esp_err_t clear = netease_direct_session_clear();
        return clear == ESP_OK ? NETEASE_DIRECT_ERR_AUTH : clear;
    }
    return err;
}

esp_err_t direct_adapter_qr_start(netease_qr_start_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    netease_direct_qr_t qr; esp_err_t err = netease_direct_qr_start(&qr);
    if (err != ESP_OK) return err;
    if (strlen(qr.key) >= sizeof(out->login_id) || strlen(qr.payload) >= sizeof(out->qr_payload)) return ESP_ERR_INVALID_SIZE;
    strcpy(out->login_id, qr.key); strcpy(out->qr_payload, qr.payload); out->expires_in_seconds = qr.expires_in_seconds; return ESP_OK;
}

esp_err_t direct_adapter_qr_poll(const char *key, netease_qr_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out)); netease_direct_qr_status_t status;
    esp_err_t err = netease_direct_qr_poll(key, &status); if (err != ESP_OK) return err;
    switch (status.state) {
    case NETEASE_DIRECT_QR_WAITING: out->state = NETEASE_QR_WAITING; break;
    case NETEASE_DIRECT_QR_SCANNED: out->state = NETEASE_QR_SCANNED; break;
    case NETEASE_DIRECT_QR_CONFIRMED: out->state = NETEASE_QR_CONFIRMED; break;
    case NETEASE_DIRECT_QR_EXPIRED: out->state = NETEASE_QR_EXPIRED; break;
    default: out->state = NETEASE_QR_FAILED; break;
    }
    strcpy(out->nickname, status.nickname); strcpy(out->message, status.message); return ESP_OK;
}

esp_err_t direct_adapter_logout(void) { return netease_direct_session_clear(); }

esp_err_t direct_adapter_playlists(netease_playlist_list_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    netease_direct_playlist_list_t *p = malloc(sizeof(*p)); if (!p) return ESP_ERR_NO_MEM;
    esp_err_t err = netease_direct_get_playlists(NULL, p);
    if (err == ESP_OK) {
        out->count = p->count;
        out->truncated = p->truncated;
        for (size_t i = 0; i < p->count; ++i) { out->items[i].id = p->items[i].id; out->items[i].track_count = p->items[i].track_count; strcpy(out->items[i].name, p->items[i].name); }
    }
    free(p); return err;
}

esp_err_t direct_adapter_tracks(int64_t id, uint32_t limit, uint32_t offset, netease_track_list_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    netease_direct_track_list_t *p = malloc(sizeof(*p)); if (!p) return ESP_ERR_NO_MEM;
    esp_err_t err = netease_direct_get_tracks(NULL, id, limit, offset, p);
    if (err == ESP_OK) {
        if (p->next_offset > INT32_MAX) err = ESP_ERR_INVALID_SIZE;
        else {
            out->count = p->count; out->playlist_id = p->playlist_id; out->has_next_offset = p->has_next_offset; out->next_offset = (int)p->next_offset;
            for (size_t i = 0; i < p->count; ++i) { out->items[i].id = p->items[i].id; out->items[i].duration_ms = p->items[i].duration_ms; strcpy(out->items[i].name, p->items[i].name); strcpy(out->items[i].artist, p->items[i].artist); }
        }
    }
    free(p); return err;
}

esp_err_t direct_adapter_url(int64_t id, int bitrate, netease_track_url_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    netease_direct_track_url_t url; esp_err_t err = netease_direct_get_track_url(NULL, id, bitrate, &url);
    if (err == ESP_OK) { out->track_id = url.track_id; out->expires_in_seconds = url.expires_in_seconds; strcpy(out->url, url.url); strcpy(out->format, url.format); }
    return err;
}

void direct_adapter_nickname(char *out,size_t capacity)
{
    netease_direct_session_t *session=malloc(sizeof(*session));
    if(!out || !capacity) { free(session);return; }
    out[0]=0;
    if(session && netease_direct_session_load(session)==ESP_OK) {
        size_t length=strlen(session->nickname);if(length>=capacity)length=capacity-1;
        memcpy(out,session->nickname,length);out[length]=0;
    }
    if(session)memset(session,0,sizeof(*session));
    free(session);
}
