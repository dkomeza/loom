#include "loom_internal.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "loom";

static uint16_t loom_default_tile_height(uint16_t height) {
  uint16_t tile_height = height / 10;
  return tile_height > 0 ? tile_height : 1;
}

static void loom_free_tile_buffers(loom_t *loom) {
  if (loom == NULL) {
    return;
  }

  for (uint8_t i = 0; i < 2; ++i) {
    loom_platform_free(loom, loom->tile_buffers[i]);
    loom->tile_buffers[i] = NULL;
  }
}

static void loom_free_allocations(loom_t *loom) {
  if (loom == NULL) {
    return;
  }

  loom_release_frame_texts(loom);
  loom_free_tile_buffers(loom);

  loom_platform_free(loom, loom->commands);
  loom->commands = NULL;
}

#if LOOM_ENABLE_PERF_LOG
void loom_perf_reset(loom_t *loom) {
  if (loom == NULL) {
    return;
  }
  memset(&loom->perf, 0, sizeof(loom->perf));
}

void loom_perf_record_hw_fill(loom_t *loom, bool success, int64_t elapsed_us) {
  if (loom == NULL) {
    return;
  }

  loom->perf.hw_fill_attempts++;
  if (success) {
    loom->perf.hw_fill_successes++;
  } else {
    loom->perf.hw_fill_fallbacks++;
  }
  if (elapsed_us > 0) {
    loom->perf.hw_fill_us += elapsed_us;
  }
}

void loom_perf_record_command(loom_t *loom, loom_command_type_t type,
                              int64_t elapsed_us) {
  if (loom == NULL || type < 0 || type >= LOOM_CMD_COUNT) {
    return;
  }

  loom->perf.command_type_drawn[type]++;
  if (elapsed_us > 0) {
    loom->perf.command_type_us[type] += elapsed_us;
  }
}
#endif

#if LOOM_ENABLE_PERF_LOG
static const char *loom_command_type_name(loom_command_type_t type) {
  switch (type) {
  case LOOM_CMD_CLEAR:
    return "clear";
  case LOOM_CMD_FILL_RECT:
    return "fill_rect";
  case LOOM_CMD_FILL_RECT_LINEAR_GRADIENT:
    return "fill_rect_linear_gradient";
  case LOOM_CMD_STROKE_RECT:
    return "stroke_rect";
  case LOOM_CMD_FILL_ROUND_RECT:
    return "fill_round_rect";
  case LOOM_CMD_FILL_ROUND_RECT_LINEAR_GRADIENT:
    return "fill_round_rect_linear_gradient";
  case LOOM_CMD_STROKE_ROUND_RECT:
    return "stroke_round_rect";
  case LOOM_CMD_FILL_CIRCLE:
    return "fill_circle";
  case LOOM_CMD_FILL_CIRCLE_RADIAL_GRADIENT:
    return "fill_circle_radial_gradient";
  case LOOM_CMD_STROKE_CIRCLE:
    return "stroke_circle";
  case LOOM_CMD_LINE:
    return "line";
  case LOOM_CMD_ARC:
    return "arc";
  case LOOM_CMD_ARC_GRADIENT:
    return "arc_gradient";
  case LOOM_CMD_BITMAP:
    return "bitmap";
  case LOOM_CMD_TEXT:
    return "text";
  case LOOM_CMD_COUNT:
  default:
    return "unknown";
  }
}
#endif

static bool loom_alloc_tile_buffers(loom_t *loom, uint8_t count,
                                    loom_alloc_type_t type) {
  if (loom == NULL || count == 0 || count > 2) {
    return false;
  }

  loom_free_tile_buffers(loom);
  for (uint8_t i = 0; i < count; ++i) {
    loom->tile_buffers[i] = loom_platform_aligned_alloc(
        loom, LOOM_TILE_ALIGNMENT, loom->tile_bytes, type);
    if (loom->tile_buffers[i] == NULL) {
      loom_free_tile_buffers(loom);
      return false;
    }
  }
  loom->buffer_count = count;
  return true;
}

