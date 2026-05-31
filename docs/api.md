# Loom API Guide

This guide summarizes the public C API exposed by `include/loom/`. It is
intended as a fast reference for humans and language models that need to use
Loom without reading the renderer internals.

Loom is an immediate-mode renderer. A frame is recorded as draw commands,
rasterized into fixed-height RGB888 tiles, and handed to the host through flush
callbacks. The portable core does not initialize display hardware.

## Headers

```c
#include "loom/loom.h"        // core renderer API
#include "loom/fonts.h"       // built-in fonts
#include "loom/loom_esp_idf.h" // ESP-IDF backend, ESP-IDF builds only
```

`loom/loom.h` includes `loom/types.h` and `loom/font.h`.

## Minimal Portable Setup

```c
#include "loom/loom.h"

typedef struct {
  uint8_t *framebuffer;
  uint16_t width;
  uint16_t height;
} app_display_t;

static loom_err_t app_flush_start(void *ctx, const void *pixels,
                                  loom_rect_t rect,
                                  loom_pixel_format_t format) {
  app_display_t *display = (app_display_t *)ctx;
  if (display == NULL || pixels == NULL ||
      format != LOOM_PIXEL_FORMAT_RGB888) {
    return LOOM_ERR_INVALID_ARG;
  }

  /*
   * pixels points at a compact RGB888 buffer for rect:
   * row stride is rect.w * 3 bytes.
   * Copy it to a framebuffer or submit it to display hardware here.
   */
  return LOOM_OK;
}

static loom_err_t app_flush_wait(void *ctx) {
  (void)ctx;
  return LOOM_OK;
}

loom_t *loom = NULL;
app_display_t display = {
    .framebuffer = NULL,
    .width = 480,
    .height = 640,
};

loom_display_config_t config = {
    .width = display.width,
    .height = display.height,
    .format = LOOM_PIXEL_FORMAT_RGB888,
    .tile_height = 64,
    .buffer_count = 2,
    .command_capacity = 128,
    .flush =
        {
            .ctx = &display,
            .flush_start = app_flush_start,
            .flush_wait = app_flush_wait,
        },
};

loom_err_t err = loom_create(&config, &loom);
```

## Frame Lifecycle

All draw calls must happen between `loom_begin_frame()` and
`loom_end_frame()`.

```c
loom_begin_frame(loom);

loom_clear(loom, loom_rgb(0, 0, 0));
loom_fill_rect(loom, loom_rect(20, 20, 120, 48), loom_rgb(40, 120, 220));

loom_stroke_t stroke = {
    .width = 2,
    .color = loom_rgb(255, 255, 255),
};
loom_stroke_rect(loom, loom_rect(18, 18, 124, 52), &stroke);

loom_end_frame(loom);
```

Rules:

- `loom_begin_frame()` resets the command list, clip stack, dirty rectangle,
  sticky error, and per-frame text storage.
- Draw calls outside a frame return `LOOM_ERR_INVALID_STATE`.
- `loom_end_frame()` renders only the dirty region touched by recorded commands
  or `loom_invalidate_rect()`.
- If no dirty rectangle is known, `loom_end_frame()` renders the whole screen.
- `loom_destroy(NULL)` is allowed. Destroying a non-NULL renderer releases all
  allocations owned by Loom.

## Display Configuration

`loom_create()` takes `loom_display_config_t`:

```c
typedef struct {
  uint16_t width;
  uint16_t height;
  loom_pixel_format_t format;
  uint16_t tile_height;
  uint8_t buffer_count;
  size_t command_capacity;
  loom_platform_ops_t platform;
  loom_flush_ops_t flush;
  loom_hw_ops_t hw;
} loom_display_config_t;
```

Important defaults and constraints:

- `width` and `height` must be non-zero.
- `format` must currently be `LOOM_PIXEL_FORMAT_RGB888`.
  `LOOM_PIXEL_FORMAT_RGB565` is declared but not supported by the portable
  renderer yet.
