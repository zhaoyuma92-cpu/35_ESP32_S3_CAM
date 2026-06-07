#ifndef ACQ_MANAGER_H
#define ACQ_MANAGER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "app_context.h"

esp_err_t acq_manager_init(app_context_t *ctx);
esp_err_t acq_manager_start(app_context_t *ctx);
esp_err_t acq_manager_wait_done(app_context_t *ctx, TickType_t timeout);

#endif
