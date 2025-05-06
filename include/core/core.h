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

/**
 * @brief Draw a single pixel on the screen.
 * @param x The x-coordinate of the pixel.
 * @param y The y-coordinate of the pixel.
 * @param color The color of the pixel.
 * @note This function draws a pixel on the screen at the specified coordinates.
 *       If the coordinates are out of bounds, the function does nothing.
 *       The color is a 32-bit unsigned integer representing the pixel color.
 *       The color format is determined by the color space of the screen buffer.
 *       The function supports both 16-bit and 32-bit color spaces.
 *       In 16-bit color space, the color is converted to 16-bit format.
 *       In 32-bit color space, the color is used as is.
 */
void loom_draw_pixel(int x, int y, uint32_t color);

/**
 * @brief Draw a horizontal line on the screen.
 * @param x The x-coordinate of the starting point of the line.
 * @param y The y-coordinate of the starting point of the line.
 * @param length The length of the line.
 * @param color The color of the line.
 * @note This function draws a horizontal line on the screen starting from the specified coordinates.
 *       If the starting coordinates are out of bounds, the function does nothing.
 *       The length of the line is specified in pixels.
 */
void loom_draw_h_line(int x, int y, int length, uint32_t color);
/**
 * @brief Draw a vertical line on the screen.
 * @param x The x-coordinate of the starting point of the line.
 * @param y The y-coordinate of the starting point of the line.
 * @param length The length of the line.
 * @param color The color of the line.
 * @note This function draws a vertical line on the screen starting from the specified coordinates.
 *       If the starting coordinates are out of bounds, the function does nothing.
 *       The length of the line is specified in pixels.
 */
void loom_draw_v_line(int x, int y, int length, uint32_t color);

#endif