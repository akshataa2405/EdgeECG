if(IDF_TARGET)
    # ESP-IDF Component registration
    idf_component_register(
        SRCS 
            "main.c"
            "acquisition.c"
            "filters.c"
            "pan_tompkins.c"
            "rr_tracker.c"
            "signal_quality.c"
            "persistent_queue.c"
            "sync_client.c"
        INCLUDE_DIRS "."
        REQUIRES nvs_flash esp_timer esp_adc esp_http_client esp_littlefs json
    )
else()
    # Native host static library for DSP & core logic testing
    add_library(edgeecg_core STATIC
        "filters.c"
        "pan_tompkins.c"
        "rr_tracker.c"
        "signal_quality.c"
        "persistent_queue.c"
        "acquisition.c"
    )
    target_include_directories(edgeecg_core PUBLIC .)
    target_compile_definitions(edgeecg_core PUBLIC EDGEECG_HOST_BUILD=1)
    target_link_libraries(edgeecg_core m)
endif()