- `flush.flush_start` is required.
- `flush.flush_wait` is optional. If omitted, waiting is treated as a success.
- `tile_height == 0` selects a default of roughly one tenth of display height,
  with a minimum of 1.
- `buffer_count >= 2` requests double buffering. Any value below 2 uses one
  tile buffer. The renderer may fall back to one buffer if a requested second
  fast tile buffer cannot be allocated.
- `command_capacity == 0` uses the internal default capacity.
- The tile buffer passed to `flush_start` is compact RGB888 data for the
  flushed rect, not a full-display stride.

## Platform Callbacks

All platform callbacks are optional. If not supplied, the portable build uses
libc allocation, `clock_gettime(CLOCK_MONOTONIC)`, and logging to `stderr`.

```c
typedef struct {
  void *ctx;
  void *(*malloc)(void *ctx, size_t size, loom_alloc_type_t type);
  void *(*calloc)(void *ctx, size_t count, size_t size,
                  loom_alloc_type_t type);
  void *(*aligned_alloc)(void *ctx, size_t alignment, size_t size,
                         loom_alloc_type_t type);
  void (*free)(void *ctx, void *ptr);
  int64_t (*time_now_us)(void *ctx);
  void (*log)(void *ctx, loom_log_level_t level, const char *tag,
              const char *message);
} loom_platform_ops_t;
```

Allocation types let a platform select memory with different capabilities:

- `LOOM_ALLOC_INTERNAL`: renderer metadata and command storage.
- `LOOM_ALLOC_TILE_FAST`: preferred tile buffers.
- `LOOM_ALLOC_TILE_FALLBACK`: fallback tile buffers.
- `LOOM_ALLOC_DEFAULT`: generic allocation.

`aligned_alloc` must return memory aligned to the requested byte alignment.
Loom currently requests 64-byte alignment for tile buffers.

## Flush Callbacks

```c
typedef struct {
  void *ctx;
  loom_err_t (*flush_start)(void *ctx, const void *pixels, loom_rect_t rect,
                            loom_pixel_format_t format);
  loom_err_t (*flush_wait)(void *ctx);
} loom_flush_ops_t;
```

`flush_start` is called once per dirty tile. It receives:

- `pixels`: compact RGB888 bytes for `rect`.
- `rect`: display-space destination rectangle.
- `format`: the configured display format, currently always RGB888 for a
  successful `loom_create()`.

With two tile buffers, Loom overlaps rendering and flushing: it starts one
flush, renders the next tile, then calls `flush_wait()` before reusing the
previous tile buffer. With one tile buffer, Loom calls `flush_start()` and then
`flush_wait()` for each tile.

## Optional Hardware Fill

```c
typedef struct {
  void *ctx;
  loom_err_t (*fill_rgb888)(void *ctx, uint8_t *pixels, size_t buffer_size,
                            uint16_t width, uint16_t height, loom_rect_t rect,
                            loom_color_t color);
} loom_hw_ops_t;
```

`fill_rgb888` is an optional accelerator for large opaque rectangle fills. It
receives a tile-local rectangle, the compact tile dimensions, and the aligned
tile buffer size. Return a non-`LOOM_OK` value to make Loom fall back to the CPU
fill path.

## Colors, Points, and Rectangles

```c
loom_color_t color = loom_rgb(255, 128, 0);
loom_color_t translucent = loom_rgba(255, 128, 0, 128);
loom_rect_t rect = loom_rect(10, 20, 100, 40);
loom_point_t point = {.x = 30, .y = 50};
```

Coordinates are integer display pixels. Rectangles use `x`, `y`, `w`, and `h`;
width and height must be positive for public rectangle APIs. Colors use
straight alpha. Drawing blends source over destination when alpha is between 1
and 254.

## Clipping and Dirty Rectangles

