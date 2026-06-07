#ifndef NODE_STATE_H
#define NODE_STATE_H

typedef enum {
    NODE_BOOT = 0,
    NODE_CAMERA_INIT,
    NODE_IDLE,
    NODE_RECORDING,
    NODE_FLUSHING,
    NODE_FINISHED,
    NODE_STOPPED,
    NODE_ERROR,
} node_state_t;

void node_state_set(node_state_t state);
node_state_t node_state_get(void);
const char *node_state_to_string(node_state_t state);

#endif
