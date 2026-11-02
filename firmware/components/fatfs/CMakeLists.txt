# exFAT lives in ESP-IDF FatFs, but the SDK header leaves it off.
# Compile the same sources and put this component's ffconf.h ahead of the SDK copy.

set(idf_fatfs "$ENV{IDF_PATH}/components/fatfs")
file(TO_CMAKE_PATH "${idf_fatfs}" idf_fatfs)

idf_component_register(
    KCONFIG "${idf_fatfs}/Kconfig"
    SRCS
        "${idf_fatfs}/diskio/diskio.c"
        "${idf_fatfs}/diskio/diskio_rawflash.c"
        "${idf_fatfs}/diskio/diskio_wl.c"
        "${idf_fatfs}/diskio/diskio_sdmmc.c"
        "${idf_fatfs}/src/ff.c"
        "${idf_fatfs}/src/ffunicode.c"
        "${idf_fatfs}/port/freertos/ffsystem.c"
        "${idf_fatfs}/vfs/vfs_fat.c"
        "${idf_fatfs}/vfs/vfs_fat_sdmmc.c"
        "${idf_fatfs}/vfs/vfs_fat_spiflash.c"
    INCLUDE_DIRS
        "src"
        "${idf_fatfs}/diskio"
        "${idf_fatfs}/src"
        "${idf_fatfs}/vfs"
    REQUIRES
        wear_levelling
        sdmmc
        esp_driver_sdmmc
        esp_driver_sdspi
    PRIV_REQUIRES
        vfs
        esp_driver_gpio
)

# ff.c sits beside the SDK ffconf.h, so no include path can outrank it.
# Force this component's full ffconf.h in first; its _FFCONF_DEFINED
# guard then turns the SDK copy into a no-op. The verbatim ff.h copy in
# src/ points every other consumer at the same local ffconf.h.
target_compile_options(${COMPONENT_LIB} PRIVATE
    "-include" "${CMAKE_CURRENT_LIST_DIR}/src/ffconf.h"
)
