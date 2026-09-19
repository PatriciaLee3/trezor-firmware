#pragma once
#include <stdint.h>
typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return (void *)1; }
static inline int xSemaphoreTakeRecursive(SemaphoreHandle_t m, uint32_t ticks) { return 1; }
static inline int xSemaphoreGiveRecursive(SemaphoreHandle_t m) { return 1; }
