#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../dependencies/minifb/include/MiniFB.h"
#include "include/loom.h"

int main(void)
{
  struct mfb_window *window = mfb_open_ex("peak", 240, 320, 0);
  if (!window)
    return 0;

  uint16_t *buffer = (uint16_t *)malloc(800 * 600 * 4);

  do
  {
    int state;

    state = mfb_update_ex(window, buffer, 800, 600);

    if (state < 0)
    {
      window = NULL;
      break;
    }
  } while (mfb_wait_sync(window));
  return 0;
}