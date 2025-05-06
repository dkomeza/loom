#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "core/core.h"
#include "loom.h"

typedef struct
{
  void *buffer;
  int width;
  int height;
  loom_color_space_t color_space;
} loom_buffer_t;
static loom_buffer_t loom_buffer = {0};

void loom_link_buffer(void *buffer, int width, int height, loom_color_space_t color_space)
{
  if (buffer == NULL || width <= 0 || height <= 0)
    return;

  loom_buffer.color_space = color_space;
  loom_buffer.buffer = buffer;
  loom_buffer.width = width;
  loom_buffer.height = height;
}

void loom_draw_pixel(int x, int y, uint32_t color)
{
  if (x < 0 || x >= loom_buffer.width || y < 0 || y >= loom_buffer.height)
    return;
  if (loom_buffer.buffer == NULL)
    return;

  if (loom_buffer.color_space == LOOM_16_BIT)
  {
    uint16_t *buffer = (uint16_t *)loom_buffer.buffer;
    buffer[y * loom_buffer.width + x] = (uint16_t)color;
  }
  else
  {
    uint32_t *buffer = (uint32_t *)loom_buffer.buffer;
    buffer[y * loom_buffer.width + x] = color;
  }
}

void loom_clear_buffer(void)
{
  if (loom_buffer.buffer == NULL)
    return;

  if (loom_buffer.color_space == LOOM_16_BIT)
    memset(loom_buffer.buffer, 0, loom_buffer.width * loom_buffer.height * sizeof(uint16_t));
  else
    memset(loom_buffer.buffer, 0, loom_buffer.width * loom_buffer.height * sizeof(uint32_t));
}

void loom_clear_buffer_color(uint32_t color)
{
  if (loom_buffer.buffer == NULL)
    return;

  if (loom_buffer.color_space == LOOM_16_BIT)
  {
    uint16_t *buffer = (uint16_t *)loom_buffer.buffer;
    for (int i = 0; i < loom_buffer.width * loom_buffer.height; i++)
      buffer[i] = (uint16_t)color;
  }
  else
  {
    uint32_t *buffer = (uint32_t *)loom_buffer.buffer;
    for (int i = 0; i < loom_buffer.width * loom_buffer.height; i++)
      buffer[i] = color;
  }
}

void loom_draw_h_line(int x, int y, int length, uint32_t color)
{
  if (x < 0 || x >= loom_buffer.width || y < 0 || y >= loom_buffer.height)
    return;
  if (length <= 0)
    return;

  for (int i = 0; i < length; i++)
    loom_draw_pixel(x + i, y, color);
}
void loom_draw_v_line(int x, int y, int length, uint32_t color)
{
  if (x < 0 || x >= loom_buffer.width || y < 0 || y >= loom_buffer.height)
    return;
  if (length <= 0)
    return;

  for (int i = 0; i < length; i++)
    loom_draw_pixel(x, y + i, color);
}
void loom_draw_rect(int x, int y, int width, int height, uint32_t color)
{
  if (x < 0 || x >= loom_buffer.width || y < 0 || y >= loom_buffer.height)
    return;
  if (width <= 0 || height <= 0)
    return;

  for (int i = 0; i < width; i++)
    loom_draw_pixel(x + i, y, color);
  for (int i = 0; i < height; i++)
    loom_draw_pixel(x, y + i, color);
  for (int i = 0; i < width; i++)
    loom_draw_pixel(x + i, y + height - 1, color);
  for (int i = 0; i < height; i++)
    loom_draw_pixel(x + width - 1, y + i, color);
}
