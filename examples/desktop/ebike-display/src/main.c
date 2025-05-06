#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../dependencies/minifb/include/MiniFB.h"
#include "loom.h"

int main(void)
{
  struct mfb_window *window = mfb_open("peak", 240 * 2, 320 * 2);
  if (!window)
    return 0;

  uint32_t *buffer = malloc(240 * 320 * sizeof(uint32_t));
  if (!buffer)
  {
    mfb_close(window);
    return 0;
  }

  loom_link_buffer(buffer, 240, 320, LOOM_32_BIT);

  srand(time(NULL));

  for (int i = 0; i < 240 * 320; i++)
  {
    uint8_t r = rand() % 256;
    uint8_t g = rand() % 256;
    uint8_t b = rand() % 256;
    uint32_t color = RGB888(r, g, b);
    // buffer[i] = color;
    loom_draw_pixel(i % 240, i / 240, color);
  }

  do
  {
    int state;
    state = mfb_update_ex(window, buffer, 240, 320);

    loom_clear_buffer_color(RGB888(255, 128, 128));

    if (state < 0)
    {
      window = NULL;
      break;
    }
  } while (mfb_wait_sync(window));
  return 0;
}