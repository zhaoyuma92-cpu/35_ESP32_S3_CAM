#include "p4_camera.h"

#include <inttypes.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "p4_camera";

static app_config_t s_cfg;

/* ============================================================
 * FAKE CAMERA BACKEND (compile-time selected via Kconfig)
 * ============================================================ */

#if CONFIG_DISP_FAKE_CAMERA

static uint8_t *s_frame;
static size_t s_frame_len;
static uint32_t s_sequence;
static TickType_t s_wake;

static void draw_disk(uint8_t *frame, int cx, int cy, int radius, uint8_t value)
{
    const int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= s_cfg.frame_height) {
            continue;
        }
        uint8_t *row = frame + (size_t)y * s_cfg.frame_stride;
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy > r2) {
                continue;
            }
            int x = cx + dx;
            if (x >= 0 && x < s_cfg.frame_width) {
                row[x] = value;
            }
        }
    }
}

static int triangular_offset(uint32_t seq, uint32_t period, int amplitude)
{
    uint32_t p = seq % period;
    int half = (int)period / 2;
    int v = (p < (uint32_t)half) ? (int)p : (int)period - (int)p;
    return (v * 2 * amplitude / half) - amplitude;
}

static void generate_fake_frame(void)
{
    memset(s_frame, 210, s_frame_len);

    int off_a = triangular_offset(s_sequence, 60, 14);
    int off_b = triangular_offset(s_sequence + 15, 70, 10);

    for (int i = 0; i < APP_TARGET_COUNT; i++) {
        const roi_config_t *roi = &s_cfg.roi[i];
        if (!roi->enabled) {
            continue;
        }
        int cx = (roi->ref_x_q8 >> 8) + off_a;
        int cy = (roi->ref_y_q8 >> 8) + ((i & 1) ? off_b : -off_b);
        draw_disk(s_frame, cx, cy, 18, 35);
    }
}

esp_err_t p4_camera_init(const app_config_t *cfg)
{
    s_cfg = *cfg;
    s_cfg.frame_width  = CONFIG_DISP_FAKE_FRAME_WIDTH;
    s_cfg.frame_height = CONFIG_DISP_FAKE_FRAME_HEIGHT;
    s_cfg.frame_stride = CONFIG_DISP_FAKE_FRAME_WIDTH;

    s_frame_len = (size_t)s_cfg.frame_stride * s_cfg.frame_height;
    s_frame = heap_caps_malloc(s_frame_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_frame) {
        s_frame = heap_caps_malloc(s_frame_len, MALLOC_CAP_8BIT);
    }
    if (!s_frame) {
        ESP_LOGE(TAG, "fake frame allocation failed: %u bytes", (unsigned)s_frame_len);
        return ESP_ERR_NO_MEM;
    }
    s_sequence = 0;
    s_wake = xTaskGetTickCount();
    ESP_LOGW(TAG, "using fake camera backend, not real OV5647/MIPI");
    ESP_LOGI(TAG, "fake frame: %ux%u stride=%u fps=%u",
             s_cfg.frame_width, s_cfg.frame_height, s_cfg.frame_stride,
             s_cfg.frame_rate_hz);
    return ESP_OK;
}

esp_err_t p4_camera_get_frame(p4_camera_frame_t *frame, TickType_t timeout)
{
    (void)timeout;
    vTaskDelayUntil(&s_wake, pdMS_TO_TICKS(app_config_frame_period_ms(&s_cfg)));
    generate_fake_frame();

    frame->data         = s_frame;
    frame->len          = s_frame_len;
    frame->width        = s_cfg.frame_width;
    frame->height       = s_cfg.frame_height;
    frame->stride       = s_cfg.frame_stride;
    frame->pixel_format = s_cfg.pixel_format;
    frame->sequence     = s_sequence++;
    frame->t_us         = esp_timer_get_time();
    return ESP_OK;
}

void p4_camera_return_frame(const p4_camera_frame_t *frame)
{
    (void)frame;
}

void p4_camera_log_aec_state(const char *tag)
{
    (void)tag; /* no real sensor in fake-camera builds */
}

