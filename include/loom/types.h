#ifndef LOOM_TYPES_H
#define LOOM_TYPES_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  LOOM_OK = 0,
  LOOM_ERR_INVALID_ARG = 1,
  LOOM_ERR_INVALID_STATE = 2,
  LOOM_ERR_NO_MEM = 3,
  LOOM_ERR_NOT_SUPPORTED = 4,
  LOOM_ERR_TIMEOUT = 5,
  LOOM_ERR_PLATFORM = 6,
} loom_err_t;

typedef enum {
  LOOM_PIXEL_FORMAT_RGB888 = 0,
  LOOM_PIXEL_FORMAT_RGB565 = 1,
} loom_pixel_format_t;

typedef enum {
  LOOM_BITMAP_FORMAT_RGB888 = 0,
  LOOM_BITMAP_FORMAT_RGBA8888 = 1,
  LOOM_BITMAP_FORMAT_A8 = 2,
} loom_bitmap_format_t;

typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
} loom_color_t;

typedef struct {
  int x;
  int y;
} loom_point_t;

typedef struct {
  int x;
  int y;
  int w;
  int h;
} loom_rect_t;

typedef struct {
  uint16_t width;
  uint16_t height;
  loom_bitmap_format_t format;
  uint16_t stride;
  const void *pixels;
} loom_bitmap_t;

typedef struct {
  uint16_t width;
  loom_color_t color;
} loom_stroke_t;

typedef struct {
  loom_point_t p0;
  loom_point_t p1;
  loom_color_t color0;
  loom_color_t color1;
} loom_linear_gradient_t;

typedef struct {
  loom_point_t center;
  uint16_t radius;
  loom_color_t color0;
  loom_color_t color1;
} loom_radial_gradient_t;

typedef enum {
  LOOM_ARC_GRADIENT_SWEEP = 0,
  LOOM_ARC_GRADIENT_RADIAL = 1,
} loom_arc_gradient_mode_t;

typedef struct {
  loom_arc_gradient_mode_t mode;
  loom_color_t color0;
  loom_color_t color1;
} loom_arc_gradient_t;

typedef enum {
  LOOM_ALLOC_DEFAULT = 0,
  LOOM_ALLOC_INTERNAL = 1,
  LOOM_ALLOC_TILE_FAST = 2,
  LOOM_ALLOC_TILE_FALLBACK = 3,
} loom_alloc_type_t;

typedef enum {
  LOOM_LOG_DEBUG = 0,
  LOOM_LOG_INFO = 1,
  LOOM_LOG_WARN = 2,
  LOOM_LOG_ERROR = 3,
} loom_log_level_t;

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

typedef struct {
  void *ctx;
  loom_err_t (*flush_start)(void *ctx, const void *pixels, loom_rect_t rect,
                            loom_pixel_format_t format);
  loom_err_t (*flush_wait)(void *ctx);
} loom_flush_ops_t;

typedef struct {
  void *ctx;
  loom_err_t (*fill_rgb888)(void *ctx, uint8_t *pixels, size_t buffer_size,
                            uint16_t width, uint16_t height, loom_rect_t rect,
                            loom_color_t color);
} loom_hw_ops_t;

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

static inline loom_color_t loom_rgb(uint8_t r, uint8_t g, uint8_t b) {
  loom_color_t color = {r, g, b, 255};
  return color;
}

static inline loom_color_t loom_rgba(uint8_t r, uint8_t g, uint8_t b,
                                     uint8_t a) {
  loom_color_t color = {r, g, b, a};
  return color;
}

static inline loom_rect_t loom_rect(int x, int y, int w, int h) {
  loom_rect_t rect = {x, y, w, h};
  return rect;
}

#ifdef __cplusplus
}
#endif

#endif
