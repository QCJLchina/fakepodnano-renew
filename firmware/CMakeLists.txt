# ============================================================
# FAKE_POD_NANO 固件 — 顶层 CMakeLists
# 芯片：ESP32-S3-WROOM-1-N16R8 (16MB Flash + 8MB Octal PSRAM)
# ============================================================
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fakepodnano)
