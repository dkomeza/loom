#ifndef LOOM_CORE_H
#define LOOM_CORE_H

#include <stdint.h>

/**
 * @brief Clear the screen buffer.
 * @note This function clears the screen buffer to the default color. (black)
 */
void loom_clear_buffer(void);
/**
 * @brief Clear the screen buffer to a specific color.
 * @param color The color to clear the screen buffer to.
 */
void loom_clear_buffer_color(uint32_t color);

void loom_draw_pixel(int x, int y, uint32_t color);

#endif