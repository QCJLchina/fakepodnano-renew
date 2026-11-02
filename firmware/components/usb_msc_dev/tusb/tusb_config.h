/* TinyUSB 配置 —— 纯 MSC（U盘）模式 */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "sdkconfig.h"

/* RHPort 模式 */
#ifdef CONFIG_TINYUSB_RHPORT_HS
#  if CONFIG_IDF_TARGET_ESP32P4
#    define CFG_TUSB_RHPORT1_MODE  OPT_MODE_DEVICE | OPT_MODE_HIGH_SPEED
#  else
#    define CFG_TUSB_RHPORT0_MODE  OPT_MODE_DEVICE | OPT_MODE_HIGH_SPEED
#  endif
#else
#  define CFG_TUSB_RHPORT0_MODE    OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED
#endif

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_FREERTOS
#endif

#ifndef ESP_PLATFORM
#define ESP_PLATFORM 1
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

#if TU_CHECK_MCU(OPT_MCU_ESP32S2, OPT_MCU_ESP32S3, OPT_MCU_ESP32P4, OPT_MCU_ESP32S31, OPT_MCU_ESP32H4)
#define CFG_TUSB_OS_INC_PATH    freertos/
#endif

#define CFG_TUD_ENABLED       1

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN        __attribute__ ((aligned(4)))
#endif

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

/* ---- MSC 类 ---- */
#define CFG_TUD_MSC               1
#define CFG_TUD_MSC_EP_BUFSIZE    512

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
