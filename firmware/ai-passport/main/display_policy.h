#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DISPLAY_TIMEOUT_OPTION_COUNT 6
#define DISPLAY_DEFAULT_TIMEOUT_SECONDS 60
#define DISPLAY_DEFAULT_TIMEOUT_OPTION 3
#define DISPLAY_VALID_CLOCK_EPOCH INT64_C(1609459200)

typedef struct {
    uint16_t timeout_seconds;
    int64_t last_activity_us;
    bool screen_off;
} display_policy_t;

uint16_t display_timeout_seconds(size_t option);
size_t display_timeout_option(uint16_t seconds);
bool display_timeout_valid(uint16_t seconds);
void display_policy_init(display_policy_t *policy, uint16_t seconds, int64_t now_us);
void display_policy_activity(display_policy_t *policy, int64_t now_us);
bool display_policy_tick(display_policy_t *policy, int64_t now_us);
bool display_policy_wake(display_policy_t *policy, int64_t now_us);
bool display_policy_night(int local_hour, bool clock_valid, bool previous_night);
