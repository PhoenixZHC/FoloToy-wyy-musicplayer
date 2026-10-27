#include "direct_stream.h"
#include "cJSON.h"
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { OTHER, ROOT, PLAYLIST_OBJECT, PLAYLIST_ARRAY, PLAYLIST_ITEM, IDS_ARRAY, ID_ITEM, SONG_ARRAY, SONG_ITEM, ARTISTS, ARTIST, ALBUM };
enum { KEY_OR_END, COLON, VALUE, COMMA_OR_END, REQUIRED };

void direct_stream_init(direct_stream_t *s, direct_stream_mode_t mode,
                        netease_direct_playlist_list_t *playlists, uint32_t offset, uint32_t limit)
{
    memset(s, 0, sizeof(*s)); s->mode = mode; s->playlists = playlists; s->offset = offset; s->limit = limit;
    if (playlists) memset(playlists, 0, sizeof(*playlists));
}

static bool number(const cJSON *j, int64_t *out)
{
    if (cJSON_IsNumber(j) && j->valuedouble >= 0 && j->valuedouble <= 9007199254740991.0 &&
        (double)(int64_t)j->valuedouble == j->valuedouble) { *out = (int64_t)j->valuedouble; return true; }
    return false;
}

static bool scalar_valid(const char *p)
{
    if (strcmp(p, "true") == 0 || strcmp(p, "false") == 0 || strcmp(p, "null") == 0) return true;
    if (*p == '-') ++p;
    if (*p == '0') ++p;
    else { if (*p < '1' || *p > '9') return false; while (isdigit((unsigned char)*p)) ++p; }
    if (*p == '.') { ++p; if (!isdigit((unsigned char)*p)) return false; while (isdigit((unsigned char)*p)) ++p; }
    if (*p == 'e' || *p == 'E') { ++p; if (*p == '+' || *p == '-') ++p; if (!isdigit((unsigned char)*p)) return false; while (isdigit((unsigned char)*p)) ++p; }
    return *p == 0;
}

static esp_err_t value_done(direct_stream_t *s)
{
    if (!s->depth) { if (s->done) return ESP_ERR_INVALID_RESPONSE; s->done = true; return ESP_OK; }
    direct_json_frame_t *f = &s->frames[s->depth - 1];
    if (f->kind == '{' ? f->state != VALUE : (f->state != KEY_OR_END && f->state != REQUIRED)) return ESP_ERR_INVALID_RESPONSE;
    f->state = COMMA_OR_END; return ESP_OK;
}

