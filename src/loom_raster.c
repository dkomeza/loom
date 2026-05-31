#include "loom_internal.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LOOM_HW_FILL_MIN_PIXELS 1024
#define LOOM_CURVE_AA_SAMPLES 4
#define LOOM_GRADIENT_ONE 65535

static size_t loom_align_up_size(size_t value, size_t alignment) {
  return (value + alignment - 1u) & ~(alignment - 1u);
}

static int loom_abs_int(int value) { return value < 0 ? -value : value; }

static int loom_min_int(int a, int b) { return a < b ? a : b; }

static int loom_max_int(int a, int b) { return a > b ? a : b; }

static int64_t loom_abs_i64(int64_t value) {
  return value < 0 ? -value : value;
}

static int64_t loom_rect_right64(loom_rect_t rect) {
  return (int64_t)rect.x + (int64_t)rect.w;
}

static int64_t loom_rect_bottom64(loom_rect_t rect) {
  return (int64_t)rect.y + (int64_t)rect.h;
}

static loom_rect_t loom_rect_inset(loom_rect_t rect, int amount) {
  int64_t left = (int64_t)rect.x + amount;
  int64_t top = (int64_t)rect.y + amount;
  int64_t right = loom_rect_right64(rect) - amount;
  int64_t bottom = loom_rect_bottom64(rect) - amount;
  if (right <= left || bottom <= top) {
    return loom_rect(rect.x, rect.y, 0, 0);
  }
  if (left < INT_MIN || top < INT_MIN || right > INT_MAX || bottom > INT_MAX) {
    return loom_rect(rect.x, rect.y, 0, 0);
  }
  return loom_rect((int)left, (int)top, (int)(right - left),
                   (int)(bottom - top));
}

static bool loom_intersect_command(const loom_command_t *command,
                                   loom_rect_t tile_rect, loom_rect_t *out) {
  loom_rect_t clipped;
  if (!loom_rect_intersect(command->bounds, tile_rect, &clipped)) {
    return false;
  }
  return loom_rect_intersect(clipped, command->clip, out);
}

static uint8_t loom_mul_u8(uint8_t a, uint8_t b) {
  return (uint8_t)(((uint16_t)a * b + 127u) / 255u);
}

static uint8_t loom_blend_channel(uint8_t src, uint8_t dst, uint8_t alpha) {
  uint16_t inv = (uint16_t)(255u - alpha);
  return (uint8_t)(((uint16_t)src * alpha + (uint16_t)dst * inv + 127u) / 255u);
}

static int32_t loom_clamp_gradient_t(int64_t t) {
  if (t <= 0) {
    return 0;
  }
  if (t >= LOOM_GRADIENT_ONE) {
    return LOOM_GRADIENT_ONE;
  }
  return (int32_t)t;
}

static uint8_t loom_lerp_u8(uint8_t a, uint8_t b, int32_t t) {
  int32_t inv = LOOM_GRADIENT_ONE - t;
  return (uint8_t)(((int32_t)a * inv + (int32_t)b * t +
                    (LOOM_GRADIENT_ONE / 2)) /
                   LOOM_GRADIENT_ONE);
}

static loom_color_t loom_lerp_color(loom_color_t a, loom_color_t b,
                                    int32_t t) {
  loom_color_t color = {
      .r = loom_lerp_u8(a.r, b.r, t),
      .g = loom_lerp_u8(a.g, b.g, t),
      .b = loom_lerp_u8(a.b, b.b, t),
      .a = loom_lerp_u8(a.a, b.a, t),
  };
  return color;
}

static loom_color_t
loom_sample_linear_gradient(const loom_linear_gradient_t *gradient, int x,
                            int y) {
  int64_t dx = (int64_t)gradient->p1.x - (int64_t)gradient->p0.x;
  int64_t dy = (int64_t)gradient->p1.y - (int64_t)gradient->p0.y;
  int64_t len_sq = dx * dx + dy * dy;
  if (len_sq <= 0) {
    return gradient->color1;
  }

  int64_t px = (int64_t)x - (int64_t)gradient->p0.x;
  int64_t py = (int64_t)y - (int64_t)gradient->p0.y;
  int64_t dot = px * dx + py * dy;
  int64_t t = (dot * LOOM_GRADIENT_ONE + len_sq / 2) / len_sq;
  return loom_lerp_color(gradient->color0, gradient->color1,
                         loom_clamp_gradient_t(t));
}

static loom_color_t
loom_sample_radial_gradient(const loom_radial_gradient_t *gradient, int x,
                            int y) {
  if (gradient->radius == 0) {
    return gradient->color1;
  }

  double dx = (double)x - (double)gradient->center.x;
  double dy = (double)y - (double)gradient->center.y;
  double distance = sqrt(dx * dx + dy * dy);
  int64_t t = (int64_t)((distance * (double)LOOM_GRADIENT_ONE) /
                            (double)gradient->radius +
                        0.5);
  return loom_lerp_color(gradient->color0, gradient->color1,
                         loom_clamp_gradient_t(t));
}

static uint8_t *loom_tile_pixel(uint8_t *tile, const loom_t *loom,
                                loom_rect_t tile_rect, int x, int y) {
  (void)loom;
  size_t local_y = (size_t)(y - tile_rect.y);
  size_t local_x = (size_t)(x - tile_rect.x);
  size_t compact_stride = (size_t)tile_rect.w * LOOM_RGB888_BYTES_PER_PIXEL;
  return tile + local_y * compact_stride +
         local_x * LOOM_RGB888_BYTES_PER_PIXEL;
}