```c
loom_push_clip(loom, loom_rect(10, 10, 200, 80));
loom_fill_circle(loom, (loom_point_t){40, 40}, 20, loom_rgb(255, 0, 0));
loom_pop_clip(loom);

loom_invalidate_rect(loom, loom_rect(0, 0, 32, 32));
```

- The clip stack is reset to the full screen at the start of each frame.
- Pushed clips are intersected with the current clip.
- `loom_pop_clip()` cannot pop the root screen clip.
- The maximum clip depth is 8 including the root clip.
- `loom_invalidate_rect()` marks a screen region dirty without adding a draw
  command. This is useful when external state affects pixels that Loom must
  flush again.

## Draw API Reference

All draw calls require an active frame and return `loom_err_t`.

### Whole-Screen Clear

```c
loom_clear(loom, loom_rgb(0, 0, 0));
```

Records a full-screen fill command. A leading opaque clear lets the rasterizer
initialize matching tiles directly to that color.

### Rectangles

```c
loom_fill_rect(loom, rect, color);
loom_stroke_rect(loom, rect, &stroke);
```

`rect.w` and `rect.h` must be positive. Stroke width must be greater than zero.

### Rounded Rectangles

```c
loom_fill_round_rect(loom, rect, radius, color);
loom_stroke_round_rect(loom, rect, radius, &stroke);
```

The radius is clamped internally to fit the rectangle.

### Circles

```c
loom_fill_circle(loom, center, radius, color);
loom_stroke_circle(loom, center, radius, &stroke);
```

`radius` must be greater than zero. Stroke width must be greater than zero.

### Lines

```c
loom_draw_line(loom, p0, p1, &stroke);
```

Draws an anti-aliased line from `p0` to `p1`. Stroke width must be greater than
zero.

### Arcs

```c
loom_draw_arc(loom, center, radius, 0, 90, &stroke);
```

Angles are degrees. Positive sweep follows the renderer's screen-coordinate
angle convention, where positive Y is downward. `radius` and `sweep_degrees`
must be non-zero, and stroke width must be greater than zero.

### Linear Gradients

```c
loom_linear_gradient_t gradient = {
    .p0 = {0, 0},
    .p1 = {100, 0},
    .color0 = loom_rgb(255, 0, 0),
    .color1 = loom_rgb(0, 0, 255),
};

loom_fill_rect_linear_gradient(loom, rect, &gradient);
loom_fill_round_rect_linear_gradient(loom, rect, radius, &gradient);
```

The gradient is sampled in display coordinates. If `p0` and `p1` are the same
point, Loom samples `color1`.

### Radial Circle Gradients

```c
loom_radial_gradient_t gradient = {
    .center = {50, 50},
    .radius = 30,
    .color0 = loom_rgb(255, 255, 255),
    .color1 = loom_rgb(0, 0, 0),
};

loom_fill_circle_radial_gradient(loom, center, radius, &gradient);
```

Both the drawn circle radius and `gradient.radius` must be greater than zero.
The gradient center and radius do not have to match the drawn circle.

### Arc Gradients

```c
loom_arc_gradient_t gradient = {
    .mode = LOOM_ARC_GRADIENT_SWEEP,
    .color0 = loom_rgb(255, 0, 0),
    .color1 = loom_rgb(0, 0, 255),
};

loom_draw_arc_gradient(loom, center, radius, 0, 180, &stroke, &gradient);
```

Modes:

- `LOOM_ARC_GRADIENT_SWEEP`: interpolates from `color0` to `color1` along the
  arc sweep.
- `LOOM_ARC_GRADIENT_RADIAL`: interpolates across the stroke thickness from
  inner radius to outer radius.

### Bitmaps

```c
loom_bitmap_t bitmap = {
    .width = 64,
    .height = 64,
    .format = LOOM_BITMAP_FORMAT_RGBA8888,
    .stride = 64 * 4,
    .pixels = pixels,
};

loom_draw_bitmap(loom, loom_rect(20, 20, 128, 128), &bitmap,
                 loom_rgba(255, 255, 255, 255));
```

