#pragma once
#include <stddef.h>
#define MALLOC_CAP_DEFAULT 0
void *heap_caps_calloc(size_t count, size_t size, unsigned caps);
void heap_caps_free(void *ptr);