static void loom_store_rgb888_run(uint8_t *dst, size_t pixels,
                                  loom_color_t color) {
  if (pixels == 0) {
    return;
  }

  if (color.r == color.g && color.g == color.b) {
    memset(dst, color.r, pixels * LOOM_RGB888_BYTES_PER_PIXEL);
    return;
  }

  uint8_t pattern[12] = {
      color.r, color.g, color.b, color.r, color.g, color.b,
      color.r, color.g, color.b, color.r, color.g, color.b,
  };
  uint32_t pattern32[3];
  memcpy(pattern32, pattern, sizeof(pattern32));

  while (((uintptr_t)dst & (sizeof(uint32_t) - 1)) != 0 && pixels > 0) {
    dst[0] = color.r;
    dst[1] = color.g;
    dst[2] = color.b;
    dst += LOOM_RGB888_BYTES_PER_PIXEL;
    pixels--;
  }

  uint32_t *dst32 = (uint32_t *)dst;
  while (pixels >= 4) {
    dst32[0] = pattern32[0];
    dst32[1] = pattern32[1];
    dst32[2] = pattern32[2];
    dst32 += 3;
    pixels -= 4;
  }

  dst = (uint8_t *)dst32;
  while (pixels > 0) {
    dst[0] = color.r;
    dst[1] = color.g;
    dst[2] = color.b;
    dst += LOOM_RGB888_BYTES_PER_PIXEL;
    pixels--;
  }
}

static void loom_clear_tile(uint8_t *tile, loom_rect_t tile_rect,
                            loom_color_t color) {
  size_t row_bytes = (size_t)tile_rect.w * LOOM_RGB888_BYTES_PER_PIXEL;

  for (int y = 0; y < tile_rect.h; ++y) {
    loom_store_rgb888_run(tile + (size_t)y * row_bytes, tile_rect.w, color);
  }
}

static bool loom_clear_covers_tile(const loom_command_t *command,
                                   loom_rect_t tile_rect) {
  if (command == NULL || command->type != LOOM_CMD_CLEAR ||
      command->data.shape.color.a != 255) {
    return false;
  }

  loom_rect_t covered;
  if (!loom_rect_intersect(tile_rect, command->clip, &covered)) {
    return false;
  }

  return covered.x == tile_rect.x && covered.y == tile_rect.y &&
         covered.w == tile_rect.w && covered.h == tile_rect.h;
}

static void loom_write_pixel(uint8_t *tile, const loom_t *loom,
                             loom_rect_t tile_rect, int x, int y,
                             loom_color_t color) {
  uint8_t *dst = loom_tile_pixel(tile, loom, tile_rect, x, y);
  if (color.a == 255) {
    dst[0] = color.r;
    dst[1] = color.g;
    dst[2] = color.b;
    return;
  }

  if (color.a == 0) {
    return;
  }

  dst[0] = loom_blend_channel(color.r, dst[0], color.a);
  dst[1] = loom_blend_channel(color.g, dst[1], color.a);
  dst[2] = loom_blend_channel(color.b, dst[2], color.a);
}

static void loom_write_pixel_coverage(uint8_t *tile, const loom_t *loom,
                                      loom_rect_t tile_rect, int x, int y,
                                      loom_color_t color, int coverage) {
  if (coverage <= 0 || color.a == 0) {
    return;
  }
  if (coverage >= LOOM_CURVE_AA_SAMPLES) {
    loom_write_pixel(tile, loom, tile_rect, x, y, color);
    return;
  }

  uint16_t alpha =
      ((uint16_t)color.a * (uint16_t)coverage +
       (LOOM_CURVE_AA_SAMPLES / 2)) /
      LOOM_CURVE_AA_SAMPLES;
  loom_color_t covered = color;
  covered.a = (uint8_t)alpha;
  loom_write_pixel(tile, loom, tile_rect, x, y, covered);
}

static void loom_fill_span(uint8_t *tile, const loom_t *loom,
                           loom_rect_t tile_rect, int y, int x0, int x1,
                           loom_color_t color) {
  if (x1 <= x0 || color.a == 0) {
    return;
  }

  uint8_t *dst = loom_tile_pixel(tile, loom, tile_rect, x0, y);
  if (color.a == 255) {
    loom_store_rgb888_run(dst, (size_t)(x1 - x0), color);
    return;
  }

  for (int x = x0; x < x1; ++x) {
    dst[0] = loom_blend_channel(color.r, dst[0], color.a);
    dst[1] = loom_blend_channel(color.g, dst[1], color.a);
    dst[2] = loom_blend_channel(color.b, dst[2], color.a);
    dst += LOOM_RGB888_BYTES_PER_PIXEL;
  }
}

static void loom_fill_rect_clipped(uint8_t *tile, const loom_t *loom,
                                   loom_rect_t tile_rect, loom_rect_t rect,
                                   loom_color_t color) {
  if (color.a == 255 && loom != NULL &&
      loom->config.hw.fill_rgb888 != NULL &&
      rect.w > 0 && rect.h > 0 &&
      (uint32_t)rect.w * (uint32_t)rect.h >= LOOM_HW_FILL_MIN_PIXELS) {
    size_t pixel_bytes =
        (size_t)tile_rect.w * (size_t)tile_rect.h * LOOM_RGB888_BYTES_PER_PIXEL;
    size_t buffer_size = loom_align_up_size(pixel_bytes, LOOM_TILE_ALIGNMENT);
    if (buffer_size <= loom->tile_bytes) {
      loom_rect_t local_rect =
          loom_rect(rect.x - tile_rect.x, rect.y - tile_rect.y, rect.w, rect.h);
#if LOOM_ENABLE_PERF_LOG
      int64_t fill_start_us = loom_platform_time_now_us(loom);
#endif
      bool fill_ok =
          loom->config.hw.fill_rgb888(loom->config.hw.ctx, tile, buffer_size,
                                      (uint16_t)tile_rect.w,
                                      (uint16_t)tile_rect.h, local_rect,
                                      color) == LOOM_OK;
#if LOOM_ENABLE_PERF_LOG
      loom_perf_record_hw_fill(
          (loom_t *)loom, fill_ok,
          loom_platform_time_now_us(loom) - fill_start_us);
#endif
      if (fill_ok) {
        return;
      }
    }
  }

  for (int y = rect.y; y < rect.y + rect.h; ++y) {
    loom_fill_span(tile, loom, tile_rect, y, rect.x, rect.x + rect.w, color);
  }
}

static void loom_fill_span_linear_gradient(
    uint8_t *tile, const loom_t *loom, loom_rect_t tile_rect, int y, int x0,
    int x1, const loom_linear_gradient_t *gradient) {
  if (x1 <= x0) {
    return;
  }

  for (int x = x0; x < x1; ++x) {
    loom_write_pixel(tile, loom, tile_rect, x, y,
                     loom_sample_linear_gradient(gradient, x, y));
  }
}