static esp_err_t token_done(direct_stream_t *s, bool is_string)
{
    direct_json_frame_t *f = s->depth ? &s->frames[s->depth - 1] : NULL;
    if (!f) return ESP_ERR_INVALID_RESPONSE;
    bool key = f->kind == '{' && (f->state == KEY_OR_END || f->state == REQUIRED);
    if (key && !is_string) return ESP_ERR_INVALID_RESPONSE;
    bool important = key || (f->role == ROOT && strcmp(f->key, "code") == 0) ||
        ((f->role == PLAYLIST_ITEM || f->role == ID_ITEM || f->role == SONG_ITEM || f->role == ARTIST) &&
         (strcmp(f->key, "id") == 0 || strcmp(f->key, "name") == 0 || strcmp(f->key, "dt") == 0 || strcmp(f->key, "trackCount") == 0));
    cJSON *j = NULL;
    if (!s->overflow) { s->token[s->token_len] = 0; if (!is_string && !scalar_valid(s->token)) return ESP_ERR_INVALID_RESPONSE; j = cJSON_ParseWithOpts(s->token, NULL, true); if (!j) return ESP_ERR_INVALID_RESPONSE; }
    else if (!is_string || (important && !key)) return ESP_ERR_INVALID_SIZE;
    if (key) {
        f->key[0] = 0;
        if (j && cJSON_IsString(j) && strlen(j->valuestring) < sizeof(f->key)) strcpy(f->key, j->valuestring);
        f->state = COLON;
    } else {
        int64_t n = 0;
        if (f->role == ROOT && strcmp(f->key, "code") == 0) {
            if (number(j, &n) && (n == 301 || n == 302 || n == 401)) { cJSON_Delete(j); return NETEASE_DIRECT_ERR_AUTH; }
            if (!number(j, &n) || n != 200 || s->code_ok) { cJSON_Delete(j); return ESP_ERR_INVALID_RESPONSE; }
            s->code_ok = true;
        } else if (f->role == PLAYLIST_ARRAY || f->role == IDS_ARRAY || f->role == SONG_ARRAY) {
            cJSON_Delete(j); return ESP_ERR_INVALID_RESPONSE;
        } else if (f->role == ALBUM && strcmp(f->key, "picUrl") == 0) {
            if(cJSON_IsString(j) && strlen(j->valuestring)<sizeof(s->cover_url))
                strcpy(s->cover_url,j->valuestring);
        } else if (f->role == SONG_ITEM || f->role == ARTIST) {
            if (f->role == SONG_ITEM && strcmp(f->key, "id") == 0) {
                if (!number(j, &n) || n <= 0 || (f->fields & 1)) { cJSON_Delete(j); return ESP_ERR_INVALID_RESPONSE; }
                s->track.id = n; f->fields |= 1;
            } else if (f->role == SONG_ITEM && strcmp(f->key, "dt") == 0) {
                if (!number(j, &n) || n > INT_MAX || (f->fields & 4)) { cJSON_Delete(j); return ESP_ERR_INVALID_RESPONSE; }
                s->track.duration_ms = (int)n; f->fields |= 4;
            } else if (strcmp(f->key, "name") == 0) {
                if (!cJSON_IsString(j) || (f->fields & 2)) { cJSON_Delete(j); return ESP_ERR_INVALID_RESPONSE; }
                char *dest = f->role == SONG_ITEM ? s->track.name : s->track.artist;
                size_t cap = f->role == SONG_ITEM ? sizeof(s->track.name) : sizeof(s->track.artist);
                if (f->role == SONG_ITEM || !dest[0]) {
                    size_t len = strlen(j->valuestring);
                    if (len >= cap) { len = cap - 1; while (len && ((unsigned char)j->valuestring[len] & 0xc0) == 0x80) --len; }
                    memcpy(dest, j->valuestring, len); dest[len] = 0;
                }
                f->fields |= 2;
            }
        } else if (f->role == PLAYLIST_ITEM || f->role == ID_ITEM) {
            if (strcmp(f->key, "id") == 0) {
                if (!number(j, &n) || n <= 0 || (f->fields & 1)) { cJSON_Delete(j); return ESP_ERR_INVALID_RESPONSE; }
                s->item.id = n; f->fields |= 1;
            } else if (f->role == PLAYLIST_ITEM && strcmp(f->key, "trackCount") == 0) {
                if (!number(j, &n) || n > INT_MAX || (f->fields & 4)) { cJSON_Delete(j); return ESP_ERR_INVALID_RESPONSE; }
                s->item.track_count = (int)n; f->fields |= 4;
            } else if (f->role == PLAYLIST_ITEM && strcmp(f->key, "name") == 0) {
                if (!cJSON_IsString(j) || (f->fields & 2)) { cJSON_Delete(j); return ESP_ERR_INVALID_RESPONSE; }
                size_t len = strlen(j->valuestring);
                if (len >= sizeof(s->item.name)) { len = sizeof(s->item.name) - 1; while (len && ((unsigned char)j->valuestring[len] & 0xc0) == 0x80) --len; }
                memcpy(s->item.name, j->valuestring, len); s->item.name[len] = 0; f->fields |= 2;
            }
        }
        esp_err_t err = value_done(s); if (err != ESP_OK) { cJSON_Delete(j); return err; }
    }
    cJSON_Delete(j); s->token_len = 0; s->overflow = false; return ESP_OK;
}