/* ============================================================
 * REAL CAMERA BACKEND — OV5647 via MIPI-CSI + ISP bypass
 * ============================================================
 *
 * Data path:
 *   OV5647 sensor (I2C SDA=GPIO7, SCL=GPIO8)
 *   → MIPI D-PHY 2-lane, 200 Mbps/lane
 *   → ESP32-P4 CSI controller (RAW8 in, RAW8 out)
 *   → ISP processor (bypass, RAW8 passthrough)
 *   → DMA → PSRAM frame buffer
 *
 * Double buffering: while one buffer is being processed by the
 * ROI tracker, the CSI/ISP fills the other. If the tracker is
 * slower than 20 ms/frame, frames are silently skipped — the
 * most recent complete frame is always returned.
 */
#else  /* !CONFIG_DISP_FAKE_CAMERA */

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/isp.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_cam_sensor.h"
#include "esp_cam_sensor_detect.h"
#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_sccb_i2c.h"
#include "esp_sccb_intf.h"
#include "board_config.h"

#define CAM_FRAME_W     BOARD_DEFAULT_FRAME_WIDTH
#define CAM_FRAME_H     BOARD_DEFAULT_FRAME_HEIGHT
#ifndef BOARD_CAM_BITS_PER_PIXEL
#define BOARD_CAM_BITS_PER_PIXEL 8
#endif
#ifndef BOARD_CAM_FRAME_BITS_PER_PIXEL
#define BOARD_CAM_FRAME_BITS_PER_PIXEL BOARD_CAM_BITS_PER_PIXEL
#endif
#ifndef BOARD_CAM_PIXEL_FORMAT
#define BOARD_CAM_PIXEL_FORMAT APP_PIXEL_FORMAT_GRAY8
#endif
#ifndef BOARD_CAM_OV5647_VTS_OVERRIDE
#define BOARD_CAM_OV5647_VTS_OVERRIDE 0
#endif
#define CAM_FRAME_SIZE  (((size_t)(CAM_FRAME_W) * (CAM_FRAME_H) * BOARD_CAM_FRAME_BITS_PER_PIXEL) / 8)
#define CAM_NUM_BUFS    2
/* 64-byte DMA alignment sufficient for ESP32-P4 */
#define CAM_BUF_ALIGN   64

typedef struct {
    uint8_t              *bufs[CAM_NUM_BUFS];
    volatile bool         buf_locked[CAM_NUM_BUFS]; /* true = app owns this buf */
    volatile int          ready_idx;                /* buf with latest full frame */
    volatile int          fill_idx;                 /* buf currently being filled */
    SemaphoreHandle_t     ready_sem;
    esp_cam_ctlr_handle_t cam_handle;
    isp_proc_handle_t     isp_handle;
    esp_ldo_channel_handle_t ldo_handle;
    i2c_master_bus_handle_t  i2c_bus;
    esp_sccb_io_handle_t     sccb_io;
    volatile uint32_t     sequence;
    volatile int64_t      ready_t_us;                /* esp_timer_get_time() at DMA-finished ISR */
    esp_cam_sensor_device_t *sensor_dev;              /* kept for AEC/AGC register diagnostics */
} real_cam_t;

static real_cam_t s_cam;

static esp_err_t ov5647_write_reg(esp_cam_sensor_device_t *cam, uint16_t reg, uint8_t value)
{
    esp_cam_sensor_reg_val_t regval = {
        .regaddr = reg,
        .value = value,
    };
    return esp_cam_sensor_ioctl(cam, ESP_CAM_SENSOR_IOC_S_REG, &regval);
}

static esp_err_t ov5647_read_reg(esp_cam_sensor_device_t *cam, uint16_t reg, uint8_t *value)
{
    esp_cam_sensor_reg_val_t regval = {
        .regaddr = reg,
    };
    esp_err_t err = esp_cam_sensor_ioctl(cam, ESP_CAM_SENSOR_IOC_G_REG, &regval);
    if (err == ESP_OK) {
        *value = (uint8_t)regval.value;
    }
    return err;
}

