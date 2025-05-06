add_library(loom STATIC
  loom.c
  core/core.c
)

target_include_directories(loom PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
