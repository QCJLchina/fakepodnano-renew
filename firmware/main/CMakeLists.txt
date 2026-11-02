# ============================================================
# FAKE_POD_NANO main 组件
# ============================================================
idf_component_register(
    SRCS "main.cpp"
         "board/board_init.cpp"
         "drivers/lcd_co5300.cpp"
         "drivers/dac_cs43131.cpp"
         "audio/audio_pipeline.cpp"
         "audio/decoder_base.cpp"
         "audio/decoder_wav.cpp"
         "audio/decoder_flac.cpp"
         "audio/decoder_mp3.cpp"
         "audio/decoder_dsd.cpp"
         "audio/decoder_aac.cpp"
         "services/player_service.cpp"
         "services/sd_library.cpp"
         "services/ble_time_sync.cpp"
         "usb/usb_uac_service.cpp"
         "ui/ui_manager.cpp"
         "ui/ui_view_player.cpp"
         "ui/ui_view_uac.cpp"
    INCLUDE_DIRS "."
                 "board"
                 "drivers"
                 "audio"
                 "usb"
                 "ui"
                 "services"
    REQUIRES driver
             esp_driver_gpio
             esp_driver_spi
             esp_driver_i2c
             esp_driver_i2s
             esp_driver_sdmmc
             esp_driver_usb_serial_jtag
             esp_lcd
             esp_timer
             esp_adc
             esp_psram
             spi_flash
             nvs_flash
             fatfs
             esp_lcd_qspi_amoled
             esp_lcd_touch_cst820
             libhelix-aac
             usb_device_uac
             lvgl
             bt
)

# 阶段 2/3/4 加入后启用：
#             esp_lcd_touch esp_lcd_touch_cst820
#             lvgl
#             esp_tinyusb
