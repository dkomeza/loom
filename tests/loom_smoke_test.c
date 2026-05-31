#include "loom/loom.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_WIDTH 64
#define TEST_HEIGHT 48
#define TEST_BYTES_PER_PIXEL 3

typedef struct {
  uint32_t flush_count;
  uint32_t wait_count;
  uint32_t pixel_bytes;
  uint8_t pixels[TEST_WIDTH * TEST_HEIGHT * TEST_BYTES_PER_PIXEL];
} smoke_ctx_t;

static loom_err_t smoke_flush_start(void *ctx, const void *pixels,
                                    loom_rect_t rect,
                                    loom_pixel_format_t format) {
  smoke_ctx_t *smoke = (smoke_ctx_t *)ctx;
  if (smoke == NULL || pixels == NULL || format != LOOM_PIXEL_FORMAT_RGB888 ||
      rect.w <= 0 || rect.h <= 0 || rect.x < 0 || rect.y < 0 ||
      rect.x + rect.w > TEST_WIDTH || rect.y + rect.h > TEST_HEIGHT) {
    return LOOM_ERR_INVALID_ARG;
  }

  const uint8_t *src = (const uint8_t *)pixels;
  for (int y = 0; y < rect.h; ++y) {
    uint8_t *dst = &smoke->pixels[((rect.y + y) * TEST_WIDTH + rect.x) *
                                  TEST_BYTES_PER_PIXEL];
    memcpy(dst, src + (size_t)y * rect.w * TEST_BYTES_PER_PIXEL,
           (size_t)rect.w * TEST_BYTES_PER_PIXEL);
  }

  smoke->flush_count++;
  smoke->pixel_bytes += (uint32_t)(rect.w * rect.h * TEST_BYTES_PER_PIXEL);
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

static loom_display_config_t smoke_config(smoke_ctx_t *smoke) {
  loom_display_config_t config = {
      .width = TEST_WIDTH,
      .height = TEST_HEIGHT,
      .format = LOOM_PIXEL_FORMAT_RGB888,
      .tile_height = 16,
      .buffer_count = 2,
      .command_capacity = 16,
      .flush =
          {
              .ctx = smoke,
              .flush_start = smoke_flush_start,
              .flush_wait = smoke_flush_wait,
          },
  };
  return config;
}

static const uint8_t *pixel_at(const smoke_ctx_t *smoke, int x, int y) {
  return &smoke->pixels[(y * TEST_WIDTH + x) * TEST_BYTES_PER_PIXEL];
}

static int pixel_is_color(const smoke_ctx_t *smoke, int x, int y, uint8_t r,
                          uint8_t g, uint8_t b) {
  const uint8_t *pixel = pixel_at(smoke, x, y);
  return pixel[0] == r && pixel[1] == g && pixel[2] == b;
}

static int pixel_is_not_color(const smoke_ctx_t *smoke, int x, int y,
                              uint8_t r, uint8_t g, uint8_t b) {
  return !pixel_is_color(smoke, x, y, r, g, b);
}

static int pixel_channel_between(const smoke_ctx_t *smoke, int x, int y,
                                 int channel, uint8_t min, uint8_t max) {
  const uint8_t *pixel = pixel_at(smoke, x, y);
  return pixel[channel] >= min && pixel[channel] <= max;
}

static int pixel_only_channel_between(const smoke_ctx_t *smoke, int x, int y,
                                      int channel, uint8_t min, uint8_t max) {
  const uint8_t *pixel = pixel_at(smoke, x, y);
  for (int i = 0; i < TEST_BYTES_PER_PIXEL; ++i) {
    if (i == channel) {
      if (pixel[i] < min || pixel[i] > max) {
        return 0;
      }
    } else if (pixel[i] != 0) {
      return 0;
    }
  }
  return 1;
}

static int expect_ok(loom_err_t ret, const char *message) {
  if (ret != LOOM_OK) {
    fprintf(stderr, "%s ret=%d\n", message, (int)ret);
    return 0;
  }
  return 1;
}

static int test_basic_flush(void) {
  smoke_ctx_t smoke = {0};
  loom_display_config_t config = smoke_config(&smoke);
  loom_t *loom = NULL;
  if (!expect_ok(loom_create(&config, &loom), "loom_create")) {
    return 0;
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

  int ok = ret == LOOM_OK && smoke.flush_count == 3 && smoke.wait_count == 3 &&
           smoke.pixel_bytes == TEST_WIDTH * TEST_HEIGHT * TEST_BYTES_PER_PIXEL;
  if (!ok) {
    fprintf(stderr,
            "unexpected basic result ret=%d flush=%u wait=%u bytes=%u\n",
            (int)ret, (unsigned)smoke.flush_count, (unsigned)smoke.wait_count,
            (unsigned)smoke.pixel_bytes);
  }
  loom_destroy(loom);
  return ok;
}

static int test_gradient_validation(void) {
  smoke_ctx_t smoke = {0};
  loom_display_config_t config = smoke_config(&smoke);
  loom_t *loom = NULL;
  if (!expect_ok(loom_create(&config, &loom), "loom_create")) {
    return 0;
  }
  if (!expect_ok(loom_begin_frame(loom), "loom_begin_frame")) {
    loom_destroy(loom);
    return 0;
  }

  loom_linear_gradient_t linear = {
      .p0 = {0, 0},
      .p1 = {10, 0},
      .color0 = loom_rgb(0, 0, 0),
      .color1 = loom_rgb(255, 255, 255),
  };
  loom_radial_gradient_t radial = {
      .center = {10, 10},
      .radius = 8,
      .color0 = loom_rgb(0, 0, 0),
      .color1 = loom_rgb(255, 255, 255),
  };
  loom_radial_gradient_t radial_zero = radial;
  radial_zero.radius = 0;
  loom_arc_gradient_t arc = {
      .mode = LOOM_ARC_GRADIENT_SWEEP,
      .color0 = loom_rgb(0, 0, 0),
      .color1 = loom_rgb(255, 255, 255),
  };
  loom_stroke_t stroke = {.width = 3, .color = loom_rgb(1, 2, 3)};

  int ok = loom_fill_rect_linear_gradient(loom, loom_rect(1, 1, 8, 8),
                                          NULL) == LOOM_ERR_INVALID_ARG &&
           loom_fill_round_rect_linear_gradient(
               loom, loom_rect(1, 1, 8, 8), 2,
               NULL) == LOOM_ERR_INVALID_ARG &&
           loom_fill_circle_radial_gradient(loom, (loom_point_t){10, 10}, 4,
                                            NULL) == LOOM_ERR_INVALID_ARG &&
           loom_fill_circle_radial_gradient(
               loom, (loom_point_t){10, 10}, 0,
               &radial) == LOOM_ERR_INVALID_ARG &&
           loom_fill_circle_radial_gradient(
               loom, (loom_point_t){10, 10}, 4,
               &radial_zero) == LOOM_ERR_INVALID_ARG &&
           loom_draw_arc_gradient(loom, (loom_point_t){10, 10}, 4, 0, 90,
                                  &stroke, NULL) == LOOM_ERR_INVALID_ARG &&
           loom_draw_arc_gradient(loom, (loom_point_t){10, 10}, 4, 0, 0,
                                  &stroke, &arc) == LOOM_ERR_INVALID_ARG &&
           loom_fill_rect_linear_gradient(loom, loom_rect(1, 1, 8, 8),
                                          &linear) == LOOM_OK;

  loom_destroy(loom);
  if (!ok) {
    fprintf(stderr, "gradient validation failed\n");
  }
  return ok;
}

static int test_fill_rect_linear_gradient_pixels(void) {
  smoke_ctx_t smoke = {0};
  loom_display_config_t config = smoke_config(&smoke);
  loom_t *loom = NULL;
  if (!expect_ok(loom_create(&config, &loom), "loom_create")) {
    return 0;
  }

  loom_linear_gradient_t gradient = {
      .p0 = {4, 4},
      .p1 = {13, 4},
      .color0 = loom_rgb(200, 0, 0),
      .color1 = loom_rgb(0, 0, 200),
  };
  loom_err_t ret = loom_begin_frame(loom);
  if (ret == LOOM_OK) {
    ret = loom_fill_rect_linear_gradient(loom, loom_rect(4, 4, 10, 4),
                                         &gradient);
  }
  if (ret == LOOM_OK) {
    ret = loom_end_frame(loom);
  }

  int ok = ret == LOOM_OK && pixel_is_color(&smoke, 4, 4, 200, 0, 0) &&
           pixel_is_color(&smoke, 13, 4, 0, 0, 200) &&
           pixel_channel_between(&smoke, 9, 4, 0, 80, 100) &&
           pixel_channel_between(&smoke, 9, 4, 2, 100, 120);
  if (!ok) {
    fprintf(stderr, "linear rect gradient test failed ret=%d\n", (int)ret);
  }
  loom_destroy(loom);
  return ok;
}

static int test_fill_round_rect_linear_gradient_pixels(void) {
  smoke_ctx_t smoke = {0};
  loom_display_config_t config = smoke_config(&smoke);
  loom_t *loom = NULL;
  if (!expect_ok(loom_create(&config, &loom), "loom_create")) {
    return 0;
  }

  loom_linear_gradient_t gradient = {
      .p0 = {10, 10},
      .p1 = {29, 10},
      .color0 = loom_rgb(0, 180, 0),
      .color1 = loom_rgb(0, 0, 180),
  };
  loom_err_t ret = loom_begin_frame(loom);
  if (ret == LOOM_OK) {
    ret = loom_fill_round_rect_linear_gradient(
        loom, loom_rect(10, 10, 20, 12), 5, &gradient);
  }
  if (ret == LOOM_OK) {
    ret = loom_end_frame(loom);
  }

  int ok = ret == LOOM_OK && pixel_is_color(&smoke, 10, 10, 0, 0, 0) &&
           pixel_is_not_color(&smoke, 20, 16, 0, 0, 0);
  if (!ok) {
    fprintf(stderr, "round rect gradient test failed ret=%d\n", (int)ret);
  }
  loom_destroy(loom);
  return ok;
}

static int test_fill_circle_radial_gradient_pixels(void) {
  smoke_ctx_t smoke = {0};
  loom_display_config_t config = smoke_config(&smoke);
  loom_t *loom = NULL;
  if (!expect_ok(loom_create(&config, &loom), "loom_create")) {
    return 0;
  }

  loom_radial_gradient_t gradient = {
      .center = {20, 20},
      .radius = 6,
      .color0 = loom_rgba(200, 0, 0, 128),
      .color1 = loom_rgba(0, 0, 200, 128),
  };
  loom_err_t ret = loom_begin_frame(loom);
  if (ret == LOOM_OK) {
    ret = loom_clear(loom, loom_rgb(0, 0, 0));
  }
  if (ret == LOOM_OK) {
    ret = loom_fill_circle_radial_gradient(loom, (loom_point_t){20, 20}, 6,
                                           &gradient);
  }
  if (ret == LOOM_OK) {
    ret = loom_end_frame(loom);
  }

  int ok = ret == LOOM_OK && pixel_is_color(&smoke, 20, 20, 100, 0, 0) &&
           pixel_channel_between(&smoke, 25, 20, 2, 75, 95);
  if (!ok) {
    fprintf(stderr, "radial circle gradient test failed ret=%d\n", (int)ret);
  }
  loom_destroy(loom);
  return ok;
}

static int test_circle_validation(void) {
  smoke_ctx_t smoke = {0};
  loom_display_config_t config = smoke_config(&smoke);
  loom_t *loom = NULL;
  if (!expect_ok(loom_create(&config, &loom), "loom_create")) {
    return 0;
  }
  if (!expect_ok(loom_begin_frame(loom), "loom_begin_frame")) {
    loom_destroy(loom);
    return 0;
  }

  loom_stroke_t stroke = {.width = 2, .color = loom_rgb(1, 2, 3)};
  int ok = loom_fill_circle(loom, (loom_point_t){10, 10}, 0,
                            loom_rgb(1, 0, 0)) == LOOM_ERR_INVALID_ARG &&
           loom_stroke_circle(loom, (loom_point_t){10, 10}, 0,
                              &stroke) == LOOM_ERR_INVALID_ARG &&
           loom_stroke_circle(loom, (loom_point_t){10, 10}, 4,
                              NULL) == LOOM_ERR_INVALID_ARG &&
           loom_draw_arc(loom, (loom_point_t){10, 10}, 4, 0, 0,
                         &stroke) == LOOM_ERR_INVALID_ARG;

  loom_destroy(loom);
  if (!ok) {
    fprintf(stderr, "circle validation failed\n");
  }
  return ok;
}

static int test_fill_circle_pixels_and_dirty_tile(void) {
  smoke_ctx_t smoke = {0};
  loom_display_config_t config = smoke_config(&smoke);
  loom_t *loom = NULL;
  if (!expect_ok(loom_create(&config, &loom), "loom_create")) {
    return 0;
  }

  loom_err_t ret = loom_begin_frame(loom);
  if (ret == LOOM_OK) {
    ret = loom_fill_circle(loom, (loom_point_t){8, 8}, 5, loom_rgb(200, 0, 0));
  }
  if (ret == LOOM_OK) {
    ret = loom_end_frame(loom);
  }

  int ok = ret == LOOM_OK && smoke.flush_count == 1 && smoke.wait_count == 1 &&
           pixel_is_color(&smoke, 8, 8, 200, 0, 0) &&
           pixel_is_not_color(&smoke, 13, 8, 0, 0, 0) &&
           pixel_is_color(&smoke, 20, 8, 0, 0, 0);
  if (!ok) {
    fprintf(stderr, "fill circle pixel/dirty test failed ret=%d flush=%u\n",
            (int)ret, (unsigned)smoke.flush_count);
  }
  loom_destroy(loom);
  return ok;
}

static int test_stroke_circle_pixels(void) {
  smoke_ctx_t smoke = {0};
  loom_display_config_t config = smoke_config(&smoke);
  loom_t *loom = NULL;
  if (!expect_ok(loom_create(&config, &loom), "loom_create")) {
    return 0;
  }

  loom_stroke_t stroke = {.width = 3, .color = loom_rgb(0, 210, 0)};
  loom_err_t ret = loom_begin_frame(loom);
  if (ret == LOOM_OK) {
    ret = loom_clear(loom, loom_rgb(0, 0, 0));
  }
  if (ret == LOOM_OK) {
    ret = loom_stroke_circle(loom, (loom_point_t){24, 24}, 8, &stroke);
  }
  if (ret == LOOM_OK) {
    ret = loom_end_frame(loom);
  }

  int ok = ret == LOOM_OK && pixel_is_color(&smoke, 24, 24, 0, 0, 0) &&
           pixel_is_not_color(&smoke, 32, 24, 0, 0, 0) &&
           pixel_is_color(&smoke, 38, 24, 0, 0, 0);
  if (!ok) {
    fprintf(stderr, "stroke circle pixel test failed ret=%d\n", (int)ret);
  }
  loom_destroy(loom);
  return ok;
}

static int test_arc_pixels_and_clip(void) {
  smoke_ctx_t smoke = {0};
  loom_display_config_t config = smoke_config(&smoke);
  loom_t *loom = NULL;
  if (!expect_ok(loom_create(&config, &loom), "loom_create")) {
    return 0;
  }

  loom_stroke_t stroke = {.width = 3, .color = loom_rgb(0, 0, 220)};
  loom_err_t ret = loom_begin_frame(loom);
  if (ret == LOOM_OK) {
    ret = loom_clear(loom, loom_rgb(0, 0, 0));
  }
  if (ret == LOOM_OK) {
    ret = loom_push_clip(loom, loom_rect(16, 16, 20, 20));
  }
  if (ret == LOOM_OK) {
    ret = loom_draw_arc(loom, (loom_point_t){24, 24}, 8, 0, 90, &stroke);
  }
  if (ret == LOOM_OK) {
    ret = loom_pop_clip(loom);
  }
  if (ret == LOOM_OK) {
    ret = loom_end_frame(loom);
  }

  int ok = ret == LOOM_OK && pixel_is_not_color(&smoke, 32, 24, 0, 0, 0) &&
           pixel_is_not_color(&smoke, 24, 32, 0, 0, 0) &&
           pixel_is_color(&smoke, 16, 24, 0, 0, 0) &&
           pixel_is_color(&smoke, 32, 16, 0, 0, 0);
  if (!ok) {
    fprintf(stderr, "arc pixel/clip test failed ret=%d\n", (int)ret);
  }
  loom_destroy(loom);
  return ok;
}

static int test_arc_sweep_gradient_pixels(void) {
  smoke_ctx_t smoke = {0};
  loom_display_config_t config = smoke_config(&smoke);
  loom_t *loom = NULL;
  if (!expect_ok(loom_create(&config, &loom), "loom_create")) {
    return 0;
  }

  loom_stroke_t stroke = {.width = 5, .color = loom_rgb(0, 0, 0)};
  loom_arc_gradient_t gradient = {
      .mode = LOOM_ARC_GRADIENT_SWEEP,
      .color0 = loom_rgb(210, 0, 0),
      .color1 = loom_rgb(0, 0, 210),
  };
  loom_err_t ret = loom_begin_frame(loom);
  if (ret == LOOM_OK) {
    ret = loom_clear(loom, loom_rgb(0, 0, 0));
  }
  if (ret == LOOM_OK) {
    ret = loom_draw_arc_gradient(loom, (loom_point_t){24, 24}, 8, 0, 90,
                                 &stroke, &gradient);
  }
  if (ret == LOOM_OK) {
    ret = loom_end_frame(loom);
  }

  int ok = ret == LOOM_OK &&
           pixel_only_channel_between(&smoke, 32, 24, 0, 100, 210) &&
           pixel_only_channel_between(&smoke, 24, 32, 2, 100, 210);
  if (!ok) {
    fprintf(stderr, "arc sweep gradient test failed ret=%d\n", (int)ret);
  }
  loom_destroy(loom);
  return ok;
}

static int test_arc_radial_gradient_pixels(void) {
  smoke_ctx_t smoke = {0};
  loom_display_config_t config = smoke_config(&smoke);
  loom_t *loom = NULL;
  if (!expect_ok(loom_create(&config, &loom), "loom_create")) {
    return 0;
  }

  loom_stroke_t stroke = {.width = 6, .color = loom_rgb(0, 0, 0)};
  loom_arc_gradient_t gradient = {
      .mode = LOOM_ARC_GRADIENT_RADIAL,
      .color0 = loom_rgb(0, 220, 0),
      .color1 = loom_rgb(0, 0, 220),
  };
  loom_err_t ret = loom_begin_frame(loom);
  if (ret == LOOM_OK) {
    ret = loom_clear(loom, loom_rgb(0, 0, 0));
  }
  if (ret == LOOM_OK) {
    ret = loom_draw_arc_gradient(loom, (loom_point_t){24, 24}, 10, 0, 90,
                                 &stroke, &gradient);
  }
  if (ret == LOOM_OK) {
    ret = loom_end_frame(loom);
  }

  int ok = ret == LOOM_OK &&
           pixel_channel_between(&smoke, 29, 29, 1, 150, 220) &&
           pixel_channel_between(&smoke, 33, 33, 2, 150, 220);
  if (!ok) {
    fprintf(stderr, "arc radial gradient test failed ret=%d\n", (int)ret);
  }
  loom_destroy(loom);
  return ok;
}

int main(void) {
  if (!test_basic_flush() || !test_gradient_validation() ||
      !test_fill_rect_linear_gradient_pixels() ||
      !test_fill_round_rect_linear_gradient_pixels() ||
      !test_fill_circle_radial_gradient_pixels() || !test_circle_validation() ||
      !test_fill_circle_pixels_and_dirty_tile() ||
      !test_stroke_circle_pixels() || !test_arc_pixels_and_clip() ||
      !test_arc_sweep_gradient_pixels() ||
      !test_arc_radial_gradient_pixels()) {
    return 1;
  }
  return 0;
}