void p4_camera_log_aec_state(const char *tag)
{
    if (!s_cam.sensor_dev) {
        ESP_LOGW(TAG, "%s: sensor not ready, skip AEC read", tag);
        return;
    }
    uint8_t e0 = 0, e1 = 0, e2 = 0, g0 = 0, g1 = 0;
    ov5647_read_reg(s_cam.sensor_dev, 0x3500, &e0);
    ov5647_read_reg(s_cam.sensor_dev, 0x3501, &e1);
    ov5647_read_reg(s_cam.sensor_dev, 0x3502, &e2);
    ov5647_read_reg(s_cam.sensor_dev, 0x350a, &g0);
    ov5647_read_reg(s_cam.sensor_dev, 0x350b, &g1);
    uint32_t exposure_lines = ((uint32_t)(e0 & 0x0f) << 16) | ((uint32_t)e1 << 8) | e2;
    uint32_t gain = ((uint32_t)(g0 & 0x03) << 8) | g1;
    ESP_LOGI(TAG, "%s: AEC exposure_lines=%" PRIu32 " (raw %02x %02x %02x) gain=%" PRIu32 " (raw %02x %02x)",
             tag, exposure_lines, e0, e1, e2, gain, g0, g1);
}

static esp_err_t ov5647_apply_timing_override(esp_cam_sensor_device_t *cam)
{
#if BOARD_CAM_OV5647_VTS_OVERRIDE > 0
    const uint16_t vts = BOARD_CAM_OV5647_VTS_OVERRIDE;
    ESP_RETURN_ON_ERROR(ov5647_write_reg(cam, 0x380e, (uint8_t)(vts >> 8)),
                        TAG, "write OV5647 VTS high failed");
    ESP_RETURN_ON_ERROR(ov5647_write_reg(cam, 0x380f, (uint8_t)(vts & 0xff)),
                        TAG, "write OV5647 VTS low failed");

    uint8_t vts_h = 0;
    uint8_t vts_l = 0;
    ESP_RETURN_ON_ERROR(ov5647_read_reg(cam, 0x380e, &vts_h),
                        TAG, "read OV5647 VTS high failed");
    ESP_RETURN_ON_ERROR(ov5647_read_reg(cam, 0x380f, &vts_l),
                        TAG, "read OV5647 VTS low failed");
    ESP_LOGI(TAG, "OV5647 VTS override: %u -> readback=%u (target %u fps)",
             vts, ((uint16_t)vts_h << 8) | vts_l, BOARD_DEFAULT_FRAME_RATE_HZ);
#else
    (void)cam;
#endif
    return ESP_OK;
}

/* ---- ISR callbacks (must be in IRAM) ---- */

static bool IRAM_ATTR on_new_trans(esp_cam_ctlr_handle_t handle,
                                   esp_cam_ctlr_trans_t *trans,
                                   void *user_data)
{
    /* Provide the buffer NOT currently locked by the app */
    int next = s_cam.fill_idx ^ 1;
    if (s_cam.buf_locked[next]) {
        /* App still reading the "other" buf — reuse fill_idx (drop frame) */
        next = s_cam.fill_idx;
    }
    trans->buffer = s_cam.bufs[next];
    trans->buflen  = CAM_FRAME_SIZE;
    s_cam.fill_idx = next;
    return false;
}

static bool IRAM_ATTR on_trans_finished(esp_cam_ctlr_handle_t handle,
                                        esp_cam_ctlr_trans_t *trans,
                                        void *user_data)
{
    /* Record which buffer just finished and wake the consumer.
     * Timestamp here, at the real DMA-finished instant — NOT later in
     * p4_camera_get_frame(), which only runs after the consumer task wakes
     * up and gets scheduled (subject to task-switch / other-task jitter). */
    s_cam.ready_t_us = esp_timer_get_time();
    for (int i = 0; i < CAM_NUM_BUFS; i++) {
        if (trans->buffer == s_cam.bufs[i]) {
            s_cam.ready_idx = i;
            break;
        }
    }
    s_cam.sequence++;

    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_cam.ready_sem, &woken);
    return woken == pdTRUE;
}

/* ---- Sensor SCCB auto-detect & init ---- */

