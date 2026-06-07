#ifndef SDCARD_H
#define SDCARD_H

#include "esp_err.h"

esp_err_t sdcard_mount(void);
void      sdcard_unmount(void);

#endif /* SDCARD_H */