static void loom_fill_rect_linear_gradient_clipped(
    uint8_t *tile, const loom_t *loom, loom_rect_t tile_rect, loom_rect_t rect,
    const loom_linear_gradient_t *gradient) {
  for (int y = rect.y; y < rect.y + rect.h; ++y) {
    loom_fill_span_linear_gradient(tile, loom, tile_rect, y, rect.x,
                                   rect.x + rect.w, gradient);
  }
}

static bool loom_point_in_round_rect(int x, int y, loom_rect_t rect,
                                     int radius) {
  if (x < rect.x || y < rect.y || x >= rect.x + rect.w ||
      y >= rect.y + rect.h) {
    return false;
  }
  radius = loom_min_int(radius, loom_min_int(rect.w, rect.h) / 2);
  if (radius <= 0) {
    return true;
  }
  if (x >= rect.x + radius && x < rect.x + rect.w - radius) {
    return true;
  }
  if (y >= rect.y + radius && y < rect.y + rect.h - radius) {
    return true;
  }

  int cx = x < rect.x + radius ? rect.x + radius - 1 : rect.x + rect.w - radius;
  int cy = y < rect.y + radius ? rect.y + radius - 1 : rect.y + rect.h - radius;
  int64_t dx = (int64_t)x - cx;
  int64_t dy = (int64_t)y - cy;
  return dx * dx + dy * dy <= (int64_t)radius * radius;
}

static void loom_raster_fill_round_rect(uint8_t *tile, const loom_t *loom,
                                        loom_rect_t tile_rect, loom_rect_t rect,
                                        uint16_t radius, loom_rect_t clip,
                                        loom_color_t color) {
  loom_rect_t visible;
  if (!loom_rect_intersect(rect, clip, &visible)) {
    return;
  }

  int r = loom_min_int(radius, loom_min_int(rect.w, rect.h) / 2);
  if (r <= 0) {
    loom_fill_rect_clipped(tile, loom, tile_rect, visible, color);
    return;
  }

  int64_t radius_sq = (int64_t)r * (int64_t)r;
  for (int y = visible.y; y < visible.y + visible.h; ++y) {
    int inset = 0;
    if (y < rect.y + r || y >= rect.y + rect.h - r) {
      int cy = y < rect.y + r ? rect.y + r - 1 : rect.y + rect.h - r;
      int64_t dy = (int64_t)y - (int64_t)cy;
      int dx = r;
      while (dx > 0 && (int64_t)dx * (int64_t)dx + dy * dy > radius_sq) {
        --dx;
      }
      inset = loom_max_int(0, r - 1 - dx);
    }

    int x0 = loom_max_int(visible.x, rect.x + inset);
    int x1 = loom_min_int(visible.x + visible.w, rect.x + rect.w - inset);
    if (x1 > x0) {
      loom_fill_span(tile, loom, tile_rect, y, x0, x1, color);
    }
  }
}

static void loom_raster_fill_round_rect_linear_gradient(
    uint8_t *tile, const loom_t *loom, loom_rect_t tile_rect, loom_rect_t rect,
    uint16_t radius, loom_rect_t clip,
    const loom_linear_gradient_t *gradient) {
  loom_rect_t visible;
  if (!loom_rect_intersect(rect, clip, &visible)) {
    return;
  }

  int r = loom_min_int(radius, loom_min_int(rect.w, rect.h) / 2);
  if (r <= 0) {
    loom_fill_rect_linear_gradient_clipped(tile, loom, tile_rect, visible,
                                           gradient);
    return;
  }

  int64_t radius_sq = (int64_t)r * (int64_t)r;
  for (int y = visible.y; y < visible.y + visible.h; ++y) {
    int inset = 0;
    if (y < rect.y + r || y >= rect.y + rect.h - r) {
      int cy = y < rect.y + r ? rect.y + r - 1 : rect.y + rect.h - r;
      int64_t dy = (int64_t)y - (int64_t)cy;
      int dx = r;
      while (dx > 0 && (int64_t)dx * (int64_t)dx + dy * dy > radius_sq) {
        --dx;
      }
      inset = loom_max_int(0, r - 1 - dx);
    }

    int x0 = loom_max_int(visible.x, rect.x + inset);
    int x1 = loom_min_int(visible.x + visible.w, rect.x + rect.w - inset);
    loom_fill_span_linear_gradient(tile, loom, tile_rect, y, x0, x1, gradient);
  }
}

static void loom_raster_stroke_rect(uint8_t *tile, const loom_t *loom,
                                    loom_rect_t tile_rect, loom_rect_t rect,
                                    uint16_t width, loom_rect_t clip,
                                    loom_color_t color) {
  if (width == 0) {
    return;
  }

  int outer_amount = (int)width / 2;
  int inner_amount = (int)width - outer_amount;
  loom_rect_t outer = loom_rect_inset(rect, -outer_amount);
  loom_rect_t inner = loom_rect_inset(rect, inner_amount);

  loom_rect_t top;
  if (loom_rect_intersect(outer, loom_rect(outer.x, outer.y, outer.w,
                                           inner.y - outer.y),
                          &top) &&
      loom_rect_intersect(top, clip, &top)) {
    loom_fill_rect_clipped(tile, loom, tile_rect, top, color);
  }

  loom_rect_t bottom_band =
      loom_rect(outer.x, inner.y + inner.h, outer.w,
                outer.y + outer.h - (inner.y + inner.h));
  loom_rect_t bottom;
  if (loom_rect_intersect(outer, bottom_band, &bottom) &&
      loom_rect_intersect(bottom, clip, &bottom)) {
    loom_fill_rect_clipped(tile, loom, tile_rect, bottom, color);
  }

  loom_rect_t left_band =
      loom_rect(outer.x, inner.y, inner.x - outer.x, inner.h);
  loom_rect_t left;
  if (loom_rect_intersect(outer, left_band, &left) &&
      loom_rect_intersect(left, clip, &left)) {
    loom_fill_rect_clipped(tile, loom, tile_rect, left, color);
  }

  loom_rect_t right_band =
      loom_rect(inner.x + inner.w, inner.y,
                outer.x + outer.w - (inner.x + inner.w), inner.h);
  loom_rect_t right;
  if (loom_rect_intersect(outer, right_band, &right) &&
      loom_rect_intersect(right, clip, &right)) {
    loom_fill_rect_clipped(tile, loom, tile_rect, right, color);
  }

  if (loom_rect_is_empty(inner)) {
    loom_rect_t visible;
    if (loom_rect_intersect(outer, clip, &visible)) {
      loom_fill_rect_clipped(tile, loom, tile_rect, visible, color);
    }
  }
}

