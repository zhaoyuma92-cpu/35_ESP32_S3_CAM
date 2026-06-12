#ifndef HTTP_API_H
#define HTTP_API_H

#include <stdbool.h>

#include "esp_err.h"

bool http_api_offline_start_requested(void);
void http_api_clear_offline_start(void);
void http_api_set_offline_start(void);

esp_err_t http_api_register(void *server);

#endif
