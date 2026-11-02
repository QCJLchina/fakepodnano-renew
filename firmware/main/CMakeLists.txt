# ============================================================
# FAKE_POD_NANO main 组件
# ============================================================
set(MAIN_SRCS
         "main.cpp"
         "board/board_init.cpp"
         "drivers/lcd_co5300.cpp"
         "drivers/dac_cs43131.cpp"
         "drivers/dac_cs43131_v4.cpp"
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
         "services/button_service.cpp"
         "services/battery_service.cpp"
         "services/imu_service.cpp"
         "services/album_art.cpp"
         "third_party/tjpgd/tjpgd.c"
         "usb/usb_uac_service.cpp"
         "ui/ui_manager.cpp"
         "ui/ui_view_home.cpp"
         "ui/ui_view_player.cpp"
         "ui/ui_view_uac.cpp"
         "ui/ui_view_files.cpp"
         "ui/ui_view_settings.cpp"
         "ui/ui_view_usbmode.cpp"
         "ui/fonts/ui_font_cjk_16.c"
         "ui/fonts/ui_font_cjk_20.c"
)

if(CONFIG_FAKEPOD_USB_MSC_MODE)
    list(REMOVE_ITEM MAIN_SRCS "usb/usb_uac_service.cpp")
    list(APPEND MAIN_SRCS "usb/usb_uac_stub.cpp")
endif()

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS "."
                 "board"
                 "drivers"
                 "audio"
                 "usb"
                 "ui"
                 "services"
                 "third_party/tjpgd"
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
             app_update
             usb_msc_dev
)

# 阶段 2/3/4 加入后启用：
#             esp_lcd_touch esp_lcd_touch_cst820
#             lvgl
#             esp_tinyusb
