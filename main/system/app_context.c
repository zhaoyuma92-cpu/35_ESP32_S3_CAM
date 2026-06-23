#include "app_context.h"

#include <string.h>

static app_context_t s_ctx;

void app_context_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    app_config_load_defaults(&s_ctx.config);
    app_config_load_from_nvs(&s_ctx.config);   // overwrite with saved values if present
    s_ctx.start_trigger_sem = xSemaphoreCreateBinary();
}

app_context_t *app_context_get(void)
{
    return &s_ctx;
}
