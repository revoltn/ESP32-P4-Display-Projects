# Wake-word models (microWakeWord, main/wake_mww.cpp, active only with
# CONFIG_BEDSIDE_WAKE_MWW). To add one: put models/<id>.tflite + models/<id>.json
# in place, append <id> below, rebuild the build_mww variant and app-flash.
# <id> is what settings/API store (voice_wake_word); the name shown in the
# Settings > Voice picker comes from the manifest's "wake_word" field.
# See models/README.md.
set(BEDSIDE_WAKE_MODELS
    hi_esp
    hey_mr_clock
)

set(wake_embed "")
set(wake_xlist "")
foreach(m ${BEDSIDE_WAKE_MODELS})
    list(APPEND wake_embed models/${m}.tflite models/${m}.json)
    string(APPEND wake_xlist "X(${m}) ")
endforeach()
# X-macro list for wake_mww.cpp; skipped in ESP-IDF's early-expansion pass
# (script mode, no real binary dir). Only rewritten when the list changes.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/gen)
    file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gen/wake_models_gen.h
         CONTENT "/* generated from BEDSIDE_WAKE_MODELS in main/CMakeLists.txt - do not edit */\n#define WAKE_MODEL_LIST(X) ${wake_xlist}\n"
         @ONLY)
endif()

idf_component_register(
    SRCS
        app_main.c
        boot_seq.c
        settings.c
        wifi_mgr.c
        clock_sync.c
        ui_clock.c
        ui_settings.c
        alarm.c
        ambient.c
        navidrome_client.c
        localai_client.c
        music_player.c
        song_cache.c
        ui_common.c
        ui_play.c
        ui_watch.c
        feature_specs.c
        app_video.c
        voice.c
        voice_actions.c
        wake_mww.cpp
        api_server.c
        evlog.c
        usb_audio.c
        fonts/dangrek_478.c
        fonts/dangrek_226.c
        fonts/app_icons_28.c
        fonts/montserrat_ext_14.c
        fonts/montserrat_ext_16.c
        fonts/montserrat_ext_20.c
        fonts/montserrat_ext_28.c
        fonts/app_fonts.c
    INCLUDE_DIRS .
    PRIV_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/gen
    EMBED_TXTFILES www/index.html
    # microWakeWord models + manifests (used only with CONFIG_BEDSIDE_WAKE_MWW;
    # ~60 KB of flash per model otherwise) - see models/README.md
    EMBED_FILES ${wake_embed})
