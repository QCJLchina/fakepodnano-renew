# =============================================================================
# FAKE_POD_NANO host-side LVGL preview (sim/)
# =============================================================================
# Compiles the REAL firmware UI sources + the REAL LVGL 8.4 sources with MSVC
# and renders one 460x460 view into a BMP/PNG. Nothing under firmware/ is
# modified; ESP-IDF identifiers are provided by sim/stubs/.
#
# Usage (normally invoked through sim/build.ps1 — needs vcvars64 on PATH):
#   cmake -S sim -B sim/build -G Ninja
#   cmake --build sim/build
#   sim/build/sim_preview.exe home sim/out/home.bmp
# =============================================================================
cmake_minimum_required(VERSION 3.20)

project(fakepodnano_sim LANGUAGES C CXX)

# Host is Windows + MSVC only (no gcc/clang on the build machine).
if(NOT MSVC)
    message(FATAL_ERROR "sim/ requires MSVC (run through sim/build.ps1, which loads vcvars64.bat)")
endif()

# /O1 keeps the (large) LVGL + CJK-font TUs compiling quickly; the renderer is
# not performance critical.
set(CMAKE_C_FLAGS_RELEASE "/O1 /DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "/O1 /DNDEBUG")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)          # UI sources use C99/C++20 designated initializers
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --------------------------------------------------------------------- paths
get_filename_component(SIM_DIR   "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
get_filename_component(ROOT_DIR  "${SIM_DIR}/.." ABSOLUTE)
set(FW_DIR        "${ROOT_DIR}/firmware")
set(LVGL_DIR      "${FW_DIR}/managed_components/lvgl__lvgl")
set(MAIN_DIR      "${FW_DIR}/main")
set(UI_DIR        "${MAIN_DIR}/ui")
set(UI_FONT_DIR   "${UI_DIR}/fonts")
set(STUB_DIR      "${SIM_DIR}/stubs")

foreach(required
        "${LVGL_DIR}/src/lv_conf_internal.h"
        "${UI_DIR}/ui_manager.h"
        "${UI_DIR}/ui_view_home.cpp"
        "${STUB_DIR}/ui_manager_stub.cpp")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "sim/: expected source is missing: ${required}")
    endif()
endforeach()

# --------------------------------------------------------------- LVGL sources
# LVGL 8.4: every optional feature is #if-gated inside its own .c file, so the
# whole src/ tree can be compiled; disabling options in lv_conf.h makes the
# corresponding files compile to nothing.
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS "${LVGL_DIR}/src/*.c")
# 3rd-party libs (png/jpg/gif/freetype/...) are all disabled in lv_conf.h and
# would only produce empty objects — skip them to keep the build fast.
list(FILTER LVGL_SOURCES EXCLUDE REGEX "/src/extra/libs/")
list(FILTER LVGL_SOURCES EXCLUDE REGEX "/src/draw/(nxp|arm2d|stm32_dma2d|swm341_dma2d|sdl)/")

# ------------------------------------------------------------------ UI sources
# The view files are being rewritten concurrently: glob them so new/renamed
# views are picked up by an incremental rebuild without touching this file.
file(GLOB UI_VIEW_SOURCES CONFIGURE_DEPENDS "${UI_DIR}/ui_view_*.cpp")

set(UI_SOURCES ${UI_VIEW_SOURCES})

# Shared design-token helpers (ui_theme.cpp) and icon bitmaps (ui_icons.c).
if(EXISTS "${UI_DIR}/ui_theme.cpp")
    list(APPEND UI_SOURCES "${UI_DIR}/ui_theme.cpp")
endif()
if(EXISTS "${UI_DIR}/ui_icons.c")
    list(APPEND UI_SOURCES "${UI_DIR}/ui_icons.c")
endif()

# Real CJK fonts (compressed lv_font_fmt_txt). These files are ~13 MB of glyph
# data; /bigobj is required. They are optional so the harness still builds if a
# teammate moves them.
set(FONT_SOURCES "")
foreach(f IN ITEMS ui_font_cjk_16.c ui_font_cjk_20.c)
    if(EXISTS "${UI_FONT_DIR}/${f}")
        list(APPEND FONT_SOURCES "${UI_FONT_DIR}/${f}")
    endif()
endforeach()

if(NOT UI_VIEW_SOURCES)
    message(FATAL_ERROR "sim/: no firmware/main/ui/ui_view_*.cpp found")
endif()

# ------------------------------------------------- optional "关于本设备" adapter
# The settings team may export the about screen as its own create function
# (e.g. ui_view_settings_about_create / ui_view_about_create). We must not guess
# a signature, so we only generate an adapter when
#   1) a UI header declares a *no-argument* <...>about<...>_create function, and
#   2) that symbol is actually defined in a compiled .cpp.
# Otherwise the adapter returns NULL and sim/main.cpp falls back to rendering the
# settings screen and clicking its 关于 trigger (heuristic, no crash either way).
set(ABOUT_HEADER "")
set(ABOUT_CREATE "")
set(ABOUT_UPDATE "")

