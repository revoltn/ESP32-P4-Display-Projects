idf_component_register(
    SRCS main.cpp
    INCLUDE_DIRS .
    EMBED_FILES
        assets/okay_nabu.tflite
        assets/okay_nabu_tts.wav
        assets/okay_nabu_tts2.wav
        assets/negative_tts.wav)
