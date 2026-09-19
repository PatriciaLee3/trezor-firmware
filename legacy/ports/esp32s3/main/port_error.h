#ifndef ESP32S3_PORT_ERROR_H
#define ESP32S3_PORT_ERROR_H
#include <stdio.h>
#include "esp_err.h"
void __attribute__((noreturn)) esp32s3_port_error(const char *message,
                                                  const char *file, int line);
#define PORT_REQUIRE(condition, message) do { \
  if (!(condition)) esp32s3_port_error(message, __FILE__, __LINE__); \
} while (0)
#define PORT_CHECK(expression)                                              \
  do {                                                                      \
    const esp_err_t port_check_result = (expression);                        \
    if (port_check_result != ESP_OK) {                                       \
      char port_check_message[16];                                          \
      snprintf(port_check_message, sizeof(port_check_message), "0x%X",       \
               (unsigned int)port_check_result);                            \
      esp32s3_port_error(port_check_message, __FILE__, __LINE__);             \
    }                                                                       \
  } while (0)
#endif