static void loom_raster_stroke_round_rect(uint8_t *tile, const loom_t *loom,
                                          loom_rect_t tile_rect,
                                          loom_rect_t rect, uint16_t radius,
                                          uint16_t width, loom_rect_t clip,
                                          loom_color_t color) {
  if (width == 0) {
    return;
  }

  int outer_amount = (int)width / 2;
  int inner_amount = (int)width - outer_amount;
  loom_rect_t outer = loom_rect_inset(rect, -outer_amount);
  int outer_radius = loom_min_int((int)radius + outer_amount,
                                  loom_min_int(outer.w, outer.h) / 2);
  loom_rect_t inner = loom_rect_inset(rect, inner_amount);
  int inner_radius = (int)radius - inner_amount;
  if (inner_radius < 0) {
    inner_radius = 0;
  }

  for (int y = clip.y; y < clip.y + clip.h; ++y) {
    for (int x = clip.x; x < clip.x + clip.w; ++x) {
      bool outer_contains = loom_point_in_round_rect(x, y, outer, outer_radius);
      bool inner_contains = !loom_rect_is_empty(inner) &&
                            loom_point_in_round_rect(x, y, inner, inner_radius);
      if (outer_contains && !inner_contains) {
        loom_write_pixel(tile, loom, tile_rect, x, y, color);
      }
    }
  }
}

static int loom_circle_fill_coverage(int x, int y, loom_point_t center,
                                     double radius) {
  static const double offsets[LOOM_CURVE_AA_SAMPLES][2] = {
      {-0.25, -0.25}, {0.25, -0.25}, {-0.25, 0.25}, {0.25, 0.25}};
  double radius_sq = radius * radius;
  int coverage = 0;
  for (int i = 0; i < LOOM_CURVE_AA_SAMPLES; ++i) {
    double dx = (double)x + offsets[i][0] - (double)center.x;
    double dy = (double)y + offsets[i][1] - (double)center.y;
    if (dx * dx + dy * dy <= radius_sq) {
      coverage++;
    }
  }
  return coverage;
}

static int loom_circle_stroke_coverage(int x, int y, loom_point_t center,
                                       double inner_radius,
                                       double outer_radius) {
  static const double offsets[LOOM_CURVE_AA_SAMPLES][2] = {
      {-0.25, -0.25}, {0.25, -0.25}, {-0.25, 0.25}, {0.25, 0.25}};
  double inner_sq = inner_radius * inner_radius;
  double outer_sq = outer_radius * outer_radius;
  int coverage = 0;
  for (int i = 0; i < LOOM_CURVE_AA_SAMPLES; ++i) {
    double dx = (double)x + offsets[i][0] - (double)center.x;
    double dy = (double)y + offsets[i][1] - (double)center.y;
    double distance_sq = dx * dx + dy * dy;
    if (distance_sq >= inner_sq && distance_sq <= outer_sq) {
      coverage++;
    }
  }
  return coverage;
}

static void loom_raster_fill_circle(uint8_t *tile, const loom_t *loom,
                                    loom_rect_t tile_rect,
                                    loom_point_t center, uint16_t radius,
                                    loom_rect_t clip, loom_color_t color) {
  if (radius == 0 || color.a == 0) {
    return;
  }

  double radius_sq = (double)radius * (double)radius;
  double inner_radius = radius > 1 ? (double)radius - 0.75 : 0.0;
  double inner_sq = inner_radius * inner_radius;

  for (int y = clip.y; y < clip.y + clip.h; ++y) {
    double dy = (double)y - (double)center.y;
    int fill_x0 = 0;
    int fill_x1 = 0;
    if (inner_radius > 0.0 && dy * dy <= inner_sq) {
      int dx = (int)floor(sqrt(inner_sq - dy * dy));
      fill_x0 = loom_max_int(clip.x, center.x - dx);
      fill_x1 = loom_min_int(clip.x + clip.w, center.x + dx + 1);
      if (fill_x1 > fill_x0) {
        loom_fill_span(tile, loom, tile_rect, y, fill_x0, fill_x1, color);
      }
    }

    if (dy * dy > radius_sq + (double)radius + 1.0) {
      continue;
    }

    double edge_remaining = radius_sq - dy * dy;
    int edge_dx = edge_remaining > 0.0 ? (int)ceil(sqrt(edge_remaining)) + 1
                                       : 1;
    int edge_x0 = loom_max_int(clip.x, center.x - edge_dx);
    int edge_x1 = loom_min_int(clip.x + clip.w, center.x + edge_dx + 1);
    for (int x = edge_x0; x < edge_x1; ++x) {
      if (fill_x1 > fill_x0 && x >= fill_x0 && x < fill_x1) {
        continue;
      }
      int coverage = loom_circle_fill_coverage(x, y, center, (double)radius);
      loom_write_pixel_coverage(tile, loom, tile_rect, x, y, color, coverage);
    }
  }
}

static void loom_fill_span_radial_gradient(
    uint8_t *tile, const loom_t *loom, loom_rect_t tile_rect, int y, int x0,
    int x1, const loom_radial_gradient_t *gradient) {
  for (int x = x0; x < x1; ++x) {
    loom_write_pixel(tile, loom, tile_rect, x, y,
                     loom_sample_radial_gradient(gradient, x, y));
  }
}

