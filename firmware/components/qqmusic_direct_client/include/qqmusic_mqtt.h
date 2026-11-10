#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define QQMUSIC_MQTT_PACKET_MAX 8192
#define QQMUSIC_QR_ID_MAX 128

/* 独立实现的MQTT 5有限子集。WebSocket/TLS由平台传输层处理。 */
typedef enum { QQMUSIC_MQTT_OK, QQMUSIC_MQTT_MORE, QQMUSIC_MQTT_INVALID,
               QQMUSIC_MQTT_TOO_LARGE, QQMUSIC_MQTT_UNSUPPORTED } qqmusic_mqtt_result_t;
typedef struct {
    uint8_t type, reason, qos;
    uint16_t packet_id, server_keepalive;
    bool has_server_keepalive;
    char server_reference[256];
    char event[32];
    const uint8_t *topic, *payload;
    size_t topic_length, payload_length, consumed;
} qqmusic_mqtt_packet_t;

qqmusic_mqtt_result_t qqmusic_mqtt_connect(const char *client_id,const char *qr_id,
                                          uint8_t *out,size_t capacity,size_t *length);
qqmusic_mqtt_result_t qqmusic_mqtt_subscribe(const char *qr_id,uint16_t packet_id,
                                            uint8_t *out,size_t capacity,size_t *length);
/* parse不保留输入指针；packet中的topic/payload视图仅在输入缓冲有效时使用。 */
qqmusic_mqtt_result_t qqmusic_mqtt_parse(const uint8_t *data,size_t length,
                                       qqmusic_mqtt_packet_t *packet);