Supported bitmap formats:

- `LOOM_BITMAP_FORMAT_RGB888`: 3 bytes per pixel, tinted by RGB, alpha from
  tint.
- `LOOM_BITMAP_FORMAT_RGBA8888`: 4 bytes per pixel, tinted by RGB, alpha
  multiplied by tint alpha.
- `LOOM_BITMAP_FORMAT_A8`: 1 byte per pixel mask, colored by tint.

The destination rectangle may scale the bitmap. Sampling is nearest-neighbor.
`stride` must be at least `width * bytes_per_pixel`.

### Text

```c
#include "loom/fonts.h"

loom_text_style_t style = {
    .color = loom_rgb(255, 255, 255),
    .opacity = 255,
    .size_px = 16,
};

loom_draw_text(loom, &loom_font_noto_sans_16, "42", 20, 40, &style);
```

Built-in fonts:

- `loom_font_noto_sans_16`
- `loom_font_noto_sans_32`
- `loom_font_noto_sans_digits_96`
- `loom_font_noto_sans_digits_144`

Text rendering currently walks the input as bytes. ASCII glyphs are resolved
directly. Non-ASCII UTF-8 sequences use the font fallback glyph when available.
`size_px` is stored in the style but current built-in font rendering uses the
selected font's atlas metrics rather than scaling to `size_px`.

`loom_draw_text()` copies the input string during command recording, so the
caller does not need to keep the string alive until `loom_end_frame()`. The
font and bitmap pixel data referenced by commands must remain valid until the
frame has ended.

## Error Codes

```c
typedef enum {
  LOOM_OK = 0,
  LOOM_ERR_INVALID_ARG,
  LOOM_ERR_INVALID_STATE,
  LOOM_ERR_NO_MEM,
  LOOM_ERR_NOT_SUPPORTED,
  LOOM_ERR_TIMEOUT,
  LOOM_ERR_PLATFORM,
} loom_err_t;
```

Common causes:

- `LOOM_ERR_INVALID_ARG`: null required pointer, zero size/radius, invalid
  rectangle, unsupported enum value, or invalid bitmap stride.
- `LOOM_ERR_INVALID_STATE`: drawing outside a frame, beginning a frame while
  already in a frame, ending without a frame, or popping the root clip.
- `LOOM_ERR_NO_MEM`: allocation failure, command capacity exhausted, or clip
  stack exhausted.
- `LOOM_ERR_NOT_SUPPORTED`: unsupported display format.
- `LOOM_ERR_TIMEOUT`: host flush wait timed out.
- `LOOM_ERR_PLATFORM`: host backend reported a platform failure.

If command capacity is exhausted inside a frame, Loom stores a sticky
`LOOM_ERR_NO_MEM`. Later command appends return that error, and
`loom_end_frame()` returns it without rendering the frame.

## ESP-IDF Backend

In ESP-IDF builds, include `loom/loom_esp_idf.h` and provide an initialized
`esp_lcd_panel_handle_t`.

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
esp_err_t ret = loom_esp_idf_create(&cfg, &backend, &loom);
```

The backend:

- Registers the LCD transfer-completion callback.
- Uses ESP heap capabilities for internal and DMA-capable tile memory.
- Uses `esp_timer_get_time()` and ESP logging.
- Uses PPA fill acceleration when available, falling back to CPU fills.
- Maps `loom_err_t` values to `esp_err_t` with `loom_err_to_esp_err()`.

Destroy the backend, not the renderer directly:

```c
loom_esp_idf_destroy(backend);
```

## Build and Test

Portable build:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Performance-log build:

```sh
cmake -S . -B build-perf -DLOOM_ENABLE_PERF_LOG=ON -DLOOM_PERF_LOG_LEVEL=INFO
cmake --build build-perf
```
