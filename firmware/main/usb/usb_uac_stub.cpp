/**
 * MSC 模式固件下 usb_uac_service 的空实现（UAC 与 MSC 互斥）
 */
#include "sdkconfig.h"

#if CONFIG_FAKEPOD_USB_MSC_MODE

#include "usb_uac_service.h"

esp_err_t usb_uac_service_start(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t usb_uac_service_stop(void) { return ESP_OK; }
bool usb_uac_is_streaming(void) { return false; }
uint32_t usb_uac_get_current_sample_rate(void) { return 0; }

#endif