static esp_err_t open_container(direct_stream_t *s, char kind)
{
    if (s->depth >= sizeof(s->frames) / sizeof(s->frames[0]) || s->done) return ESP_ERR_INVALID_SIZE;
    unsigned role = OTHER;
    if (!s->depth) { if (kind != '{') return ESP_ERR_INVALID_RESPONSE; role = ROOT; }
    else {
        direct_json_frame_t *p = &s->frames[s->depth - 1];
        if (p->kind == '{' ? p->state != VALUE : (p->state != KEY_OR_END && p->state != REQUIRED)) return ESP_ERR_INVALID_RESPONSE;
        if ((p->role == ROOT && strcmp(p->key, "code") == 0) ||
            ((p->role == PLAYLIST_ITEM || p->role == ID_ITEM || p->role == SONG_ITEM || p->role == ARTIST) &&
             (strcmp(p->key, "id") == 0 || strcmp(p->key, "name") == 0 || strcmp(p->key, "dt") == 0 || strcmp(p->key, "trackCount") == 0))) return ESP_ERR_INVALID_RESPONSE;
        if (p->role == ROOT && strcmp(p->key, "playlist") == 0) {
            role = s->mode == DIRECT_STREAM_PLAYLISTS ? PLAYLIST_ARRAY : PLAYLIST_OBJECT;
            if ((role == PLAYLIST_ARRAY ? '[' : '{') != kind) return ESP_ERR_INVALID_RESPONSE;
        } else if (p->role == ROOT && strcmp(p->key, "songs") == 0 && s->mode == DIRECT_STREAM_SONGS) {
            if (kind != '[') return ESP_ERR_INVALID_RESPONSE;
            role = SONG_ARRAY;
        } else if (p->role == SONG_ARRAY) {
            if (kind != '{') return ESP_ERR_INVALID_RESPONSE;
            role = SONG_ITEM; memset(&s->track, 0, sizeof(s->track));
        } else if (p->role == SONG_ITEM && strcmp(p->key, "al") == 0 && kind == '{') {
            role = ALBUM;
        } else if (p->role == SONG_ITEM && strcmp(p->key, "ar") == 0) {
            if (kind != '[') return ESP_ERR_INVALID_RESPONSE;
            role = ARTISTS;
        } else if (p->role == ARTISTS) {
            if (kind != '{') return ESP_ERR_INVALID_RESPONSE;
            role = ARTIST;
        } else if (p->role == PLAYLIST_OBJECT && strcmp(p->key, "trackIds") == 0) {
            if (kind != '[') return ESP_ERR_INVALID_RESPONSE;
            role = IDS_ARRAY;
        } else if (p->role == PLAYLIST_ARRAY || p->role == IDS_ARRAY) {
            if (kind != '{') return ESP_ERR_INVALID_RESPONSE;
            role = p->role == PLAYLIST_ARRAY ? PLAYLIST_ITEM : ID_ITEM; memset(&s->item, 0, sizeof(s->item));
        }
    }
    if (role == PLAYLIST_ARRAY || role == IDS_ARRAY || role == SONG_ARRAY) { if (s->found) return ESP_ERR_INVALID_RESPONSE; s->found = true; }
    direct_json_frame_t *f = &s->frames[s->depth++]; memset(f, 0, sizeof(*f)); f->kind = kind; f->role = role; return ESP_OK;
}

static esp_err_t close_container(direct_stream_t *s, char kind)
{
    if (!s->depth) return ESP_ERR_INVALID_RESPONSE;
    direct_json_frame_t *f = &s->frames[s->depth - 1];
    if ((kind == '}' ? '{' : '[') != f->kind || (f->state != KEY_OR_END && f->state != COMMA_OR_END)) return ESP_ERR_INVALID_RESPONSE;
    if (f->role == PLAYLIST_ITEM) {
        if (f->fields != 7 || !s->playlists) return ESP_ERR_INVALID_RESPONSE;
        if (s->playlists->count >= NETEASE_DIRECT_MAX_PLAYLISTS) s->playlists->truncated = true;
        else s->playlists->items[s->playlists->count++] = s->item;
    } else if (f->role == ID_ITEM) {
        if (!(f->fields & 1)) return ESP_ERR_INVALID_RESPONSE;
        if (s->total >= s->offset && s->id_count < s->limit && s->id_count < NETEASE_DIRECT_MAX_TRACKS) s->ids[s->id_count++] = s->item.id;
        if (s->all_ids && s->total >= s->all_ids_offset && s->total - s->all_ids_offset < s->all_ids_capacity)
            s->all_ids[s->total - s->all_ids_offset] = s->item.id;
        if (s->total == UINT32_MAX) return ESP_ERR_INVALID_SIZE;
        ++s->total;
    } else if (f->role == SONG_ITEM) {
        if ((f->fields & 7) != 7 || !s->tracks) return ESP_ERR_INVALID_RESPONSE;
        if (s->tracks->count >= NETEASE_DIRECT_MAX_TRACKS) return ESP_ERR_INVALID_SIZE;
        s->tracks->items[s->tracks->count++] = s->track;
    }
    --s->depth; return value_done(s);
}

