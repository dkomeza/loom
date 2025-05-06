#ifndef LOOM_H
#define LOOM_H

#include "core/core.h"

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))
#define RGB888(r, g, b) ((uint32_t)((((r) & 0xFF) << 16) | (((g) & 0xFF) << 8) | ((b) & 0xFF)))

typedef enum
{
  LOOM_16_BIT,
  LOOM_32_BIT,
} loom_color_space_t;

/**
 * @brief Link a buffer to the Loom library.
 *
 * This function links a buffer to the Loom library for rendering.
 *
 * @param buffer Pointer to the buffer to be linked.
 * @param width Width of the buffer.
 * @param height Height of the buffer.
 * @param color_space Color space of the buffer (16-bit or 32-bit).
 */
void loom_link_buffer(void *buffer, int width, int height, loom_color_space_t color_space);

#endif