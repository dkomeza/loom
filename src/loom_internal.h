#ifndef LOOM_INTERNAL_H
#define LOOM_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "loom/loom.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOOM_DEFAULT_COMMAND_CAPACITY 256
#define LOOM_MAX_CLIP_DEPTH 8
#define LOOM_RGB888_BYTES_PER_PIXEL 3
#define LOOM_TILE_ALIGNMENT 64

#ifndef LOOM_ENABLE_PERF_LOG
#define LOOM_ENABLE_PERF_LOG 0
#endif

#ifndef LOOM_ENABLE_DEBUG_LOG
#define LOOM_ENABLE_DEBUG_LOG 0
#endif

#ifndef LOOM_PERF_LOG_LEVEL
#define LOOM_PERF_LOG_LEVEL LOOM_LOG_DEBUG
#endif

#ifndef LOOM_DEBUG_LOG_LEVEL
#define LOOM_DEBUG_LOG_LEVEL LOOM_LOG_DEBUG
#endif

#if LOOM_ENABLE_PERF_LOG
#define LOOM_PERF_LOGF(loom, level, tag, fmt, ...) \
  do {                                             \
    if ((level) >= LOOM_PERF_LOG_LEVEL) {          \
      loom_platform_logf((loom), (level), (tag),   \
                         (fmt), ##__VA_ARGS__);   \
    }                                              \
  } while (0)
#else
#define LOOM_PERF_LOGF(loom, level, tag, fmt, ...) \
  do {                                             \
    (void)(loom);                                  \
  } while (0)
#endif

#if LOOM_ENABLE_DEBUG_LOG
#define LOOM_DEBUG_LOGF(loom, level, tag, fmt, ...) \
  do {                                              \
    if ((level) >= LOOM_DEBUG_LOG_LEVEL) {          \
      loom_platform_logf((loom), (level), (tag),    \
                         (fmt), ##__VA_ARGS__);    \
    }                                               \
  } while (0)
#else
#define LOOM_DEBUG_LOGF(loom, level, tag, fmt, ...) \
  do {                                              \
    (void)(loom);                                   \
  } while (0)
#endif

typedef enum {
  LOOM_CMD_CLEAR,
  LOOM_CMD_FILL_RECT,
  LOOM_CMD_FILL_RECT_LINEAR_GRADIENT,
  LOOM_CMD_STROKE_RECT,
  LOOM_CMD_FILL_ROUND_RECT,
  LOOM_CMD_FILL_ROUND_RECT_LINEAR_GRADIENT,
  LOOM_CMD_STROKE_ROUND_RECT,
  LOOM_CMD_FILL_CIRCLE,
  LOOM_CMD_FILL_CIRCLE_RADIAL_GRADIENT,
  LOOM_CMD_STROKE_CIRCLE,
  LOOM_CMD_LINE,
  LOOM_CMD_ARC,
  LOOM_CMD_ARC_GRADIENT,
  LOOM_CMD_BITMAP,
  LOOM_CMD_TEXT,
  LOOM_CMD_COUNT,
} loom_command_type_t;

typedef struct {
  loom_rect_t dst;
  const loom_bitmap_t *bitmap;
  loom_color_t tint;
} loom_bitmap_cmd_t;

typedef struct {
  const loom_font_t *font;
  char *text;
  int x;
  int y;
  loom_text_style_t style;
} loom_text_cmd_t;

typedef struct {
  loom_command_type_t type;
  loom_rect_t bounds;
  loom_rect_t clip;
  union {
    struct {
      loom_rect_t rect;
      loom_color_t color;
      uint16_t radius;
      loom_stroke_t stroke;
      loom_linear_gradient_t linear_gradient;
    } shape;
    struct {
      loom_point_t p0;
      loom_point_t p1;
      loom_stroke_t stroke;
    } line;
    struct {
      loom_point_t center;
      uint16_t radius;
      loom_color_t color;
      loom_stroke_t stroke;
      loom_radial_gradient_t radial_gradient;
    } circle;
    struct {
      loom_point_t center;
      uint16_t radius;
      int16_t start_degrees;
      int16_t sweep_degrees;
      double start_x;
      double start_y;
      double end_x;
      double end_y;
      loom_stroke_t stroke;
      loom_arc_gradient_t gradient;
    } arc;
    loom_bitmap_cmd_t bitmap;
    loom_text_cmd_t text;
  } data;
} loom_command_t;

#if LOOM_ENABLE_PERF_LOG
typedef struct {
  uint32_t commands_scanned;
  uint32_t commands_drawn;
  uint32_t hw_fill_attempts;
  uint32_t hw_fill_successes;
  uint32_t hw_fill_fallbacks;
  int64_t hw_fill_us;
  uint32_t command_type_drawn[LOOM_CMD_COUNT];
  int64_t command_type_us[LOOM_CMD_COUNT];
} loom_perf_counters_t;
#endif

struct loom {
  loom_display_config_t config;
  loom_command_t *commands;
  size_t command_count;
  size_t command_capacity;
  loom_rect_t clip_stack[LOOM_MAX_CLIP_DEPTH];
  size_t clip_depth;
  loom_rect_t dirty;
  bool dirty_valid;
  bool in_frame;
  loom_err_t sticky_error;
  uint8_t *tile_buffers[2];
  uint8_t buffer_count;
  size_t tile_stride;
  size_t tile_bytes;
#if LOOM_ENABLE_PERF_LOG
  loom_perf_counters_t perf;
#endif
};

loom_err_t loom_command_append(loom_t *loom, const loom_command_t *command);
void loom_release_frame_texts(loom_t *loom);

loom_rect_t loom_current_clip(const loom_t *loom);
loom_rect_t loom_screen_rect(const loom_t *loom);
bool loom_rect_intersect(loom_rect_t a, loom_rect_t b, loom_rect_t *out);
loom_rect_t loom_rect_union(loom_rect_t a, loom_rect_t b);
bool loom_rect_is_empty(loom_rect_t rect);
loom_rect_t loom_clip_to_screen(const loom_t *loom, loom_rect_t rect);

loom_err_t loom_render_tile(loom_t *loom, uint8_t *tile,
                            loom_rect_t tile_rect);
loom_err_t loom_backend_flush_start(loom_t *loom, const uint8_t *tile,
                                    loom_rect_t tile_rect);
loom_err_t loom_backend_flush_wait(loom_t *loom);
loom_err_t loom_backend_flush(loom_t *loom, const uint8_t *tile,
                              loom_rect_t tile_rect);

void *loom_platform_malloc(const loom_t *loom, size_t size,
                           loom_alloc_type_t type);
void *loom_platform_calloc(const loom_t *loom, size_t count, size_t size,
                           loom_alloc_type_t type);
void *loom_platform_aligned_alloc(const loom_t *loom, size_t alignment,
                                  size_t size, loom_alloc_type_t type);
void loom_platform_free(const loom_t *loom, void *ptr);
int64_t loom_platform_time_now_us(const loom_t *loom);
void loom_platform_logf(const loom_t *loom, loom_log_level_t level,
                        const char *tag, const char *fmt, ...);

#if LOOM_ENABLE_PERF_LOG
void loom_perf_reset(loom_t *loom);
void loom_perf_record_hw_fill(loom_t *loom, bool success, int64_t elapsed_us);
void loom_perf_record_command(loom_t *loom, loom_command_type_t type,
                              int64_t elapsed_us);
#else
static inline void loom_perf_reset(loom_t *loom) { (void)loom; }
static inline void loom_perf_record_hw_fill(loom_t *loom, bool success,
                                            int64_t elapsed_us) {
  (void)loom;
  (void)success;
  (void)elapsed_us;
}
static inline void loom_perf_record_command(loom_t *loom,
                                            loom_command_type_t type,
                                            int64_t elapsed_us) {
  (void)loom;
  (void)type;
  (void)elapsed_us;
}
#endif

#ifdef __cplusplus
}
#endif

#endif