static void loom_raster_fill_circle_radial_gradient(
    uint8_t *tile, const loom_t *loom, loom_rect_t tile_rect,
    loom_point_t center, uint16_t radius, loom_rect_t clip,
    const loom_radial_gradient_t *gradient) {
  if (radius == 0 || gradient->radius == 0) {
    return;
  }

  double radius_sq = (double)radius * (double)radius;
  double inner_radius = radius > 1 ? (double)radius - 0.75 : 0.0;
  double inner_sq = inner_radius * inner_radius;

  for (int y = clip.y; y < clip.y + clip.h; ++y) {
    double dy = (double)y - (double)center.y;
    int fill_x0 = 0;
    int fill_x1 = 0;
    if (inner_radius > 0.0 && dy * dy <= inner_sq) {
      int dx = (int)floor(sqrt(inner_sq - dy * dy));
      fill_x0 = loom_max_int(clip.x, center.x - dx);
      fill_x1 = loom_min_int(clip.x + clip.w, center.x + dx + 1);
      if (fill_x1 > fill_x0) {
        loom_fill_span_radial_gradient(tile, loom, tile_rect, y, fill_x0,
                                       fill_x1, gradient);
      }
    }

    if (dy * dy > radius_sq + (double)radius + 1.0) {
      continue;
    }

    double edge_remaining = radius_sq - dy * dy;
    int edge_dx = edge_remaining > 0.0 ? (int)ceil(sqrt(edge_remaining)) + 1
                                       : 1;
    int edge_x0 = loom_max_int(clip.x, center.x - edge_dx);
    int edge_x1 = loom_min_int(clip.x + clip.w, center.x + edge_dx + 1);
    for (int x = edge_x0; x < edge_x1; ++x) {
      if (fill_x1 > fill_x0 && x >= fill_x0 && x < fill_x1) {
        continue;
      }
      int coverage = loom_circle_fill_coverage(x, y, center, (double)radius);
      loom_write_pixel_coverage(tile, loom, tile_rect, x, y,
                                loom_sample_radial_gradient(gradient, x, y),
                                coverage);
    }
  }
}

static void loom_raster_stroke_circle(uint8_t *tile, const loom_t *loom,
                                      loom_rect_t tile_rect,
                                      loom_point_t center, uint16_t radius,
                                      uint16_t width, loom_rect_t clip,
                                      loom_color_t color) {
  if (radius == 0 || width == 0 || color.a == 0) {
    return;
  }

  double half_width = (double)width * 0.5;
  double inner_radius = (double)radius - half_width;
  double outer_radius = (double)radius + half_width;
  if (inner_radius < 0.0) {
    inner_radius = 0.0;
  }

  double outer_sq = outer_radius * outer_radius;
  for (int y = clip.y; y < clip.y + clip.h; ++y) {
    double dy = (double)y - (double)center.y;
    if (dy * dy > outer_sq + outer_radius) {
      continue;
    }

    double remaining = outer_sq - dy * dy;
    int dx = remaining > 0.0 ? (int)ceil(sqrt(remaining)) + 1 : 1;
    int x0 = loom_max_int(clip.x, center.x - dx);
    int x1 = loom_min_int(clip.x + clip.w, center.x + dx + 1);
    for (int x = x0; x < x1; ++x) {
      int coverage =
          loom_circle_stroke_coverage(x, y, center, inner_radius, outer_radius);
      loom_write_pixel_coverage(tile, loom, tile_rect, x, y, color, coverage);
    }
  }
}

static void loom_draw_brush(uint8_t *tile, const loom_t *loom,
                            loom_rect_t tile_rect, int x, int y, uint16_t width,
                            loom_rect_t clip, loom_color_t color) {
  int half = (int)width / 2;
  int start = -half;
  int end = (int)width - half;
  for (int by = start; by < end; ++by) {
    int py = y + by;
    if (py < clip.y || py >= clip.y + clip.h) {
      continue;
    }
    for (int bx = start; bx < end; ++bx) {
      int px = x + bx;
      if (px >= clip.x && px < clip.x + clip.w) {
        loom_write_pixel(tile, loom, tile_rect, px, py, color);
      }
    }
  }
}

static bool loom_clip_line_to_rect(loom_point_t p0, loom_point_t p1,
                                   loom_rect_t clip, uint16_t brush_width,
                                   int64_t *out_x0, int64_t *out_y0,
                                   int64_t *out_x1, int64_t *out_y1) {
  if (loom_rect_is_empty(clip) || brush_width == 0 || out_x0 == NULL ||
      out_y0 == NULL || out_x1 == NULL || out_y1 == NULL) {
    return false;
  }

  double x0 = p0.x;
  double y0 = p0.y;
  double dx = (double)p1.x - (double)p0.x;
  double dy = (double)p1.y - (double)p0.y;
  double t0 = 0.0;
  double t1 = 1.0;
  double half = (double)(brush_width / 2u);
  double left = (double)clip.x - half;
  double top = (double)clip.y - half;
  double right = (double)clip.x + (double)clip.w - 1.0 + half;
  double bottom = (double)clip.y + (double)clip.h - 1.0 + half;
  const double p[4] = {-dx, dx, -dy, dy};
  const double q[4] = {x0 - left, right - x0, y0 - top, bottom - y0};

  for (int i = 0; i < 4; ++i) {
    if (p[i] == 0.0) {
      if (q[i] < 0.0) {
        return false;
      }
      continue;
    }

    double t = q[i] / p[i];
    if (p[i] < 0.0) {
      if (t > t1) {
        return false;
      }
      if (t > t0) {
        t0 = t;
      }
    } else {
      if (t < t0) {
        return false;
      }
      if (t < t1) {
        t1 = t;
      }
    }
  }

  *out_x0 = (int64_t)llround(x0 + t0 * dx);
  *out_y0 = (int64_t)llround(y0 + t0 * dy);
  *out_x1 = (int64_t)llround(x0 + t1 * dx);
  *out_y1 = (int64_t)llround(y0 + t1 * dy);
  return *out_x0 >= INT_MIN && *out_x0 <= INT_MAX && *out_y0 >= INT_MIN &&
         *out_y0 <= INT_MAX && *out_x1 >= INT_MIN && *out_x1 <= INT_MAX &&
         *out_y1 >= INT_MIN && *out_y1 <= INT_MAX;
}

