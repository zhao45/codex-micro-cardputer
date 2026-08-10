idf_component_register(
    SRCS
        "app_main.cpp"
        "cardputer_board.cpp"
        "cardputer_keyboard.cpp"
        "cardputer_usb_codex.cpp"
        "cardputer_usb_mic.cpp"
        "cardputer_ui.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        codex_control
        codex_transport_ble_espidf
        nvs_flash
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_spi
        esp_adc
        esp_lcd
        esp_timer
        usb_device_uac
        espressif__tinyusb
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)

# usb_device_uac's composite mode deliberately leaves TinyUSB configuration and
# descriptors to the application. Make the Cardputer-specific configuration
# visible while continuing to reuse the component's UAC implementation.
idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/usb"
    "${CMAKE_CURRENT_LIST_DIR}/../components/usb_device_uac/tusb_uac"
)
target_include_directories(${tusb_lib} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../components/codex_control/include"
)
target_sources(${tusb_lib} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/usb/cardputer_usb_descriptors.c"
)