static void append(direct_stream_t *s, char c)
{
    if (s->token_len + 1 < sizeof(s->token)) s->token[s->token_len++] = c; else s->overflow = true;
}

esp_err_t direct_stream_feed(direct_stream_t *s, const char *data, size_t length)
{
    if (!s || (!data && length)) return ESP_ERR_INVALID_ARG;
    if (s->error != ESP_OK) return s->error;
    if (length > 4 * 1024 * 1024 - s->bytes) return s->error = ESP_ERR_INVALID_SIZE;
    s->bytes += length;
    for (size_t i = 0; i < length && s->error == ESP_OK; ++i) {
        unsigned char c = (unsigned char)data[i];
        if (s->string) {
            append(s, (char)c);
            if (s->unicode_left) { if (!isxdigit(c)) s->error = ESP_ERR_INVALID_RESPONSE; else --s->unicode_left; }
            else if (s->escape) { s->escape = false; if (c == 'u') s->unicode_left = 4; else if (!strchr("\"\\/bfnrt", c)) s->error = ESP_ERR_INVALID_RESPONSE; }
            else if (c == '\\') s->escape = true;
            else if (c == '"') { s->string = false; s->error = token_done(s, true); }
            else if (c < 0x20) s->error = ESP_ERR_INVALID_RESPONSE;
            continue;
        }
        if (s->scalar) {
            if (isspace(c) || c == ',' || c == ']' || c == '}') { s->scalar = false; s->error = token_done(s, false); if (s->error != ESP_OK) break; }
            else { append(s, (char)c); continue; }
        }
        if (isspace(c)) continue;
        direct_json_frame_t *f = s->depth ? &s->frames[s->depth - 1] : NULL;
        if (c == '{' || c == '[') s->error = open_container(s, (char)c);
        else if (c == '}' || c == ']') s->error = close_container(s, (char)c);
        else if (c == ':') { if (!f || f->kind != '{' || f->state != COLON) s->error = ESP_ERR_INVALID_RESPONSE; else f->state = VALUE; }
        else if (c == ',') { if (!f || f->state != COMMA_OR_END) s->error = ESP_ERR_INVALID_RESPONSE; else f->state = REQUIRED; }
        else if (c == '"') {
            if (!f || (f->kind == '{' ? (f->state != VALUE && f->state != KEY_OR_END && f->state != REQUIRED) : (f->state != KEY_OR_END && f->state != REQUIRED))) s->error = ESP_ERR_INVALID_RESPONSE;
            else { s->string = true; s->token_len = 0; s->overflow = false; append(s, '"'); }
        } else {
            if (!f || (f->kind == '{' ? f->state != VALUE : (f->state != KEY_OR_END && f->state != REQUIRED))) s->error = ESP_ERR_INVALID_RESPONSE;
            else { s->scalar = true; s->token_len = 0; s->overflow = false; append(s, (char)c); }
        }
    }
    return s->error;
}

esp_err_t direct_stream_finish(direct_stream_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    if (s->error != ESP_OK || !s->done || s->depth || s->string || s->scalar || !s->found || !s->code_ok) {
        if (s->playlists) memset(s->playlists, 0, sizeof(*s->playlists));
        if (s->tracks) memset(s->tracks, 0, sizeof(*s->tracks));
        s->id_count = 0;
        return s->error != ESP_OK ? s->error : ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
