#include "loom/loom.h"

#include <stdint.h>
#include <stdio.h>

typedef struct {
  uint32_t flush_count;
  uint32_t wait_count;
  uint32_t pixel_bytes;
} smoke_ctx_t;

static loom_err_t smoke_flush_start(void *ctx, const void *pixels,
                                    loom_rect_t rect,
                                    loom_pixel_format_t format) {
  smoke_ctx_t *smoke = (smoke_ctx_t *)ctx;
  if (smoke == NULL || pixels == NULL || format != LOOM_PIXEL_FORMAT_RGB888 ||
      rect.w <= 0 || rect.h <= 0) {
    return LOOM_ERR_INVALID_ARG;
  }
  smoke->flush_count++;
  smoke->pixel_bytes += (uint32_t)(rect.w * rect.h * 3);
  return LOOM_OK;
}

static loom_err_t smoke_flush_wait(void *ctx) {
  smoke_ctx_t *smoke = (smoke_ctx_t *)ctx;
  if (smoke == NULL) {
    return LOOM_ERR_INVALID_ARG;
  }
  smoke->wait_count++;
  return LOOM_OK;
}

int main(void) {
  smoke_ctx_t smoke = {0};
  loom_display_config_t config = {
      .width = 64,
      .height = 48,
      .format = LOOM_PIXEL_FORMAT_RGB888,
      .tile_height = 16,
      .buffer_count = 2,
      .command_capacity = 8,
      .flush =
          {
              .ctx = &smoke,
              .flush_start = smoke_flush_start,
              .flush_wait = smoke_flush_wait,
          },
  };

  loom_t *loom = NULL;
  if (loom_create(&config, &loom) != LOOM_OK || loom == NULL) {
    fprintf(stderr, "loom_create failed\n");
    return 1;
  }

  loom_err_t ret = loom_begin_frame(loom);
  if (ret == LOOM_OK) {
    ret = loom_clear(loom, loom_rgb(1, 2, 3));
  }
  if (ret == LOOM_OK) {
    ret = loom_fill_rect(loom, loom_rect(4, 4, 20, 10), loom_rgb(8, 9, 10));
  }
  if (ret == LOOM_OK) {
    ret = loom_end_frame(loom);
  }

  loom_destroy(loom);

  if (ret != LOOM_OK || smoke.flush_count != 3 || smoke.wait_count != 3 ||
      smoke.pixel_bytes != 64u * 48u * 3u) {
    fprintf(stderr,
            "unexpected result ret=%d flush=%u wait=%u bytes=%u\n", ret,
            (unsigned)smoke.flush_count, (unsigned)smoke.wait_count,
            (unsigned)smoke.pixel_bytes);
    return 1;
  }

  return 0;
}