file(GLOB SIM_UI_HEADERS CONFIGURE_DEPENDS "${UI_DIR}/*.h")
file(GLOB SIM_UI_CPPS    CONFIGURE_DEPENDS "${UI_DIR}/*.cpp")

foreach(_hdr IN LISTS SIM_UI_HEADERS)
    file(READ "${_hdr}" _hdr_content)
    if(_hdr_content MATCHES "(ui_view_[A-Za-z0-9_]*about[A-Za-z0-9_]*_create)[ \t]*\\([ \t]*(void)?[ \t]*\\)")
        set(_sym "${CMAKE_MATCH_1}")
        set(_defined FALSE)
        foreach(_cpp IN LISTS SIM_UI_CPPS)
            file(READ "${_cpp}" _cpp_content)
            string(FIND "${_cpp_content}" "${_sym}(" _pos)
            if(NOT _pos EQUAL -1)
                set(_defined TRUE)
                break()
            endif()
        endforeach()
        if(_defined)
            set(ABOUT_HEADER "${_hdr}")
            set(ABOUT_CREATE "${_sym}")
            string(REGEX REPLACE "_create$" "_update" _upd "${_sym}")
            if(_hdr_content MATCHES "${_upd}[ \t]*\\([ \t]*(void)?[ \t]*\\)")
                set(ABOUT_UPDATE "${_upd}")
            endif()
        endif()
        break()
    endif()
endforeach()

set(ABOUT_ADAPTER "${CMAKE_CURRENT_BINARY_DIR}/gen/sim_about_adapter.cpp")
if(ABOUT_CREATE)
    get_filename_component(_about_hdr_name "${ABOUT_HEADER}" NAME)
    string(CONCAT _adapter "#include <lvgl.h>\n#include \"${_about_hdr_name}\"\n\n"
                 "extern \"C\" lv_obj_t *sim_about_create(void) { return ${ABOUT_CREATE}(); }\n")
    if(ABOUT_UPDATE)
        string(APPEND _adapter "extern \"C\" void sim_about_update(void) { ${ABOUT_UPDATE}(); }\n")
    else()
        string(APPEND _adapter "extern \"C\" void sim_about_update(void) {}\n")
    endif()
    message(STATUS "sim: about screen -> exported ${ABOUT_CREATE}() from ${_about_hdr_name}")
else()
    string(CONCAT _adapter "/* No exported about screen found: sim/main.cpp renders the\n"
                 " * settings screen and clicks its about trigger instead. */\n"
                 "#include <lvgl.h>\n\n"
                 "extern \"C\" lv_obj_t *sim_about_create(void) { return (lv_obj_t *)0; }\n"
                 "extern \"C\" void sim_about_update(void) {}\n")
    message(STATUS "sim: about screen -> no export found, using settings-screen fallback")
endif()
file(WRITE "${ABOUT_ADAPTER}" "${_adapter}")

# -------------------------------------------------------------------- stubs
file(GLOB STUB_SOURCES CONFIGURE_DEPENDS "${STUB_DIR}/*.cpp")

# ---------------------------------------------------------------- executable
add_executable(sim_preview
    "${SIM_DIR}/main.cpp"
    "${ABOUT_ADAPTER}"
    ${STUB_SOURCES}
    ${UI_SOURCES}
    ${FONT_SOURCES}
    ${LVGL_SOURCES}
)

target_include_directories(sim_preview PRIVATE
    # sim/ first: lv_conf.h and the ESP-IDF stub headers must win over anything
    # the firmware tree might pull in.
    "${SIM_DIR}"
    "${STUB_DIR}"
    "${UI_DIR}"
    "${UI_FONT_DIR}"
    "${MAIN_DIR}"
    "${MAIN_DIR}/services"
    "${MAIN_DIR}/audio"
    "${MAIN_DIR}/usb"
    "${MAIN_DIR}/drivers"
    "${MAIN_DIR}/board"
    "${MAIN_DIR}/third_party/tjpgd"
    "${LVGL_DIR}"
)

target_compile_definitions(sim_preview PRIVATE
    LV_CONF_INCLUDE_SIMPLE     # pick up sim/lv_conf.h
    LV_KCONFIG_IGNORE          # do not include the ESP-IDF Kconfig shim
    _CRT_SECURE_NO_WARNINGS
    _USE_MATH_DEFINES
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# /utf-8 is essential: the UI sources contain UTF-8 Chinese string literals and
# the CJK font .c files contain UTF-8 comments.
target_compile_options(sim_preview PRIVATE
    /utf-8
    /W0
    /bigobj
)

# Force-include the virtual-FS shim (stat/S_ISDIR) into the firmware UI sources
# only. /FI searches the include path, so no spaces-in-path quoting is needed.
set_source_files_properties(${UI_SOURCES} PROPERTIES
    COMPILE_OPTIONS "/FIsim_fs.h;/FIwin_compat.h")

message(STATUS "sim: ${LVGL_DIR}")
message(STATUS "sim: UI views  -> ${UI_VIEW_SOURCES}")
message(STATUS "sim: UI extra  -> ${UI_SOURCES}")
message(STATUS "sim: CJK fonts -> ${FONT_SOURCES}")
message(STATUS "sim: LVGL TUs  -> ${LVGL_SOURCES}")
