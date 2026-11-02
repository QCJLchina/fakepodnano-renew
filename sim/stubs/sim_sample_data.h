/**
 * Shared sample data for the host preview (fake SD card + fake player state).
 * Keep every "design-like" string in one place so all stubs agree.
 */
#pragma once

/* --- fake TF card root (see sim/stubs/fake_fs.cpp for the tree) --- */
#define SIM_SD_ROOT              "/sdcard"
#define SIM_TRACK_PATH           "/sdcard/欧美发烧经典原盘/加州旅馆 - Eagles (Live).flac"
#define SIM_TRACK_NAME           "加州旅馆 - Eagles (Live)"
#define SIM_TRACK_ARTIST         "Hotel California"
#define SIM_TRACK_ALBUM          "Eagles"
#define SIM_TRACK_KHZ            96
#define SIM_TRACK_BITS           24
#define SIM_TRACK_POS_SEC        105      /* 01:45 */
#define SIM_TRACK_TOTAL_SEC      390      /* 06:30 */
#define SIM_VOLUME_PCT           80
#define SIM_BATTERY_PCT          95

/* --- USB UAC / CS43131 --- */
#define SIM_UAC_SAMPLE_RATE      384000   /* design: "384.0 kHz" */
#define SIM_UAC_BITS             32
#define SIM_UAC_RX_BYTES         (123456789ULL)
#define SIM_DAC_NAME             "CS43131"
