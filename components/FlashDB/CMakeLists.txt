idf_component_register(
    SRCS "src/fdb.c"
         "src/fdb_kvdb.c"
         "src/fdb_tsdb.c"
         "src/fdb_utils.c"
         "port/fal/src/fal.c"
         "port/fal/src/fal_flash.c"
         "port/fal/src/fal_partition.c"
         "port/fal_flash_esp32_port.c"
    INCLUDE_DIRS "inc"
                 "port/fal/inc"
                 "port/fal/inc" # Add this line to ensure fal.h is found
    PRIV_REQUIRES esp_partition freertos
)