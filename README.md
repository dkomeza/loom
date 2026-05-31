# Loom

Loom is a small immediate-mode graphics renderer for embedded displays. The
core library records draw commands for a frame, rasterizes intersecting commands
into fixed-height RGB888 tiles, and asks the host platform to flush each tile.

The core API is platform-neutral. Callers provide display geometry, allocation
hooks, a monotonic clock/logging hook if desired, and flush callbacks. Display
hardware initialization stays outside Loom.

## Backends

The ESP-IDF backend is available when the library is built as an ESP-IDF
component. Include `loom/loom_esp_idf.h` and create a renderer from an existing
`esp_lcd_panel_handle_t`:

```c
loom_esp_idf_config_t cfg = {
    .width = 480,
    .height = 640,
    .format = LOOM_PIXEL_FORMAT_RGB888,
    .tile_height = 64,
    .buffer_count = 2,
    .command_capacity = 128,
    .panel = panel,
};

loom_esp_idf_t *backend = NULL;
loom_t *loom = NULL;
ESP_ERROR_CHECK(loom_esp_idf_create(&cfg, &backend, &loom));
```

The ESP-IDF adapter owns the DPI transfer callback registration, transfer
completion semaphore, ESP heap allocation hooks, and optional PPA fill
acceleration. Destroy it with `loom_esp_idf_destroy()`.

## Portable Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Portable builds use libc allocation and a caller-provided flush callback.
