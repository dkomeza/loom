add_library(loom STATIC
  core/core.c
)

target_include_directories(loom PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
