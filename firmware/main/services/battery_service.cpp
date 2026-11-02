/**
 * FAKE_POD_NANO 电池电量检测实现
 */

#include "battery_service.h"
#include "board_config.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "BAT";

static adc_oneshot_unit_handle_t s_adc = NULL;
static bool s_ready = false;

/* GPIO1 -> ADC1_CH0；电池经 1/2 分压进 ADC */
#define BAT_ADC_CHANNEL   ADC_CHANNEL_0
#define BAT_DIVIDER       2.0f
#define BAT_V_EMPTY       3.30f
#define BAT_V_FULL        4.20f

esp_err_t battery_service_init(void)
{
    if (s_ready) return ESP_OK;

    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = ADC_UNIT_1;
    unit_cfg.clk_src = ADC_RTC_CLK_SRC_DEFAULT;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC 单元初始化失败: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_12;
    err = adc_oneshot_config_channel(s_adc, BAT_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC 通道配置失败: %s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;
    ESP_LOGI(TAG, "电池检测已就绪 (GPIO%d / ADC1_CH0)", BAT_ADC_PIN);
    return ESP_OK;
}

int battery_percent(void)
{
    if (!s_ready || !s_adc) return -1;

    int raw = 0;
    if (adc_oneshot_read(s_adc, BAT_ADC_CHANNEL, &raw) != ESP_OK) return -1;

    float v_adc = (float)raw / 4095.0f * 3.3f;   /* ADC 引脚电压 */
    float v_bat = v_adc * BAT_DIVIDER;            /* 还原电池电压 */

    int pct = (int)((v_bat - BAT_V_EMPTY) / (BAT_V_FULL - BAT_V_EMPTY) * 100.0f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    ESP_LOGI(TAG, "BAT raw=%d adc=%.3fV vbat=%.3fV(按1/2分压) pct=%d%%", raw, v_adc, v_bat, pct);
    return pct;
}
