#ifndef P4_CAMERA_H
#define P4_CAMERA_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "app_config.h"

typedef struct {
    const uint8_t *data;
    size_t len;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    app_pixel_format_t pixel_format;
    uint32_t sequence;
    int64_t t_us;
} p4_camera_frame_t;

esp_err_t p4_camera_init(const app_config_t *cfg);
esp_err_t p4_camera_get_frame(p4_camera_frame_t *frame, TickType_t timeout);
void p4_camera_return_frame(const p4_camera_frame_t *frame);

// 诊断用：读取并打印 OV5647 当前曝光(0x3500-0x3502)/增益(0x350a-0x350b)寄存器值。
// 用来确认帧周期残留抖动是不是传感器自动曝光(AEC/AGC)在帧间微调曝光行数导致的——
// 如果多次调用之间这些值在变化，说明AEC处于自动模式，正在悄悄改变有效帧长。
void p4_camera_log_aec_state(const char *tag);

// 读取当前曝光行数(0x3500-0x3502)/增益(0x350a-0x350b)——不管当前是自动还是
// 手动锁定模式，都是读传感器里现在生效的值。
esp_err_t p4_camera_get_exposure(uint32_t *exposure_lines, uint32_t *gain);

// 把AEC/AGC切到手动模式并写入指定的曝光行数/增益，之后传感器不会再自动调整，
// 直到下次调用本函数或重新开机。给HTTP API用，方便以后从手机界面手动设置固定曝光。
esp_err_t p4_camera_set_exposure(uint32_t exposure_lines, uint32_t gain);

#endif
