# The slave image is embedded in the app (no WiFi, no extra partition needed).
# Default: devices/esp32-c6/firmware/network_adapter_esp32c6-2.12.12.bin (gitignored,
# fetched from https://esphome.github.io/esp-hosted-firmware/v2.12.12/, SHA-256 checked
# in the README). Override with -D C6_IMAGE=<path>.
if(NOT DEFINED C6_IMAGE)
    set(C6_IMAGE ${CMAKE_CURRENT_LIST_DIR}/../../../devices/esp32-c6/firmware/network_adapter_esp32c6-2.12.12.bin)
endif()
get_filename_component(C6_IMAGE_ABS ${C6_IMAGE} ABSOLUTE)
if(NOT EXISTS ${C6_IMAGE_ABS} AND NOT CMAKE_BUILD_EARLY_EXPANSION)
    message(FATAL_ERROR "C6 slave image not found: ${C6_IMAGE_ABS} (see README.md)")
endif()
# Fixed basename so main.c can reference _binary_slave_image_bin_start/_end
set(SLAVE_IMAGE ${CMAKE_CURRENT_BINARY_DIR}/slave_image.bin)
if(EXISTS ${C6_IMAGE_ABS})
    configure_file(${C6_IMAGE_ABS} ${SLAVE_IMAGE} COPYONLY)
endif()

idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_hosted esp_timer app_update nvs_flash esp_event
                       EMBED_FILES ${SLAVE_IMAGE})
