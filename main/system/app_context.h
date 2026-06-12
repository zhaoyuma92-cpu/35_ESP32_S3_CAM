#ifndef APP_CONTEXT_H
#define APP_CONTEXT_H

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "app_config.h"

typedef struct {
    QueueHandle_t frame_queue;
    QueueHandle_t batch_queue;
    QueueHandle_t free_batch_queue;
    SemaphoreHandle_t start_trigger_sem;
    SemaphoreHandle_t acquisition_done_sem;
    app_config_t config;
    app_config_t run_config;
    volatile bool stop_requested;
} app_context_t;

void app_context_init(void);
app_context_t *app_context_get(void);

#endif
