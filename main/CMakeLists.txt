idf_component_register(SRCS "main.cpp" "bt_keyboard.cpp" "esp32-ps2dev.cpp" "serial_mouse.cpp" "${app_sources}"
                       INCLUDE_DIRS "../include"
                       REQUIRES esp_hid nvs_flash driver)
