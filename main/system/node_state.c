#include "node_state.h"

#include "esp_log.h"

static const char *TAG = "node_state";
static volatile node_state_t s_state = NODE_BOOT;

void node_state_set(node_state_t state)
{
    s_state = state;
    ESP_LOGI(TAG, "state=%s", node_state_to_string(state));
}

node_state_t node_state_get(void)
{
    return s_state;
}

const char *node_state_to_string(node_state_t state)
{
    switch (state) {
    case NODE_BOOT: return "BOOT";
    case NODE_CAMERA_INIT: return "CAMERA_INIT";
    case NODE_IDLE: return "IDLE";
    case NODE_RECORDING: return "RECORDING";
    case NODE_FLUSHING: return "FLUSHING";
    case NODE_FINISHED: return "FINISHED";
    case NODE_STOPPED: return "STOPPED";
    case NODE_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}
