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
    if(IDF_TARGET STREQUAL "esp32p4")
        target_compile_definitions(${COMPONENT_LIB} PUBLIC LOOM_ENABLE_ESP32P4=1)
    endif()
    if(CONFIG_LOOM_PERF_LOG_LEVEL_DEBUG)
        target_compile_definitions(${COMPONENT_LIB} PUBLIC
            LOOM_ENABLE_PERF_LOG=1
            LOOM_PERF_LOG_LEVEL=0
            LOOM_ENABLE_DEBUG_LOG=1
            LOOM_DEBUG_LOG_LEVEL=0
            LOG_LOCAL_LEVEL=4)
    elseif(CONFIG_LOOM_PERF_LOG_LEVEL_INFO)
        target_compile_definitions(${COMPONENT_LIB} PUBLIC
            LOOM_ENABLE_PERF_LOG=1
            LOOM_PERF_LOG_LEVEL=1
            LOG_LOCAL_LEVEL=3)
    elseif(LOOM_ENABLE_PERF_LOG)
        target_compile_definitions(${COMPONENT_LIB} PUBLIC
            LOOM_ENABLE_PERF_LOG=1
            LOOM_PERF_LOG_LEVEL=0
            LOG_LOCAL_LEVEL=4)
    endif()
    if(LOOM_ENABLE_DEBUG_LOG)
        target_compile_definitions(${COMPONENT_LIB} PUBLIC
            LOOM_ENABLE_DEBUG_LOG=1
            LOOM_DEBUG_LOG_LEVEL=0
            LOG_LOCAL_LEVEL=4)
    endif()
else()
    project(loom LANGUAGES C)

    option(LOOM_BUILD_TESTS "Build loom tests" ON)
    option(LOOM_ENABLE_PERF_LOG "Enable Loom performance logging" OFF)
    option(LOOM_ENABLE_DEBUG_LOG "Enable Loom debug logging" OFF)
    set(LOOM_PERF_LOG_LEVEL "DEBUG" CACHE STRING
        "Loom performance log level when LOOM_ENABLE_PERF_LOG is enabled")
    set_property(CACHE LOOM_PERF_LOG_LEVEL PROPERTY STRINGS DEBUG INFO WARN ERROR)

    add_library(loom STATIC ${LOOM_CORE_SRCS})
    target_include_directories(loom PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
    target_include_directories(loom PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_features(loom PUBLIC c_std_11)
    if(UNIX AND NOT APPLE)
        target_link_libraries(loom PUBLIC m)
    endif()
    if(LOOM_ENABLE_PERF_LOG)
        if(LOOM_PERF_LOG_LEVEL STREQUAL "ERROR")
            set(LOOM_PERF_LOG_LEVEL_VALUE 3)
        elseif(LOOM_PERF_LOG_LEVEL STREQUAL "WARN")
            set(LOOM_PERF_LOG_LEVEL_VALUE 2)
        elseif(LOOM_PERF_LOG_LEVEL STREQUAL "INFO")
            set(LOOM_PERF_LOG_LEVEL_VALUE 1)
        else()
            set(LOOM_PERF_LOG_LEVEL_VALUE 0)
        endif()
        target_compile_definitions(loom PUBLIC
            LOOM_ENABLE_PERF_LOG=1
            LOOM_PERF_LOG_LEVEL=${LOOM_PERF_LOG_LEVEL_VALUE})
    endif()
    if(LOOM_ENABLE_DEBUG_LOG)
        target_compile_definitions(loom PUBLIC
            LOOM_ENABLE_DEBUG_LOG=1
            LOOM_DEBUG_LOG_LEVEL=0)
    endif()

    if(LOOM_BUILD_TESTS)
        enable_testing()
        add_executable(loom_smoke_test tests/loom_smoke_test.c)
        target_link_libraries(loom_smoke_test PRIVATE loom)
        add_test(NAME loom_smoke_test COMMAND loom_smoke_test)
    endif()
endif()
