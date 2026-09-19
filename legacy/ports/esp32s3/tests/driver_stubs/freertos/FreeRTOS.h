#pragma once
#include <assert.h>
#include <stdint.h>
#define pdMS_TO_TICKS(ms) (ms)
#define portMUX_INITIALIZER_UNLOCKED {.owner = -1, .count = 0}
typedef struct {
  int owner;
  unsigned count;
} portMUX_TYPE;
static inline void test_enter(portMUX_TYPE *lock) {
  assert(lock->owner == -1 && lock->count == 0);
  lock->count++;
}
static inline void test_exit(portMUX_TYPE *lock) {
  assert(lock->count == 1);
  lock->count--;
}
#define portENTER_CRITICAL(lock) test_enter(lock)
#define portEXIT_CRITICAL(lock) test_exit(lock)
