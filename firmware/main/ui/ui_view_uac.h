/**
 * FAKE_POD_NANO USB 声卡专属界面 (UAC 极客仪表盘)
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t* ui_view_uac_create(void);
void ui_view_uac_update(void);

#ifdef __cplusplus
}
#endif
