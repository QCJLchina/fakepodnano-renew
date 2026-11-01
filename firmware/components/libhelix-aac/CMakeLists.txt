# ============================================================
# libhelix-aac 组件
# ============================================================
file(GLOB HELIX_SRCS "*.c")

idf_component_register(
    SRCS ${HELIX_SRCS}
    INCLUDE_DIRS "."
)

# 关闭部分过于严格的编译器警告，确保定点数汇编/宏顺利编译
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    USE_DEFAULT_STDLIB
    ARDUINO
)
