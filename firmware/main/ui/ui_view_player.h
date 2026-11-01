/**
 * FAKE_POD_NANO 主播放器界面
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t* ui_view_player_create(void);
void ui_view_player_update(void);

#ifdef __cplusplus
}
#endif