static void loom_raster_draw_line(uint8_t *tile, const loom_t *loom,
                                  loom_rect_t tile_rect, loom_point_t p0,
                                  loom_point_t p1, loom_stroke_t stroke,
                                  loom_rect_t clip) {
  int64_t x0 = 0;
  int64_t y0 = 0;
  int64_t x1 = 0;
  int64_t y1 = 0;
  if (!loom_clip_line_to_rect(p0, p1, clip, stroke.width, &x0, &y0, &x1, &y1)) {
    return;
  }

  int64_t dx = loom_abs_i64(x1 - x0);
  int64_t sx = x0 < x1 ? 1 : -1;
  int64_t dy = -loom_abs_i64(y1 - y0);
  int64_t sy = y0 < y1 ? 1 : -1;
  int64_t err = dx + dy;

  for (;;) {
    loom_draw_brush(tile, loom, tile_rect, (int)x0, (int)y0, stroke.width, clip,
                    stroke.color);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    int64_t old_err = err;
    if (old_err >= dy - old_err) {
      err += dy;
      x0 += sx;
    }
    if (old_err <= dx - old_err) {
      err += dx;
      y0 += sy;
    }
  }
}

static double loom_cross_double(double ax, double ay, double bx, double by) {
  return ax * by - ay * bx;
}

static bool loom_point_in_positive_sweep(double px, double py, double start_x,
                                         double start_y, double end_x,
                                         double end_y, int sweep_degrees) {
  if (sweep_degrees >= 360) {
    return true;
  }

  double start_cross = loom_cross_double(start_x, start_y, px, py);
  double end_cross = loom_cross_double(px, py, end_x, end_y);
  if (sweep_degrees <= 180) {
    return start_cross >= -0.000001 && end_cross >= -0.000001;
  }
  return start_cross >= -0.000001 || end_cross >= -0.000001;
}

static bool loom_point_in_arc_sweep(double px, double py,
                                    const loom_command_t *command) {
  int sweep = command->data.arc.sweep_degrees;
  int abs_sweep = loom_abs_int(sweep);
  if (abs_sweep >= 360) {
    return true;
  }
  if (px == 0.0 && py == 0.0) {
    return false;
  }

  if (sweep > 0) {
    return loom_point_in_positive_sweep(
        px, py, command->data.arc.start_x, command->data.arc.start_y,
        command->data.arc.end_x, command->data.arc.end_y, abs_sweep);
  }
  return loom_point_in_positive_sweep(
      px, py, command->data.arc.end_x, command->data.arc.end_y,
      command->data.arc.start_x, command->data.arc.start_y, abs_sweep);
}

static double loom_normalize_degrees(double degrees) {
  double normalized = fmod(degrees, 360.0);
  if (normalized < 0.0) {
    normalized += 360.0;
  }
  return normalized;
}

static loom_color_t loom_sample_arc_gradient(const loom_command_t *command,
                                             int x, int y,
                                             double inner_radius,
                                             double outer_radius) {
  const loom_arc_gradient_t *gradient = &command->data.arc.gradient;
  loom_point_t center = command->data.arc.center;
  double dx = (double)x - (double)center.x;
  double dy = (double)y - (double)center.y;
  int64_t t = 0;

  if (gradient->mode == LOOM_ARC_GRADIENT_RADIAL) {
    double span = outer_radius - inner_radius;
    double distance = sqrt(dx * dx + dy * dy);
    t = span > 0.0 ? (int64_t)(((distance - inner_radius) *
                                (double)LOOM_GRADIENT_ONE) /
                                   span +
                               0.5)
                   : 0;
  } else {
    int sweep = command->data.arc.sweep_degrees;
    int abs_sweep = loom_abs_int(sweep);
    double angle = loom_normalize_degrees(atan2(dy, dx) * 180.0 / M_PI);
    double start = loom_normalize_degrees(command->data.arc.start_degrees);
    double delta = sweep > 0 ? loom_normalize_degrees(angle - start)
                             : loom_normalize_degrees(start - angle);
    double denominator = abs_sweep >= 360 ? 360.0 : (double)abs_sweep;
    t = denominator > 0.0 ? (int64_t)(delta * (double)LOOM_GRADIENT_ONE /
                                          denominator +
                                      0.5)
                          : 0;
  }

  return loom_lerp_color(gradient->color0, gradient->color1,
                         loom_clamp_gradient_t(t));
}

static int loom_arc_stroke_coverage(int x, int y,
                                    const loom_command_t *command,
                                    double inner_radius, double outer_radius) {
  static const double offsets[LOOM_CURVE_AA_SAMPLES][2] = {
      {-0.25, -0.25}, {0.25, -0.25}, {-0.25, 0.25}, {0.25, 0.25}};
  double inner_sq = inner_radius * inner_radius;
  double outer_sq = outer_radius * outer_radius;
  loom_point_t center = command->data.arc.center;
  int coverage = 0;

  for (int i = 0; i < LOOM_CURVE_AA_SAMPLES; ++i) {
    double dx = (double)x + offsets[i][0] - (double)center.x;
    double dy = (double)y + offsets[i][1] - (double)center.y;
    double distance_sq = dx * dx + dy * dy;
    if (distance_sq >= inner_sq && distance_sq <= outer_sq &&
        loom_point_in_arc_sweep(dx, dy, command)) {
      coverage++;
    }
  }
  return coverage;
}

static void loom_raster_draw_arc_gradient(uint8_t *tile, const loom_t *loom,
                                          loom_rect_t tile_rect,
                                          const loom_command_t *command,
                                          loom_rect_t clip) {
  uint16_t radius = command->data.arc.radius;
  uint16_t width = command->data.arc.stroke.width;
  const loom_arc_gradient_t *gradient = &command->data.arc.gradient;
  if (radius == 0 || width == 0 ||
      (gradient->color0.a == 0 && gradient->color1.a == 0)) {
    return;
  }

  double half_width = (double)width * 0.5;
  double inner_radius = (double)radius - half_width;
  double outer_radius = (double)radius + half_width;
  if (inner_radius < 0.0) {
    inner_radius = 0.0;
  }

  double outer_sq = outer_radius * outer_radius;
  loom_point_t center = command->data.arc.center;
  for (int y = clip.y; y < clip.y + clip.h; ++y) {
    double dy = (double)y - (double)center.y;
    if (dy * dy > outer_sq + outer_radius) {
      continue;
    }

    double remaining = outer_sq - dy * dy;
    int dx = remaining > 0.0 ? (int)ceil(sqrt(remaining)) + 1 : 1;
    int x0 = loom_max_int(clip.x, center.x - dx);
    int x1 = loom_min_int(clip.x + clip.w, center.x + dx + 1);
    for (int x = x0; x < x1; ++x) {
      int coverage =
          loom_arc_stroke_coverage(x, y, command, inner_radius, outer_radius);
      loom_write_pixel_coverage(
          tile, loom, tile_rect, x, y,
          loom_sample_arc_gradient(command, x, y, inner_radius, outer_radius),
          coverage);
    }
  }
}

