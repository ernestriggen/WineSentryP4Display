idf_component_register(
    SRCS
        arc_text.cpp
        displaycommunications.cpp
        displaydashboardbackground.cpp
        displaydashboardscreen.cpp
        displayhelpers.cpp
        displayscreenmanager.cpp
        displaytransmitter.cpp
        displaywinedetailscreen.cpp
        lowerstatus.cpp
        main.cpp
        messages.cpp
        models/displaycommunicationsmodels.cpp
        ping.cpp
        processwinestatechange.cpp
        rightstatus.cpp
        splashscreen.cpp
        statusbar.cpp
        winefailurepopup.cpp
        wineglass.cpp
        winepresentation.cpp
        winereading.cpp
        winereadingicons.cpp
        winesentry_sans_16.c 
        winesentry_serif_24.c

    INCLUDE_DIRS
        .
        ${LV_DEMO_DIR}

    REQUIRES
        bsp_extra
        esp_driver_jpeg  
        esp_driver_uart
        mbedtls
        nvs_flash
        spiffs
        waveshare__esp32_p4_wifi6_touch_lcd_xc
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=cpp)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
        -DLV_USE_DEMO_MUSIC
)
idf_build_get_property(build_components BUILD_COMPONENTS)
if("lvgl__lvgl" IN_LIST build_components)
    idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
    if(TARGET ${LVGL_LIB})
        target_compile_options(${LVGL_LIB} PRIVATE -Wno-error=attributes -Wno-error=cpp)
    endif()
endif()
idf_build_get_property(build_components BUILD_COMPONENTS)
if("chmorgan__esp-audio-player" IN_LIST build_components AND "esp_driver_i2s" IN_LIST build_components)
    idf_component_get_property(AUDIO_PLAYER_LIB chmorgan__esp-audio-player COMPONENT_LIB)
    if(TARGET ${AUDIO_PLAYER_LIB})
        target_link_libraries(${AUDIO_PLAYER_LIB} PUBLIC idf::esp_driver_i2s)
    endif()
endif()
