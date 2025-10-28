idf_component_register(
    SRCS "porting/fal_flash_esp32_port.c"
         "${CMAKE_CURRENT_LIST_DIR}/../../.pio/libdeps/heltec_wifi_lora_32_V3/FlashDB/port/fal/src/fal.c"
         "${CMAKE_CURRENT_LIST_DIR}/../../.pio/libdeps/heltec_wifi_lora_32_V3/FlashDB/port/fal/src/fal_flash.c"
         "${CMAKE_CURRENT_LIST_DIR}/../../.pio/libdeps/heltec_wifi_lora_32_V3/FlashDB/port/fal/src/fal_partition.c"
    INCLUDE_DIRS "inc"
                 "${CMAKE_CURRENT_LIST_DIR}/../../.pio/libdeps/heltec_wifi_lora_32_V3/FlashDB/port/fal/inc"
    PRIV_REQUIRES esp_partition freertos
)