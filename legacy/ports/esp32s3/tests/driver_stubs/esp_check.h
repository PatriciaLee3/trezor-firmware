#pragma once
#include "esp_err.h"
#define ESP_RETURN_ON_ERROR(expr, tag, ...)        \
  do {                                             \
    esp_err_t test_result = (expr);                \
    if (test_result != ESP_OK) return test_result; \
  } while (0)
#define ESP_GOTO_ON_ERROR(expr, label, tag, ...) \
  do {                                           \
    esp_err_t test_result = (expr);              \
    if (test_result != ESP_OK) {                 \
      ret = test_result;                         \
      goto label;                                \
    }                                            \
  } while (0)
#define ESP_GOTO_ON_FALSE(condition, error, label, tag, ...) \
  do {                                                       \
    if (!(condition)) {                                      \
      ret = (error);                                         \
      goto label;                                            \
    }                                                        \
  } while (0)
#define ESP_RETURN_ON_FALSE(condition, error, tag, ...) \
  do {                                                  \
    if (!(condition)) return (error);                   \
  } while (0)
