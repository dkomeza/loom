#ifndef LOOM_ESP_IDF_H
#define LOOM_ESP_IDF_H

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "loom/loom.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct loom_esp_idf loom_esp_idf_t;

typedef struct {
  uint16_t width;
  uint16_t height;
  loom_pixel_format_t format;
  uint16_t tile_height;
  uint8_t buffer_count;
  size_t command_capacity;
  esp_lcd_panel_handle_t panel;
} loom_esp_idf_config_t;

esp_err_t loom_esp_idf_create(const loom_esp_idf_config_t *config,
                              loom_esp_idf_t **out_backend, loom_t **out_loom);
void loom_esp_idf_destroy(loom_esp_idf_t *backend);
esp_err_t loom_err_to_esp_err(loom_err_t err);

#ifdef __cplusplus
}
#endif

#endif