static void loom_raster_draw_arc(uint8_t *tile, const loom_t *loom,
                                 loom_rect_t tile_rect,
                                 const loom_command_t *command,
                                 loom_rect_t clip) {
  uint16_t radius = command->data.arc.radius;
  uint16_t width = command->data.arc.stroke.width;
  if (radius == 0 || width == 0 || command->data.arc.stroke.color.a == 0) {
    return;
  }

  double half_width = (double)width * 0.5;
  double inner_radius = (double)radius - half_width;
  double outer_radius = (double)radius + half_width;
  if (inner_radius < 0.0) {
    inner_radius = 0.0;
  }

  double outer_sq = outer_radius * outer_radius;
  loom_point_t center = command->data.arc.center;
  for (int y = clip.y; y < clip.y + clip.h; ++y) {
    double dy = (double)y - (double)center.y;
    if (dy * dy > outer_sq + outer_radius) {
      continue;
    }

    double remaining = outer_sq - dy * dy;
    int dx = remaining > 0.0 ? (int)ceil(sqrt(remaining)) + 1 : 1;
    int x0 = loom_max_int(clip.x, center.x - dx);
    int x1 = loom_min_int(clip.x + clip.w, center.x + dx + 1);
    for (int x = x0; x < x1; ++x) {
      int coverage =
          loom_arc_stroke_coverage(x, y, command, inner_radius, outer_radius);
      loom_write_pixel_coverage(tile, loom, tile_rect, x, y,
                                command->data.arc.stroke.color, coverage);
    }
  }
}

static void loom_raster_draw_bitmap(uint8_t *tile, const loom_t *loom,
                                    loom_rect_t tile_rect,
                                    const loom_bitmap_cmd_t *cmd,
                                    loom_rect_t clip) {
  const loom_bitmap_t *bitmap = cmd->bitmap;
  if (bitmap == NULL || bitmap->pixels == NULL || cmd->dst.w <= 0 ||
      cmd->dst.h <= 0) {
    return;
  }

  const uint8_t *pixels = (const uint8_t *)bitmap->pixels;
  for (int y = clip.y; y < clip.y + clip.h; ++y) {
    int sy = (int)(((int64_t)(y - cmd->dst.y) * bitmap->height) / cmd->dst.h);
    if (sy < 0 || sy >= bitmap->height) {
      continue;
    }
    for (int x = clip.x; x < clip.x + clip.w; ++x) {
      int sx = (int)(((int64_t)(x - cmd->dst.x) * bitmap->width) / cmd->dst.w);
      if (sx < 0 || sx >= bitmap->width) {
        continue;
      }

      loom_color_t color = {0, 0, 0, 0};
      const uint8_t *src = pixels + (size_t)sy * bitmap->stride;
      switch (bitmap->format) {
      case LOOM_BITMAP_FORMAT_RGB888:
        src += (size_t)sx * 3u;
        color.r = loom_mul_u8(src[0], cmd->tint.r);
        color.g = loom_mul_u8(src[1], cmd->tint.g);
        color.b = loom_mul_u8(src[2], cmd->tint.b);
        color.a = cmd->tint.a;
        break;
      case LOOM_BITMAP_FORMAT_RGBA8888:
        src += (size_t)sx * 4u;
        color.r = loom_mul_u8(src[0], cmd->tint.r);
        color.g = loom_mul_u8(src[1], cmd->tint.g);
        color.b = loom_mul_u8(src[2], cmd->tint.b);
        color.a = loom_mul_u8(src[3], cmd->tint.a);
        break;
      case LOOM_BITMAP_FORMAT_A8:
        src += sx;
        color = cmd->tint;
        color.a = loom_mul_u8(src[0], cmd->tint.a);
        break;
      default:
        return;
      }
      loom_write_pixel(tile, loom, tile_rect, x, y, color);
    }
  }
}

static const loom_glyph_t *loom_find_glyph(const loom_font_t *font,
                                           uint32_t codepoint) {
  if (font == NULL || font->glyphs == NULL) {
    return NULL;
  }
  for (uint16_t i = 0; i < font->glyph_count; ++i) {
    if (font->glyphs[i].codepoint == codepoint) {
      return &font->glyphs[i];
    }
  }
  return NULL;
}

static const loom_glyph_t *loom_resolve_glyph(const loom_font_t *font,
                                              uint8_t byte) {
  const loom_glyph_t *glyph = loom_find_glyph(font, byte);
  if (glyph == NULL && font->fallback_codepoint != 0) {
    glyph = loom_find_glyph(font, font->fallback_codepoint);
  }
  return glyph;
}

static bool loom_glyph_atlas_valid(const loom_font_t *font,
                                   const loom_glyph_t *glyph) {
  if (font == NULL || glyph == NULL || font->atlas == NULL ||
      font->atlas_stride == 0) {
    return false;
  }
  if (glyph->width == 0 || glyph->height == 0) {
    return true;
  }
  if ((uint32_t)glyph->atlas_x + glyph->width > font->atlas_width ||
      (uint32_t)glyph->atlas_y + glyph->height > font->atlas_height) {
    return false;
  }
  return font->atlas_stride >= (uint32_t)glyph->atlas_x + glyph->width;
}

