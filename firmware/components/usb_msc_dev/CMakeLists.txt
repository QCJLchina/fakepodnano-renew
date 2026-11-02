# 解析 tinyusb 组件名（本地或托管）
idf_build_get_property(build_components BUILD_COMPONENTS)
if(tinyusb IN_LIST build_components)
    set(tinyusb_name tinyusb)
else()
    set(tinyusb_name espressif__tinyusb)
endif()

idf_component_register(SRCS "usb_msc_dev.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_sdmmc ${tinyusb_name} usb)

# 仅在 U盘(MSC)模式固件里，把本组件的 tusb 描述符挂到 tinyusb 库上
if(CONFIG_FAKEPOD_USB_MSC_MODE)
    idf_component_get_property(tusb_lib ${tinyusb_name} COMPONENT_LIB)
    target_include_directories(${tusb_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/tusb")
    target_sources(${tusb_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/tusb/usb_descriptors.c")
endif()
