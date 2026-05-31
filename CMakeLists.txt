cmake_minimum_required(VERSION 3.16)

set(LOOM_CORE_SRCS
    "src/loom.c"
    "src/loom_command.c"
    "src/loom_platform.c"
    "src/loom_raster.c"
    "src/loom_font_noto_sans_16.c"
    "src/loom_font_noto_sans_32.c"
    "src/loom_font_noto_sans_digits_96.c"
    "src/loom_font_noto_sans_digits_144.c")

if(DEFINED IDF_TARGET)
    idf_component_register(
        SRCS
            ${LOOM_CORE_SRCS}
            "src/platform/esp_idf/loom_esp_idf.c"
        INCLUDE_DIRS "include"
        PRIV_INCLUDE_DIRS "src"
        PRIV_REQUIRES esp_driver_ppa esp_hw_support esp_lcd esp_timer heap)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC LOOM_ENABLE_ESP_IDF=1)
else()
    project(loom LANGUAGES C)

    option(LOOM_BUILD_TESTS "Build loom tests" ON)

    add_library(loom STATIC ${LOOM_CORE_SRCS})
    target_include_directories(loom PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
    target_include_directories(loom PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_features(loom PUBLIC c_std_11)

    if(LOOM_BUILD_TESTS)
        enable_testing()
        add_executable(loom_smoke_test tests/loom_smoke_test.c)
        target_link_libraries(loom_smoke_test PRIVATE loom)
        add_test(NAME loom_smoke_test COMMAND loom_smoke_test)
    endif()
endif()