static loom_err_t loom_alloc_preferred_tile_buffers(loom_t *loom,
                                                    uint8_t requested_count) {
  if (loom_alloc_tile_buffers(loom, requested_count, LOOM_ALLOC_TILE_FAST)) {
    loom_platform_logf(loom, LOOM_LOG_INFO, TAG,
                       "tile buffers: %u x %u bytes in fast memory",
                       (unsigned)loom->buffer_count,
                       (unsigned)loom->tile_bytes);
    return LOOM_OK;
  }

  if (requested_count > 1 &&
      loom_alloc_tile_buffers(loom, 1, LOOM_ALLOC_TILE_FAST)) {
    loom->config.buffer_count = 1;
    loom_platform_logf(loom, LOOM_LOG_WARN, TAG,
                       "only one %u byte tile buffer fit in fast memory; "
                       "using one buffer instead of %u",
                       (unsigned)loom->tile_bytes, (unsigned)requested_count);
    return LOOM_OK;
  }

  if (loom_alloc_tile_buffers(loom, requested_count,
                              LOOM_ALLOC_TILE_FALLBACK)) {
    loom_platform_logf(loom, LOOM_LOG_WARN, TAG,
                       "tile buffers: %u x %u bytes in fallback memory",
                       (unsigned)loom->buffer_count,
                       (unsigned)loom->tile_bytes);
    return LOOM_OK;
  }

  return LOOM_ERR_NO_MEM;
}

static bool loom_mul_size_checked(size_t a, size_t b, size_t *out) {
  if (out == NULL) {
    return false;
  }
  if (a != 0 && b > SIZE_MAX / a) {
    return false;
  }

  *out = a * b;
  return true;
}

loom_err_t loom_create(const loom_display_config_t *config, loom_t **out_loom) {
  if (config == NULL || out_loom == NULL || config->width == 0 ||
      config->height == 0 || config->flush.flush_start == NULL) {
    return LOOM_ERR_INVALID_ARG;
  }

  *out_loom = NULL;

  if (config->format != LOOM_PIXEL_FORMAT_RGB888) {
    return LOOM_ERR_NOT_SUPPORTED;
  }

  loom_t bootstrap = {
      .config = *config,
  };
  loom_t *loom =
      loom_platform_calloc(&bootstrap, 1, sizeof(*loom), LOOM_ALLOC_INTERNAL);
  if (loom == NULL) {
    return LOOM_ERR_NO_MEM;
  }

  loom->config = *config;
  if (loom->config.tile_height == 0) {
    loom->config.tile_height = loom_default_tile_height(config->height);
  }
  if (loom->config.tile_height == 0) {
    loom_destroy(loom);
    return LOOM_ERR_INVALID_ARG;
  }

  loom->config.buffer_count = config->buffer_count >= 2 ? 2 : 1;
  loom->config.command_capacity = config->command_capacity > 0
                                      ? config->command_capacity
                                      : LOOM_DEFAULT_COMMAND_CAPACITY;

  loom->command_capacity = loom->config.command_capacity;
  loom->commands = loom_platform_calloc(loom, loom->command_capacity,
                                        sizeof(loom->commands[0]),
                                        LOOM_ALLOC_INTERNAL);
  if (loom->commands == NULL) {
    loom_destroy(loom);
    return LOOM_ERR_NO_MEM;
  }

  if (!loom_mul_size_checked(loom->config.width, LOOM_RGB888_BYTES_PER_PIXEL,
                             &loom->tile_stride) ||
      !loom_mul_size_checked(loom->tile_stride, loom->config.tile_height,
                             &loom->tile_bytes) ||
      loom->tile_stride == 0 || loom->tile_bytes == 0) {
    loom_destroy(loom);
    return LOOM_ERR_INVALID_ARG;
  }

  loom_err_t alloc_ret =
      loom_alloc_preferred_tile_buffers(loom, loom->config.buffer_count);
  if (alloc_ret != LOOM_OK) {
    loom_destroy(loom);
    return alloc_ret;
  }

  *out_loom = loom;
  return LOOM_OK;
}

