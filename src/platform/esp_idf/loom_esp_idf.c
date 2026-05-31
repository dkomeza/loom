#include "loom/loom_esp_idf.h"

#include "driver/ppa.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "loom";

#define LOOM_ESP_IDF_INTERNAL_CAPS \
  (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define LOOM_ESP_IDF_TILE_FAST_CAPS \
  (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT)
#define LOOM_ESP_IDF_TILE_FALLBACK_CAPS \
  (MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT)

#ifndef LOOM_ENABLE_PERF_LOG
#define LOOM_ENABLE_PERF_LOG 0
#endif

#ifndef LOOM_ENABLE_DEBUG_LOG
#define LOOM_ENABLE_DEBUG_LOG 0
#endif

#ifndef LOOM_PERF_LOG_LEVEL
#define LOOM_PERF_LOG_LEVEL 0
#endif

#ifndef LOOM_DEBUG_LOG_LEVEL
#define LOOM_DEBUG_LOG_LEVEL 0
#endif

struct loom_esp_idf {
  esp_lcd_panel_handle_t panel;
  ppa_client_handle_t ppa_fill_client;
  SemaphoreHandle_t trans_done_sem;
  loom_t *loom;
  bool callbacks_registered;
};

#if LOOM_ENABLE_PERF_LOG
static void loom_esp_idf_log_heap(const char *phase) {
  ESP_LOGI(TAG,
           "heap %s: internal free=%u largest=%u, dma free=%u largest=%u, "
           "spiram dma free=%u largest=%u",
           phase,
           (unsigned)heap_caps_get_free_size(LOOM_ESP_IDF_INTERNAL_CAPS),
           (unsigned)heap_caps_get_largest_free_block(
               LOOM_ESP_IDF_INTERNAL_CAPS),
           (unsigned)heap_caps_get_free_size(LOOM_ESP_IDF_TILE_FAST_CAPS),
           (unsigned)heap_caps_get_largest_free_block(
               LOOM_ESP_IDF_TILE_FAST_CAPS),
           (unsigned)heap_caps_get_free_size(LOOM_ESP_IDF_TILE_FALLBACK_CAPS),
           (unsigned)heap_caps_get_largest_free_block(
               LOOM_ESP_IDF_TILE_FALLBACK_CAPS));
}
#endif

static bool IRAM_ATTR loom_esp_idf_color_trans_done_cb(
    esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata,
    void *user_ctx) {
  (void)panel;
  (void)edata;

  loom_esp_idf_t *backend = (loom_esp_idf_t *)user_ctx;
  if (backend == NULL || backend->trans_done_sem == NULL) {
    return false;
  }

  BaseType_t need_yield = pdFALSE;
  xSemaphoreGiveFromISR(backend->trans_done_sem, &need_yield);
  return need_yield == pdTRUE;
}

static uint32_t loom_esp_idf_caps_for_type(loom_alloc_type_t type) {
  switch (type) {
  case LOOM_ALLOC_INTERNAL:
    return LOOM_ESP_IDF_INTERNAL_CAPS;
  case LOOM_ALLOC_TILE_FAST:
    return LOOM_ESP_IDF_TILE_FAST_CAPS;
  case LOOM_ALLOC_TILE_FALLBACK:
    return LOOM_ESP_IDF_TILE_FALLBACK_CAPS;
  default:
    return MALLOC_CAP_8BIT;
  }
}

static void *loom_esp_idf_malloc(void *ctx, size_t size,
                                 loom_alloc_type_t type) {
  (void)ctx;
  return heap_caps_malloc(size, loom_esp_idf_caps_for_type(type));
}

static void *loom_esp_idf_calloc(void *ctx, size_t count, size_t size,
                                 loom_alloc_type_t type) {
  (void)ctx;
  return heap_caps_calloc(count, size, loom_esp_idf_caps_for_type(type));
}

static void *loom_esp_idf_aligned_alloc(void *ctx, size_t alignment,
                                        size_t size, loom_alloc_type_t type) {
  (void)ctx;
  return heap_caps_aligned_alloc(alignment, size,
                                 loom_esp_idf_caps_for_type(type));
}

static void loom_esp_idf_free(void *ctx, void *ptr) {
  (void)ctx;
  heap_caps_free(ptr);
}

static int64_t loom_esp_idf_time_now_us(void *ctx) {
  (void)ctx;
  return esp_timer_get_time();
}

static void loom_esp_idf_log(void *ctx, loom_log_level_t level,
                             const char *tag, const char *message) {
  (void)ctx;
  switch (level) {
  case LOOM_LOG_ERROR:
    ESP_LOGE(tag, "%s", message);
    break;
  case LOOM_LOG_WARN:
    ESP_LOGW(tag, "%s", message);
    break;
  case LOOM_LOG_INFO:
    ESP_LOGI(tag, "%s", message);
    break;
  default:
    ESP_LOGD(tag, "%s", message);
    break;
  }
}

static loom_err_t loom_esp_idf_flush_start(void *ctx, const void *pixels,
                                           loom_rect_t rect,
                                           loom_pixel_format_t format) {
  loom_esp_idf_t *backend = (loom_esp_idf_t *)ctx;
  if (backend == NULL || pixels == NULL || backend->panel == NULL ||
      backend->trans_done_sem == NULL || rect.w <= 0 || rect.h <= 0) {
    return LOOM_ERR_INVALID_ARG;
  }
  if (format != LOOM_PIXEL_FORMAT_RGB888) {
    return LOOM_ERR_NOT_SUPPORTED;
  }

  while (xSemaphoreTake(backend->trans_done_sem, 0) == pdTRUE) {
  }

#if LOOM_ENABLE_PERF_LOG && LOOM_PERF_LOG_LEVEL <= 0
  int64_t draw_start_us = esp_timer_get_time();
#endif
  esp_err_t ret = esp_lcd_panel_draw_bitmap(backend->panel, rect.x, rect.y,
                                            rect.x + rect.w, rect.y + rect.h,
                                            pixels);
#if LOOM_ENABLE_PERF_LOG && LOOM_PERF_LOG_LEVEL <= 0
  ESP_LOGD(TAG, "flush start rect=%d,%d %dx%d ret=%s submit=%lld us", rect.x,
           rect.y, rect.w, rect.h, esp_err_to_name(ret),
           (long long)(esp_timer_get_time() - draw_start_us));
#endif
  return ret == ESP_OK ? LOOM_OK : LOOM_ERR_PLATFORM;
}

static loom_err_t loom_esp_idf_flush_wait(void *ctx) {
  loom_esp_idf_t *backend = (loom_esp_idf_t *)ctx;
  if (backend == NULL || backend->trans_done_sem == NULL) {
    return LOOM_ERR_INVALID_ARG;
  }

#if LOOM_ENABLE_PERF_LOG && LOOM_PERF_LOG_LEVEL <= 0
  int64_t wait_start_us = esp_timer_get_time();
#endif
  if (xSemaphoreTake(backend->trans_done_sem, portMAX_DELAY) != pdTRUE) {
    return LOOM_ERR_TIMEOUT;
  }
#if LOOM_ENABLE_PERF_LOG && LOOM_PERF_LOG_LEVEL <= 0
  ESP_LOGD(TAG, "flush wait=%lld us",
           (long long)(esp_timer_get_time() - wait_start_us));
#endif
  return LOOM_OK;
}

static loom_err_t loom_esp_idf_fill_rgb888(void *ctx, uint8_t *pixels,
                                           size_t buffer_size, uint16_t width,
                                           uint16_t height, loom_rect_t rect,
                                           loom_color_t color) {
  loom_esp_idf_t *backend = (loom_esp_idf_t *)ctx;
  if (backend == NULL || backend->ppa_fill_client == NULL || pixels == NULL ||
      width == 0 || height == 0 || rect.w <= 0 || rect.h <= 0) {
    return LOOM_ERR_INVALID_ARG;
  }

  ppa_fill_oper_config_t config = {
      .out.buffer = pixels,
      .out.buffer_size = buffer_size,
      .out.pic_w = width,
      .out.pic_h = height,
      .out.block_offset_x = (uint32_t)rect.x,
      .out.block_offset_y = (uint32_t)rect.y,
      .out.fill_cm = PPA_FILL_COLOR_MODE_RGB888,
      .fill_block_w = (uint32_t)rect.w,
      .fill_block_h = (uint32_t)rect.h,
      .fill_argb_color = {
          .a = color.a,
          .r = color.b,
          .g = color.g,
          .b = color.r,
      },
      .mode = PPA_TRANS_MODE_BLOCKING,
  };

  esp_err_t ret = ppa_do_fill(backend->ppa_fill_client, &config);
#if LOOM_ENABLE_DEBUG_LOG && LOOM_DEBUG_LOG_LEVEL <= 0
  ESP_LOGD(TAG, "ppa fill rect=%d,%d %dx%d buffer=%u ret=%s", rect.x, rect.y,
           rect.w, rect.h, (unsigned)buffer_size, esp_err_to_name(ret));
#endif
  return ret == ESP_OK ? LOOM_OK : LOOM_ERR_PLATFORM;
}

esp_err_t loom_err_to_esp_err(loom_err_t err) {
  switch (err) {
  case LOOM_OK:
    return ESP_OK;
  case LOOM_ERR_INVALID_ARG:
    return ESP_ERR_INVALID_ARG;
  case LOOM_ERR_INVALID_STATE:
    return ESP_ERR_INVALID_STATE;
  case LOOM_ERR_NO_MEM:
    return ESP_ERR_NO_MEM;
  case LOOM_ERR_NOT_SUPPORTED:
    return ESP_ERR_NOT_SUPPORTED;
  case LOOM_ERR_TIMEOUT:
    return ESP_ERR_TIMEOUT;
  default:
    return ESP_FAIL;
  }
}

esp_err_t loom_esp_idf_create(const loom_esp_idf_config_t *config,
                              loom_esp_idf_t **out_backend, loom_t **out_loom) {
  if (config == NULL || out_backend == NULL || out_loom == NULL ||
      config->panel == NULL || config->width == 0 || config->height == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  *out_backend = NULL;
  *out_loom = NULL;

#if LOOM_ENABLE_PERF_LOG
  ESP_LOGI(TAG,
           "create esp-idf backend %ux%u format=%d tile_height=%u buffers=%u "
           "commands=%u",
           (unsigned)config->width, (unsigned)config->height,
           (int)config->format, (unsigned)config->tile_height,
           (unsigned)config->buffer_count, (unsigned)config->command_capacity);
  loom_esp_idf_log_heap("before create");
#endif

  loom_esp_idf_t *backend =
      heap_caps_calloc(1, sizeof(*backend), LOOM_ESP_IDF_INTERNAL_CAPS);
  if (backend == NULL) {
    return ESP_ERR_NO_MEM;
  }

  backend->panel = config->panel;
  backend->trans_done_sem = xSemaphoreCreateBinary();
  if (backend->trans_done_sem == NULL) {
    loom_esp_idf_destroy(backend);
    return ESP_ERR_NO_MEM;
  }

  ppa_client_config_t ppa_config = {
      .oper_type = PPA_OPERATION_FILL,
      .max_pending_trans_num = 1,
  };
  esp_err_t ppa_ret = ppa_register_client(&ppa_config, &backend->ppa_fill_client);
  if (ppa_ret != ESP_OK) {
    ESP_LOGW(TAG, "PPA fill unavailable, using CPU fills: %s",
             esp_err_to_name(ppa_ret));
    backend->ppa_fill_client = NULL;
#if LOOM_ENABLE_PERF_LOG
  } else {
    ESP_LOGI(TAG, "PPA fill client registered");
#endif
  }

  esp_lcd_dpi_panel_event_callbacks_t callbacks = {
      .on_color_trans_done = loom_esp_idf_color_trans_done_cb,
  };
  esp_err_t ret =
      esp_lcd_dpi_panel_register_event_callbacks(config->panel, &callbacks,
                                                 backend);
  if (ret != ESP_OK) {
    loom_esp_idf_destroy(backend);
    return ret;
  }
  backend->callbacks_registered = true;

  loom_display_config_t loom_config = {
      .width = config->width,
      .height = config->height,
      .format = config->format,
      .tile_height = config->tile_height,
      .buffer_count = config->buffer_count,
      .command_capacity = config->command_capacity,
      .platform =
          {
              .ctx = backend,
              .malloc = loom_esp_idf_malloc,
              .calloc = loom_esp_idf_calloc,
              .aligned_alloc = loom_esp_idf_aligned_alloc,
              .free = loom_esp_idf_free,
              .time_now_us = loom_esp_idf_time_now_us,
              .log = loom_esp_idf_log,
          },
      .flush =
          {
              .ctx = backend,
              .flush_start = loom_esp_idf_flush_start,
              .flush_wait = loom_esp_idf_flush_wait,
          },
      .hw =
          {
              .ctx = backend,
              .fill_rgb888 = backend->ppa_fill_client != NULL
                                  ? loom_esp_idf_fill_rgb888
                                  : NULL,
          },
  };

  loom_err_t loom_ret = loom_create(&loom_config, &backend->loom);
  if (loom_ret != LOOM_OK) {
    loom_esp_idf_destroy(backend);
    return loom_err_to_esp_err(loom_ret);
  }

#if LOOM_ENABLE_PERF_LOG
  loom_esp_idf_log_heap("after create");
#endif

  *out_backend = backend;
  *out_loom = backend->loom;
  return ESP_OK;
}

void loom_esp_idf_destroy(loom_esp_idf_t *backend) {
  if (backend == NULL) {
    return;
  }

  if (backend->callbacks_registered && backend->panel != NULL) {
    esp_lcd_dpi_panel_event_callbacks_t callbacks = {0};
    esp_lcd_dpi_panel_register_event_callbacks(backend->panel, &callbacks,
                                               NULL);
    backend->callbacks_registered = false;
  }

  if (backend->loom != NULL) {
    loom_destroy(backend->loom);
    backend->loom = NULL;
  }

  if (backend->ppa_fill_client != NULL) {
    ppa_unregister_client(backend->ppa_fill_client);
    backend->ppa_fill_client = NULL;
  }

  if (backend->trans_done_sem != NULL) {
    vSemaphoreDelete(backend->trans_done_sem);
    backend->trans_done_sem = NULL;
  }

  heap_caps_free(backend);
}
