#pragma once
#include <stdbool.h>
#define portMAX_DELAY (~0u)
#define pdMS_TO_TICKS(ms) (ms)
typedef struct { bool held; } StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
