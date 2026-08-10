idf_component_register(
    SRCS "codex_ble_espidf.c"
    INCLUDE_DIRS "include"
    REQUIRES codex_control
    PRIV_REQUIRES bt esp_hid
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