void loom_destroy(loom_t *loom) {
  if (loom == NULL) {
    return;
  }

  loom_platform_ops_t platform = loom->config.platform;
  loom_free_allocations(loom);
  memset(loom, 0, sizeof(*loom));
  if (platform.free != NULL) {
    platform.free(platform.ctx, loom);
  } else {
    free(loom);
  }
}

loom_err_t loom_begin_frame(loom_t *loom) {
  if (loom == NULL) {
    return LOOM_ERR_INVALID_ARG;
  }
  if (loom->in_frame) {
    return LOOM_ERR_INVALID_STATE;
  }

  loom_release_frame_texts(loom);
  loom->command_count = 0;
  loom->clip_stack[0] = loom_screen_rect(loom);
  loom->clip_depth = 1;
  loom->dirty = loom_rect(0, 0, 0, 0);
  loom->dirty_valid = false;
  loom->sticky_error = LOOM_OK;
  loom->in_frame = true;
  loom_perf_reset(loom);

  return LOOM_OK;
}

loom_err_t loom_end_frame(loom_t *loom) {
  if (loom == NULL) {
    return LOOM_ERR_INVALID_ARG;
  }
  if (!loom->in_frame) {
    return LOOM_ERR_INVALID_STATE;
  }

  int64_t start_us = loom_platform_time_now_us(loom);
  int64_t render_us = 0;
  int64_t flush_us = 0;
  uint32_t tile_count = 0;
  loom_rect_t dirty = loom_rect(0, 0, 0, 0);
  loom_err_t ret = loom->sticky_error;
  if (ret == LOOM_OK) {
    dirty = loom->dirty_valid ? loom->dirty : loom_screen_rect(loom);
    dirty = loom_clip_to_screen(loom, dirty);

    int dirty_bottom = dirty.y + dirty.h;
    uint8_t buffer_index = 0;
    bool flush_pending = false;
    for (int y = dirty.y; !loom_rect_is_empty(dirty) && y < dirty_bottom;
         y += loom->config.tile_height) {
      int remaining = dirty_bottom - y;
      int tile_h = remaining < loom->config.tile_height
                       ? remaining
                       : loom->config.tile_height;
      loom_rect_t tile_rect = loom_rect(dirty.x, y, dirty.w, tile_h);
      uint8_t *tile = loom->tile_buffers[buffer_index];

      int64_t render_start_us = loom_platform_time_now_us(loom);
#if LOOM_ENABLE_PERF_LOG
      loom_perf_counters_t tile_perf_start = loom->perf;
#endif
      ret = loom_render_tile(loom, tile, tile_rect);
      int64_t render_end_us = loom_platform_time_now_us(loom);
      render_us += render_end_us - render_start_us;
      if (ret != LOOM_OK) {
        break;
      }

      int64_t flush_start_us = loom_platform_time_now_us(loom);
      if (loom->buffer_count > 1) {
        if (flush_pending) {
          ret = loom_backend_flush_wait(loom);
          flush_us += loom_platform_time_now_us(loom) - flush_start_us;
          flush_pending = false;
          if (ret != LOOM_OK) {
            break;
          }
          flush_start_us = loom_platform_time_now_us(loom);
        }

        ret = loom_backend_flush_start(loom, tile, tile_rect);
        flush_us += loom_platform_time_now_us(loom) - flush_start_us;
        if (ret == LOOM_OK) {
          flush_pending = true;
        }
      } else {
        ret = loom_backend_flush(loom, tile, tile_rect);
        flush_us += loom_platform_time_now_us(loom) - flush_start_us;
      }
      if (ret != LOOM_OK) {
        break;
      }

#if LOOM_ENABLE_PERF_LOG
      int64_t tile_render_us = render_end_us - render_start_us;
      loom_perf_counters_t tile_perf = loom->perf;
      tile_perf.commands_scanned -= tile_perf_start.commands_scanned;
      tile_perf.commands_drawn -= tile_perf_start.commands_drawn;
      tile_perf.hw_fill_attempts -= tile_perf_start.hw_fill_attempts;
      tile_perf.hw_fill_successes -= tile_perf_start.hw_fill_successes;
      tile_perf.hw_fill_fallbacks -= tile_perf_start.hw_fill_fallbacks;
      tile_perf.hw_fill_us -= tile_perf_start.hw_fill_us;
      LOOM_PERF_LOGF(
          loom, LOOM_LOG_DEBUG, TAG,
          "tile %u rect=%d,%d %dx%d render=%lld us commands=%u/%u "
          "hw_fill=%u/%u fallback=%u hw_fill=%lld us",
          (unsigned)tile_count, tile_rect.x, tile_rect.y, tile_rect.w,
          tile_rect.h, (long long)tile_render_us,
          (unsigned)tile_perf.commands_drawn,
          (unsigned)tile_perf.commands_scanned,
          (unsigned)tile_perf.hw_fill_successes,
          (unsigned)tile_perf.hw_fill_attempts,
          (unsigned)tile_perf.hw_fill_fallbacks,
          (long long)tile_perf.hw_fill_us);
#endif

      tile_count++;
      buffer_index = (uint8_t)((buffer_index + 1) % loom->buffer_count);
    }

    if (flush_pending) {
      int64_t flush_start_us = loom_platform_time_now_us(loom);
      loom_err_t wait_ret = loom_backend_flush_wait(loom);
      flush_us += loom_platform_time_now_us(loom) - flush_start_us;
      if (ret == LOOM_OK) {
        ret = wait_ret;
      }
    }
  }

  loom_release_frame_texts(loom);
  loom->in_frame = false;
  int64_t elapsed_us = loom_platform_time_now_us(loom) - start_us;
  float hz = elapsed_us > 0 ? (float)1000000 / elapsed_us : 0.0f;
  loom_platform_logf(loom, LOOM_LOG_INFO, TAG,
                     "rendered %u commands, %u tiles, dirty=%dx%d in %lld us "
                     "(render=%lld us flush=%lld us), ~%f hz",
                     (unsigned)loom->command_count, (unsigned)tile_count,
                     dirty.w, dirty.h, (long long)elapsed_us,
                     (long long)render_us, (long long)flush_us, hz);
#if LOOM_ENABLE_PERF_LOG
  LOOM_PERF_LOGF(loom, LOOM_LOG_INFO, TAG,
                 "frame perf: ret=%d commands drawn/scanned=%u/%u "
                 "hw_fill=%u/%u fallback=%u hw_fill=%lld us tile_bytes=%u "
                 "buffers=%u",
                 (int)ret, (unsigned)loom->perf.commands_drawn,
                 (unsigned)loom->perf.commands_scanned,
                 (unsigned)loom->perf.hw_fill_successes,
                 (unsigned)loom->perf.hw_fill_attempts,
                 (unsigned)loom->perf.hw_fill_fallbacks,
                 (long long)loom->perf.hw_fill_us,
                 (unsigned)loom->tile_bytes, (unsigned)loom->buffer_count);
  for (loom_command_type_t type = 0; type < LOOM_CMD_COUNT; ++type) {
    if (loom->perf.command_type_drawn[type] == 0) {
      continue;
    }
    LOOM_PERF_LOGF(loom, LOOM_LOG_INFO, TAG,
                   "command perf: %s drawn=%u total=%lld us avg=%lld us",
                   loom_command_type_name(type),
                   (unsigned)loom->perf.command_type_drawn[type],
                   (long long)loom->perf.command_type_us[type],
                   (long long)(loom->perf.command_type_us[type] /
                               loom->perf.command_type_drawn[type]));
  }
#endif
  return ret;
}
