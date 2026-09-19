#include "esp_log.h"
#include "port_error.h"

int trezor_main(void);

void app_main(void) {
  ESP_LOGI("trezor", "Legacy Trezor One is starting");
  (void)trezor_main();
  esp32s3_port_error("wallet task returned", __FILE__, __LINE__);
}