static esp_err_t sensor_init(void)
{
#if BOARD_CAM_PWDN_IO >= 0
    gpio_config_t pwdn_cfg = {
        .pin_bit_mask = (1ULL << BOARD_CAM_PWDN_IO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&pwdn_cfg);
    gpio_set_level(BOARD_CAM_PWDN_IO, 0);   /* LOW = camera active */
    vTaskDelay(pdMS_TO_TICKS(10));           /* let camera come out of powerdown */
    ESP_LOGI(TAG, "PWDN GPIO%d driven LOW — camera active", BOARD_CAM_PWDN_IO);
#else
    ESP_LOGI(TAG, "PWDN not controlled, matching Waveshare/IDF OV5647 examples");
#endif

    /* I2C master bus */
    i2c_master_bus_config_t i2c_cfg = {
        .clk_source            = I2C_CLK_SRC_DEFAULT,
        .sda_io_num            = BOARD_CAM_SCCB_SDA_IO,
        .scl_io_num            = BOARD_CAM_SCCB_SCL_IO,
        .i2c_port              = BOARD_CAM_I2C_PORT,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &s_cam.i2c_bus),
                        TAG, "I2C bus init failed");

#if CONFIG_LOG_DEFAULT_LEVEL_DEBUG
    /* I2C bus scan — prints all devices; useful during bring-up */
    {
        bool found_any = false;
        ESP_LOGD(TAG, "I2C scan on SDA=GPIO%d SCL=GPIO%d:",
                 BOARD_CAM_SCCB_SDA_IO, BOARD_CAM_SCCB_SCL_IO);
        for (uint8_t addr = 0x08; addr < 0x78; addr++) {
            if (i2c_master_probe(s_cam.i2c_bus, addr, 10) == ESP_OK) {
                ESP_LOGD(TAG, "  I2C device at 0x%02X", addr);
                found_any = true;
            }
        }
        if (!found_any) {
            ESP_LOGW(TAG, "  NO I2C devices found");
        }
    }
#endif

    /* Walk the auto-detect list built by esp_cam_sensor */
    esp_cam_sensor_config_t sensor_cfg = {
        .reset_pin  = BOARD_CAM_RESET_IO,
        .pwdn_pin   = BOARD_CAM_PWDN_IO,
        .xclk_pin   = BOARD_CAM_XCLK_IO,
        .sccb_handle = NULL,
    };

    esp_cam_sensor_device_t *cam = NULL;
    for (esp_cam_sensor_detect_fn_t *p = &__esp_cam_sensor_detect_fn_array_start;
         p < &__esp_cam_sensor_detect_fn_array_end; ++p) {

        sccb_i2c_config_t sccb_cfg = {
            .scl_speed_hz  = BOARD_CAM_SCCB_FREQ_HZ,
            .device_address = p->sccb_addr,
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        };
        esp_err_t ret = sccb_new_i2c_io(s_cam.i2c_bus, &sccb_cfg, &sensor_cfg.sccb_handle);
        if (ret != ESP_OK) {
            continue;
        }
        sensor_cfg.sensor_port = p->port;
        cam = (*(p->detect))(&sensor_cfg);
        if (cam) {
            if (p->port != ESP_CAM_SENSOR_MIPI_CSI) {
                ESP_LOGE(TAG, "sensor found but wrong interface type");
                cam = NULL;
            } else {
                s_cam.sccb_io = sensor_cfg.sccb_handle;
            }
            break;
        }
        esp_sccb_del_i2c_io(sensor_cfg.sccb_handle);
    }

    if (!cam) {
        ESP_LOGE(TAG, "OV5647 not detected — check I2C wiring (SDA=GPIO%d SCL=GPIO%d)",
                 BOARD_CAM_SCCB_SDA_IO, BOARD_CAM_SCCB_SCL_IO);
        return ESP_ERR_NOT_FOUND;
    }
    s_cam.sensor_dev = cam;

    /* Select format */
    esp_cam_sensor_format_array_t fmt_arr = {0};
    esp_cam_sensor_query_format(cam, &fmt_arr);
    const esp_cam_sensor_format_t *selected = NULL;
    for (int i = 0; i < (int)fmt_arr.count; i++) {
        const esp_cam_sensor_format_t *fmt = &fmt_arr.format_array[i];
        ESP_LOGI(TAG, "sensor fmt[%d]: %s %" PRIu32 "x%" PRIu32 " fps=%" PRIu32 " mipi_clk=%" PRIu32 " lanes=%" PRIu32,
                 i, fmt->name, (uint32_t)fmt->width, (uint32_t)fmt->height, (uint32_t)fmt->fps,
                 fmt->mipi_info.mipi_clk, (uint32_t)fmt->mipi_info.lane_num);
        if (strcmp(fmt_arr.format_array[i].name, BOARD_CAM_FORMAT_NAME) == 0) {
            selected = &fmt_arr.format_array[i];
        }
    }
    if (!selected) {
        ESP_LOGE(TAG, "format '%s' not available in sensor driver", BOARD_CAM_FORMAT_NAME);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_RETURN_ON_ERROR(esp_cam_sensor_set_format(cam, selected),
                        TAG, "set format failed");
    ESP_RETURN_ON_ERROR(ov5647_apply_timing_override(cam),
                        TAG, "sensor timing override failed");

    int stream_en = 1;
    ESP_RETURN_ON_ERROR(esp_cam_sensor_ioctl(cam, ESP_CAM_SENSOR_IOC_S_STREAM, &stream_en),
                        TAG, "start stream failed");

    ESP_LOGI(TAG, "OV5647 streaming: %s %ux%u native=%u fps effective=%u fps",
             selected->name, selected->width, selected->height,
             selected->fps, BOARD_DEFAULT_FRAME_RATE_HZ);
    return ESP_OK;
}

/* ---- Public API ---- */

esp_err_t p4_camera_init(const app_config_t *cfg)
{
    s_cfg = *cfg;
    /* Override to match real sensor output */
    s_cfg.frame_width  = CAM_FRAME_W;
    s_cfg.frame_height = CAM_FRAME_H;
    s_cfg.frame_stride = CAM_FRAME_W;
    s_cfg.frame_rate_hz = BOARD_DEFAULT_FRAME_RATE_HZ;
    s_cfg.pixel_format  = BOARD_CAM_PIXEL_FORMAT;

    memset(&s_cam, 0, sizeof(s_cam));
    s_cam.ready_idx = -1;
    s_cam.fill_idx  = 0;

    /* Semaphore: binary, starts at 0 */
    s_cam.ready_sem = xSemaphoreCreateBinary();
    if (!s_cam.ready_sem) {
        ESP_LOGE(TAG, "semaphore alloc failed");
        return ESP_ERR_NO_MEM;
    }

    /* Allocate double-buffer in PSRAM (DMA-capable) */
    for (int i = 0; i < CAM_NUM_BUFS; i++) {
        s_cam.bufs[i] = heap_caps_aligned_alloc(
            CAM_BUF_ALIGN, CAM_FRAME_SIZE,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_cam.bufs[i]) {
            ESP_LOGE(TAG, "frame buf[%d] alloc failed (%u bytes)", i,
                     (unsigned)CAM_FRAME_SIZE);
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG, "frame bufs: %p  %p  (%u bytes each)",
             s_cam.bufs[0], s_cam.bufs[1], (unsigned)CAM_FRAME_SIZE);

    /* Power the MIPI D-PHY via LDO */
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = BOARD_CAM_LDO_CHAN,
        .voltage_mv = BOARD_CAM_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &s_cam.ldo_handle),
                        TAG, "LDO init failed");
    ESP_LOGI(TAG, "MIPI LDO chan%d = %d mV", BOARD_CAM_LDO_CHAN, BOARD_CAM_LDO_MV);

    /* CSI controller */
    esp_cam_ctlr_csi_config_t csi_cfg = {
        .ctlr_id               = 0,
        .h_res                 = CAM_FRAME_W,
        .v_res                 = CAM_FRAME_H,
        .data_lane_num         = 2,
        .lane_bit_rate_mbps    = BOARD_CAM_LANE_BIT_RATE_MBPS,
#if BOARD_CAM_BITS_PER_PIXEL == 10
        .input_data_color_type = CAM_CTLR_COLOR_RAW10,
        .output_data_color_type = CAM_CTLR_COLOR_YUV422,
#else
        .input_data_color_type = CAM_CTLR_COLOR_RAW8,
        .output_data_color_type = CAM_CTLR_COLOR_RAW8,
#endif
        .queue_items           = 1,
        .byte_swap_en          = false,
    };
    ESP_RETURN_ON_ERROR(esp_cam_new_csi_ctlr(&csi_cfg, &s_cam.cam_handle),
                        TAG, "CSI controller init failed");

    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans  = on_new_trans,
        .on_trans_finished = on_trans_finished,
    };
    ESP_RETURN_ON_ERROR(
        esp_cam_ctlr_register_event_callbacks(s_cam.cam_handle, &cbs, NULL),
        TAG, "register CSI callbacks failed");

    ESP_RETURN_ON_ERROR(esp_cam_ctlr_enable(s_cam.cam_handle),
                        TAG, "CSI enable failed");

    /* ISP — RAW8 in, RAW8 out (no debayering, Bayer data treated as gray) */
    esp_isp_processor_cfg_t isp_cfg = {
        .clk_hz                = 80 * 1000 * 1000,
        .input_data_source     = ISP_INPUT_DATA_SOURCE_CSI,
#if BOARD_CAM_BITS_PER_PIXEL == 10
        .input_data_color_type = ISP_COLOR_RAW10,
        .output_data_color_type = ISP_COLOR_YUV422,
#else
        .input_data_color_type = ISP_COLOR_RAW8,
        .output_data_color_type = ISP_COLOR_RAW8,
#endif
        .has_line_start_packet = false,
        .has_line_end_packet   = false,
        .h_res                 = CAM_FRAME_W,
        .v_res                 = CAM_FRAME_H,
    };
    ESP_RETURN_ON_ERROR(esp_isp_new_processor(&isp_cfg, &s_cam.isp_handle),
                        TAG, "ISP init failed");
    ESP_RETURN_ON_ERROR(esp_isp_enable(s_cam.isp_handle),
                        TAG, "ISP enable failed");

    /* Initialise OV5647 and start streaming */
    ESP_RETURN_ON_ERROR(sensor_init(), TAG, "sensor init failed");

    /* Submit first transaction so CSI starts capturing */
    esp_cam_ctlr_trans_t first_trans = {
        .buffer = s_cam.bufs[0],
        .buflen = CAM_FRAME_SIZE,
    };
    s_cam.fill_idx = 0;
    ESP_RETURN_ON_ERROR(esp_cam_ctlr_start(s_cam.cam_handle),
                        TAG, "CSI start failed");
    ESP_RETURN_ON_ERROR(
        esp_cam_ctlr_receive(s_cam.cam_handle, &first_trans, 200),
        TAG, "initial CSI receive failed");

    ESP_LOGI(TAG, "OV5647 MIPI-CSI ready: %ux%u RAW%u in → fmt=%d %ubpp buf=%u bytes @%u fps",
             CAM_FRAME_W, CAM_FRAME_H, BOARD_CAM_BITS_PER_PIXEL,
             (int)BOARD_CAM_PIXEL_FORMAT, BOARD_CAM_FRAME_BITS_PER_PIXEL,
             (unsigned)CAM_FRAME_SIZE, BOARD_DEFAULT_FRAME_RATE_HZ);
    return ESP_OK;
}

esp_err_t p4_camera_get_frame(p4_camera_frame_t *frame, TickType_t timeout)
{
    if (xSemaphoreTake(s_cam.ready_sem, timeout) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    int idx = s_cam.ready_idx;
    if (idx < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    s_cam.buf_locked[idx] = true;

    frame->data         = s_cam.bufs[idx];
    frame->len          = CAM_FRAME_SIZE;
    frame->width        = CAM_FRAME_W;
    frame->height       = CAM_FRAME_H;
    frame->stride       = CAM_FRAME_W;
    frame->pixel_format = BOARD_CAM_PIXEL_FORMAT;
    frame->sequence     = s_cam.sequence;
    frame->t_us         = s_cam.ready_t_us;   /* DMA-finished instant, not "task woke up" instant */
    return ESP_OK;
}

void p4_camera_return_frame(const p4_camera_frame_t *frame)
{
    for (int i = 0; i < CAM_NUM_BUFS; i++) {
        if (frame->data == s_cam.bufs[i]) {
            s_cam.buf_locked[i] = false;
            return;
        }
    }
}

#endif  /* CONFIG_DISP_FAKE_CAMERA */
