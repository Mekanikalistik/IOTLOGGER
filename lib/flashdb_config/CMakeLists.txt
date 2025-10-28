idf_component_register(
    SRCS "src/fal_flash_esp32_port.c"
         "../../.pio/libdeps/heltec_wifi_lora_32_V3/FlashDB/port/fal/src/fal.c"
         "../../.pio/libdeps/heltec_wifi_lora_32_V3/FlashDB/port/fal/src/fal_flash.c"
         "../../.pio/libdeps/heltec_wifi_lora_32_V3/FlashDB/port/fal/src/fal_partition.c"
    INCLUDE_DIRS "include"
                 "../../.pio/libdeps/heltec_wifi_lora_32_V3/FlashDB/port/fal/inc"
    PRIV_INCLUDE_DIRS ""
    PRIV_REQUIRES esp_partition freertos
    REQUIRES
)