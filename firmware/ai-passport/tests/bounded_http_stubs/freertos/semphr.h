#pragma once
#include "FreeRTOS.h"
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage);
int xSemaphoreTake(SemaphoreHandle_t mutex,unsigned ticks);
int xSemaphoreGive(SemaphoreHandle_t mutex);
