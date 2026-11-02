/**
 * services_stub.cpp — fake implementations of the firmware services the UI
 * talks to. Data is chosen to match design_ref/*.png so the preview shows a
 * realistic screen (see sim/stubs/sim_sample_data.h).
 *
 * Headers below are the REAL firmware headers, so any signature the views use
 * stays in sync automatically; only the implementations are fake.
 */

#include "player_service.h"
#include "battery_service.h"
#include "audio_pipeline.h"
#include "imu_service.h"
#include "usb_uac_service.h"
#include "album_art.h"
#include "sd_library.h"
#include "dac_cs43131.h"
#include "sim_sample_data.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

/* -------------------------------------------------------------- env overrides */
static bool env_flag(const char *name, bool def)
{
    const char *v = getenv(name);
    if (!v || !*v) return def;
    return !(v[0] == '0' || v[0] == 'f' || v[0] == 'F' || v[0] == 'n' || v[0] == 'N');
}

/* UAC active by default? The lead's sample data says "not started"; set
 * SIM_UAC_ACTIVE=1 (render.ps1 -UacActive) to preview the streaming screen. */
static bool uac_active(void) { return env_flag("SIM_UAC_ACTIVE", false); }

/* ------------------------------------------------------------- player service */
static player_state_t s_state = PLAYER_STATE_PLAYING;
static bool s_state_init = false;
static uint8_t s_player_volume = SIM_VOLUME_PCT;
static play_mode_t s_play_mode = PLAY_MODE_REPEAT_ALL;

extern "C" void player_set_play_mode(play_mode_t mode) { s_play_mode = mode; }
extern "C" play_mode_t player_get_play_mode(void) { return s_play_mode; }

static void init_player_state()
{
    if (s_state_init) return;
    s_state_init = true;
    const char *v = getenv("SIM_PLAYER_STATE");
    if (v && *v) {
        if (!strcmp(v, "paused"))       s_state = PLAYER_STATE_PAUSED;
        else if (!strcmp(v, "stopped")) s_state = PLAYER_STATE_STOPPED;
        else                            s_state = PLAYER_STATE_PLAYING;
    }
    const char *vol = getenv("SIM_VOLUME");
    if (vol && *vol) s_player_volume = (uint8_t)atoi(vol);
}

extern "C" esp_err_t player_service_init(void) { init_player_state(); return ESP_OK; }

extern "C" esp_err_t player_play(const char *filepath)
{
    (void)filepath;
    init_player_state();
    s_state = PLAYER_STATE_PLAYING;
    return ESP_OK;
}

extern "C" esp_err_t player_pause(void)  { s_state = PLAYER_STATE_PAUSED;  return ESP_OK; }
extern "C" esp_err_t player_resume(void) { s_state = PLAYER_STATE_PLAYING; return ESP_OK; }
extern "C" esp_err_t player_stop(void)   { s_state = PLAYER_STATE_STOPPED; return ESP_OK; }
extern "C" esp_err_t player_next(void)   { return ESP_OK; }
extern "C" esp_err_t player_prev(void)   { return ESP_OK; }
extern "C" esp_err_t player_seek(uint32_t sec) { (void)sec; return ESP_OK; }

extern "C" esp_err_t player_set_volume(uint8_t vol)
{
    s_player_volume = vol > 100 ? 100 : vol;
    return ESP_OK;
}

extern "C" void player_get_status(player_status_t *out)
{
    if (!out) return;
    init_player_state();
    memset(out, 0, sizeof(*out));
    out->state = s_state;
    snprintf(out->current_path, sizeof(out->current_path), "%s", SIM_TRACK_PATH);

    out->track_info.type = DECODER_TYPE_FLAC;
    out->track_info.fmt.sample_rate = (uint32_t)SIM_TRACK_KHZ * 1000u;
    out->track_info.fmt.bits_per_sample = SIM_TRACK_BITS;
    out->track_info.fmt.channels = 2;
    out->track_info.total_time_sec = SIM_TRACK_TOTAL_SEC;
    out->track_info.current_time_sec = SIM_TRACK_POS_SEC;
    out->track_info.bit_rate = 2304; /* kbps, plausible for 96/24 FLAC */
    out->track_info.is_dop_dsd = false;

    out->current_time_sec = SIM_TRACK_POS_SEC;
    out->total_time_sec = SIM_TRACK_TOTAL_SEC;
    out->volume = s_player_volume;
}

/* ------------------------------------------------------------ battery service */
extern "C" esp_err_t battery_service_init(void) { return ESP_OK; }
extern "C" int battery_percent(void) { return SIM_BATTERY_PCT; }

/* ------------------------------------------------------------ audio pipeline */
static audio_owner_t s_owner = AUDIO_OWNER_PLAYER;
static uint8_t s_pipeline_volume = SIM_VOLUME_PCT;
static bool s_mute = false;
static int s_eq = 0;

extern "C" esp_err_t audio_pipeline_acquire(audio_owner_t who)
{
    if (s_owner != AUDIO_OWNER_NONE && s_owner != who) return ESP_ERR_INVALID_STATE;
    s_owner = who;
    return ESP_OK;
}

extern "C" void audio_pipeline_release(audio_owner_t who)
{
    if (s_owner == who) s_owner = AUDIO_OWNER_NONE;
}

extern "C" audio_owner_t audio_pipeline_owner(void) { return s_owner; }
extern "C" esp_err_t audio_pipeline_init(void) { return ESP_OK; }
extern "C" esp_err_t audio_pipeline_reconfig(const audio_format_t *fmt) { (void)fmt; return ESP_OK; }

