#include "loom_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void *loom_libc_aligned_alloc(size_t alignment, size_t size) {
  void *ptr = NULL;
#if defined(_MSC_VER)
  ptr = _aligned_malloc(size, alignment);
#elif defined(_POSIX_VERSION) || defined(__APPLE__) || defined(__unix__)
  if (posix_memalign(&ptr, alignment, size) != 0) {
    ptr = NULL;
  }
#else
  size_t extra = alignment - 1u + sizeof(void *);
  void *raw = malloc(size + extra);
  if (raw == NULL) {
    return NULL;
  }
  uintptr_t aligned =
      ((uintptr_t)raw + sizeof(void *) + alignment - 1u) & ~(alignment - 1u);
  ((void **)aligned)[-1] = raw;
  ptr = (void *)aligned;
#endif
  return ptr;
}

void *loom_platform_malloc(const loom_t *loom, size_t size,
                           loom_alloc_type_t type) {
  (void)type;
  if (loom != NULL && loom->config.platform.malloc != NULL) {
    return loom->config.platform.malloc(loom->config.platform.ctx, size, type);
  }
  return malloc(size);
}

void *loom_platform_calloc(const loom_t *loom, size_t count, size_t size,
                           loom_alloc_type_t type) {
  (void)type;
  if (loom != NULL && loom->config.platform.calloc != NULL) {
    return loom->config.platform.calloc(loom->config.platform.ctx, count, size,
                                        type);
  }
  return calloc(count, size);
}

void *loom_platform_aligned_alloc(const loom_t *loom, size_t alignment,
                                  size_t size, loom_alloc_type_t type) {
  (void)type;
  if (loom != NULL && loom->config.platform.aligned_alloc != NULL) {
    return loom->config.platform.aligned_alloc(loom->config.platform.ctx,
                                               alignment, size, type);
  }
  return loom_libc_aligned_alloc(alignment, size);
}

void loom_platform_free(const loom_t *loom, void *ptr) {
  if (ptr == NULL) {
    return;
  }
  if (loom != NULL && loom->config.platform.free != NULL) {
    loom->config.platform.free(loom->config.platform.ctx, ptr);
    return;
  }
  free(ptr);
}

int64_t loom_platform_time_now_us(const loom_t *loom) {
  if (loom != NULL && loom->config.platform.time_now_us != NULL) {
    return loom->config.platform.time_now_us(loom->config.platform.ctx);
  }

  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }
  return (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
}

void loom_platform_logf(const loom_t *loom, loom_log_level_t level,
                        const char *tag, const char *fmt, ...) {
  char message[192];
  va_list args;
  va_start(args, fmt);
  vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);

  if (loom != NULL && loom->config.platform.log != NULL) {
    loom->config.platform.log(loom->config.platform.ctx, level, tag, message);
    return;
  }

  const char *level_text = "D";
  if (level == LOOM_LOG_INFO) {
    level_text = "I";
  } else if (level == LOOM_LOG_WARN) {
    level_text = "W";
  } else if (level == LOOM_LOG_ERROR) {
    level_text = "E";
  }
  fprintf(stderr, "%s %s: %s\n", level_text, tag != NULL ? tag : "loom",
          message);
}

loom_err_t loom_backend_flush_start(loom_t *loom, const uint8_t *tile,
                                    loom_rect_t tile_rect) {
  if (loom == NULL || tile == NULL || loom_rect_is_empty(tile_rect) ||
      loom->config.flush.flush_start == NULL) {
    return LOOM_ERR_INVALID_ARG;
  }
  return loom->config.flush.flush_start(loom->config.flush.ctx, tile, tile_rect,
                                        loom->config.format);
}

loom_err_t loom_backend_flush_wait(loom_t *loom) {
  if (loom == NULL) {
    return LOOM_ERR_INVALID_ARG;
  }
  if (loom->config.flush.flush_wait == NULL) {
    return LOOM_OK;
  }
  return loom->config.flush.flush_wait(loom->config.flush.ctx);
}

loom_err_t loom_backend_flush(loom_t *loom, const uint8_t *tile,
                              loom_rect_t tile_rect) {
  loom_err_t ret = loom_backend_flush_start(loom, tile, tile_rect);
  if (ret != LOOM_OK) {
    return ret;
  }
  return loom_backend_flush_wait(loom);
}