static void loom_raster_draw_text(uint8_t *tile, const loom_t *loom,
                                  loom_rect_t tile_rect,
                                  const loom_text_cmd_t *cmd,
                                  loom_rect_t clip) {
  const loom_font_t *font = cmd->font;
  if (font == NULL || cmd->text == NULL || font->atlas == NULL) {
    return;
  }

  int pen_x = cmd->x;
  loom_color_t color = cmd->style.color;
  color.a = loom_mul_u8(color.a, cmd->style.opacity);

  for (const unsigned char *p = (const unsigned char *)cmd->text; *p != '\0';
       ++p) {
    const loom_glyph_t *glyph = NULL;
    if (*p < 0x80) {
      glyph = loom_resolve_glyph(font, *p);
    } else {
      glyph = font->fallback_codepoint != 0
                  ? loom_find_glyph(font, font->fallback_codepoint)
                  : NULL;
      while ((p[1] & 0xc0) == 0x80) {
        ++p;
      }
    }

    if (glyph == NULL) {
      pen_x += font->line_height > 0 ? font->line_height / 2 : 8;
      continue;
    }

    if (!loom_glyph_atlas_valid(font, glyph)) {
      pen_x += glyph->advance_x;
      continue;
    }

    loom_rect_t glyph_rect = loom_rect(
        pen_x + glyph->bearing_x, cmd->y + font->baseline - glyph->bearing_y,
        glyph->width, glyph->height);
    loom_rect_t visible;
    if (loom_rect_intersect(glyph_rect, clip, &visible)) {
      for (int y = visible.y; y < visible.y + visible.h; ++y) {
        int gy = y - glyph_rect.y;
        const uint8_t *row =
            font->atlas + (size_t)(glyph->atlas_y + gy) * font->atlas_stride +
            glyph->atlas_x;
        for (int x = visible.x; x < visible.x + visible.w; ++x) {
          int gx = x - glyph_rect.x;
          loom_color_t glyph_color = color;
          glyph_color.a = loom_mul_u8(row[gx], color.a);
          loom_write_pixel(tile, loom, tile_rect, x, y, glyph_color);
        }
      }
    }

    pen_x += glyph->advance_x;
  }
}

loom_err_t loom_render_tile(loom_t *loom, uint8_t *tile,
                            loom_rect_t tile_rect) {
  if (loom == NULL || tile == NULL || loom_rect_is_empty(tile_rect) ||
      tile_rect.x < 0 || tile_rect.y < 0 ||
      loom_rect_right64(tile_rect) > loom->config.width ||
      loom_rect_bottom64(tile_rect) > loom->config.height ||
      tile_rect.h > loom->config.tile_height) {
    return LOOM_ERR_INVALID_ARG;
  }

  size_t first_command = 0;
  if (loom->command_count > 0 &&
      loom_clear_covers_tile(&loom->commands[0], tile_rect)) {
    loom_clear_tile(tile, tile_rect, loom->commands[0].data.shape.color);
    first_command = 1;
  } else {
    loom_clear_tile(tile, tile_rect, loom_rgb(0, 0, 0));
  }

  for (size_t i = first_command; i < loom->command_count; ++i) {
    const loom_command_t *command = &loom->commands[i];
#if LOOM_ENABLE_PERF_LOG
    loom->perf.commands_scanned++;
#endif
    loom_rect_t visible;
    if (!loom_intersect_command(command, tile_rect, &visible)) {
      continue;
    }
#if LOOM_ENABLE_PERF_LOG
    loom->perf.commands_drawn++;
#endif

    switch (command->type) {
    case LOOM_CMD_CLEAR:
      loom_fill_rect_clipped(tile, loom, tile_rect, visible,
                             command->data.shape.color);
      break;
    case LOOM_CMD_FILL_RECT:
      loom_fill_rect_clipped(tile, loom, tile_rect, visible,
                             command->data.shape.color);
      break;
    case LOOM_CMD_FILL_RECT_LINEAR_GRADIENT:
      loom_fill_rect_linear_gradient_clipped(
          tile, loom, tile_rect, visible, &command->data.shape.linear_gradient);
      break;
    case LOOM_CMD_STROKE_RECT:
      loom_raster_stroke_rect(tile, loom, tile_rect, command->data.shape.rect,
                              command->data.shape.stroke.width, visible,
                              command->data.shape.stroke.color);
      break;
    case LOOM_CMD_FILL_ROUND_RECT:
      loom_raster_fill_round_rect(
          tile, loom, tile_rect, command->data.shape.rect,
          command->data.shape.radius, visible, command->data.shape.color);
      break;
    case LOOM_CMD_FILL_ROUND_RECT_LINEAR_GRADIENT:
      loom_raster_fill_round_rect_linear_gradient(
          tile, loom, tile_rect, command->data.shape.rect,
          command->data.shape.radius, visible,
          &command->data.shape.linear_gradient);
      break;
    case LOOM_CMD_STROKE_ROUND_RECT:
      loom_raster_stroke_round_rect(
          tile, loom, tile_rect, command->data.shape.rect,
          command->data.shape.radius, command->data.shape.stroke.width, visible,
          command->data.shape.stroke.color);
      break;
    case LOOM_CMD_FILL_CIRCLE:
      loom_raster_fill_circle(tile, loom, tile_rect,
                              command->data.circle.center,
                              command->data.circle.radius, visible,
                              command->data.circle.color);
      break;
    case LOOM_CMD_FILL_CIRCLE_RADIAL_GRADIENT:
      loom_raster_fill_circle_radial_gradient(
          tile, loom, tile_rect, command->data.circle.center,
          command->data.circle.radius, visible,
          &command->data.circle.radial_gradient);
      break;
    case LOOM_CMD_STROKE_CIRCLE:
      loom_raster_stroke_circle(tile, loom, tile_rect,
                                command->data.circle.center,
                                command->data.circle.radius,
                                command->data.circle.stroke.width, visible,
                                command->data.circle.stroke.color);
      break;
    case LOOM_CMD_LINE:
      loom_raster_draw_line(tile, loom, tile_rect, command->data.line.p0,
                            command->data.line.p1, command->data.line.stroke,
                            visible);
      break;
    case LOOM_CMD_ARC:
      loom_raster_draw_arc(tile, loom, tile_rect, command, visible);
      break;
    case LOOM_CMD_ARC_GRADIENT:
      loom_raster_draw_arc_gradient(tile, loom, tile_rect, command, visible);
      break;
    case LOOM_CMD_BITMAP:
      loom_raster_draw_bitmap(tile, loom, tile_rect, &command->data.bitmap,
                              visible);
      break;
    case LOOM_CMD_TEXT:
      loom_raster_draw_text(tile, loom, tile_rect, &command->data.text,
                            visible);
      break;
    default:
      break;
    }
  }

  return LOOM_OK;
}