extern "C" esp_err_t audio_pipeline_write(const void *data, size_t size, size_t *written, uint32_t timeout_ms)
{
    (void)data; (void)timeout_ms;
    if (written) *written = size;
    return ESP_OK;
}

extern "C" esp_err_t audio_pipeline_flush(void) { return ESP_OK; }

extern "C" esp_err_t audio_pipeline_set_volume(uint8_t volume)
{
    s_pipeline_volume = volume > 100 ? 100 : volume;
    return ESP_OK;
}

extern "C" uint8_t audio_pipeline_get_volume(void) { return s_pipeline_volume; }

extern "C" esp_err_t audio_pipeline_set_mute(bool mute) { s_mute = mute; return ESP_OK; }
extern "C" esp_err_t audio_pipeline_set_eq(int preset) { s_eq = preset; return ESP_OK; }
extern "C" int audio_pipeline_get_eq(void) { return s_eq; }

extern "C" esp_err_t audio_pipeline_play_test_tone(uint32_t freq_hz, uint32_t ms, uint8_t volume_pct)
{
    (void)freq_hz; (void)ms; (void)volume_pct;
    fprintf(stderr, "[sim] audio_pipeline_play_test_tone(%lu Hz, %lu ms) - silent on host\n",
            (unsigned long)freq_hz, (unsigned long)ms);
    return ESP_OK;
}

/* --------------------------------------------------------------- imu service */
static bool s_imu_enabled = true;

extern "C" esp_err_t imu_service_start(void) { return ESP_OK; }
extern "C" void imu_service_set_enabled(bool enabled) { s_imu_enabled = enabled; }
extern "C" bool imu_service_get_enabled(void) { return s_imu_enabled; }
extern "C" bool imu_service_present(void) { return true; }

/* --------------------------------------------------------------- USB UAC svc */
static bool s_uac_started = false;

extern "C" esp_err_t usb_uac_service_start(void)  { s_uac_started = true;  return ESP_OK; }
extern "C" esp_err_t usb_uac_service_stop(void)   { s_uac_started = false; return ESP_OK; }
extern "C" bool usb_uac_service_is_started(void)  { return uac_active() || s_uac_started; }
extern "C" uint64_t usb_uac_get_rx_bytes(void)    { return (uac_active() || s_uac_started) ? SIM_UAC_RX_BYTES : 0; }
extern "C" bool usb_uac_is_streaming(void)        { return uac_active() || s_uac_started; }

extern "C" uint32_t usb_uac_get_current_sample_rate(void)
{
    return (uac_active() || s_uac_started) ? (uint32_t)SIM_UAC_SAMPLE_RATE : 0;
}

/* --------------------------------------------------------------- album art */
extern "C" bool album_art_get(const char *path, lv_img_dsc_t *out_dsc)
{
    (void)path; (void)out_dsc;
    return false; /* no embedded cover -> the view falls back to its vinyl/disc art */
}

extern "C" void album_art_release(void) {}

/* --------------------------------------------------------------- sd library */
extern "C" esp_err_t sd_library_mount(void) { return ESP_OK; }

extern "C" esp_err_t sd_library_first_track(char *out_path, size_t out_len)
{
    if (!out_path || out_len < 2) return ESP_ERR_INVALID_ARG;
    snprintf(out_path, out_len, "%s", SIM_TRACK_PATH);
    return ESP_OK;
}

/* ------------------------------------------------------------- CS43131 DAC */
extern "C" esp_err_t cs43131_init(i2c_master_bus_handle_t bus) { (void)bus; return ESP_OK; }
extern "C" esp_err_t cs43131_init_v2(i2c_master_bus_handle_t bus, uint32_t sr, uint8_t bits)
{
    (void)bus; (void)sr; (void)bits; return ESP_OK;
}
extern "C" esp_err_t cs43131_init_v4(i2c_master_bus_handle_t bus, uint32_t sr) { (void)bus; (void)sr; return ESP_OK; }
extern "C" esp_err_t cs43131_asp_try(int idx) { (void)idx; return ESP_OK; }
extern "C" int cs43131_asp_count(void) { return 6; }
extern "C" esp_err_t cs43131_clock_try(int idx) { (void)idx; return ESP_OK; }
extern "C" int cs43131_clock_count(void) { return 3; }
extern "C" esp_err_t cs43131_set_stream(uint32_t sample_rate, uint8_t bits)
{
    (void)sample_rate; (void)bits; return ESP_OK;
}
extern "C" esp_err_t cs43131_set_word_bits(uint8_t bits) { (void)bits; return ESP_OK; }
extern "C" void cs43131_set_i2c_dev_handle(i2c_master_dev_handle_t dev) { (void)dev; }

extern "C" esp_err_t cs43131_read_chip_id(i2c_master_bus_handle_t bus, uint8_t *id)
{
    (void)bus;
    if (id) *id = 0x31;
    return ESP_OK;
}

extern "C" esp_err_t cs43131_set_sample_rate(i2c_master_bus_handle_t bus, uint32_t sr)
{
    (void)bus; (void)sr; return ESP_OK;
}
extern "C" esp_err_t cs43131_set_sample_bits(i2c_master_bus_handle_t bus, uint8_t bits)
{
    (void)bus; (void)bits; return ESP_OK;
}
extern "C" esp_err_t cs43131_set_volume(i2c_master_bus_handle_t bus, uint8_t volume)
{
    (void)bus; (void)volume; return ESP_OK;
}
extern "C" esp_err_t cs43131_set_mute(i2c_master_bus_handle_t bus, bool mute)
{
    (void)bus; (void)mute; return ESP_OK;
}
extern "C" esp_err_t cs43131_hw_reset(void) { return ESP_OK; }
