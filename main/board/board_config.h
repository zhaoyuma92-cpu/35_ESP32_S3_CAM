#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define BOARD_NAME "ESP32-P4-NANO"

/* Real OV5647 camera resolution via MIPI-CSI RAW8 2-lane mode */
#define BOARD_DEFAULT_FRAME_WIDTH    800
#define BOARD_DEFAULT_FRAME_HEIGHT   640
#define BOARD_DEFAULT_FRAME_STRIDE   800
#define BOARD_DEFAULT_FRAME_RATE_HZ   50

/* I2C / SCCB bus for OV5647 (confirmed from Waveshare ESP32-P4-NANO schematic) */
#define BOARD_CAM_SCCB_SDA_IO        7
#define BOARD_CAM_SCCB_SCL_IO        8
#define BOARD_CAM_SCCB_FREQ_HZ       (100 * 1000)   /* 100 kHz */
#define BOARD_CAM_I2C_PORT           0

/* Camera power/control pins.
 * Waveshare ESP32-P4-NANO demos and Espressif OV5647 CSI tests leave
 * reset/pwdn/xclk unconnected from software. Keep PWDN disabled unless
 * bring-up logs prove this board revision needs an explicit control GPIO. */
#define BOARD_CAM_PWDN_IO           (-1)
#define BOARD_CAM_RESET_IO          (-1)
#define BOARD_CAM_XCLK_IO           (-1)

/* SDMMC IO power: Waveshare P4-NANO demo uses ESP32-P4 on-chip LDO channel 4. */
#define BOARD_SD_PWR_LDO_CHAN        4

/* MIPI PHY power supply: LDO channel 3 @ 2.5 V (from Espressif reference design) */
#define BOARD_CAM_LDO_CHAN            3
#define BOARD_CAM_LDO_MV          2500

/* CSI controller lane bit rate for OV5647 RAW8 800x640@50, matching IDF's
 * esp_driver_cam test_csi_ov5647.c. */
#define BOARD_CAM_LANE_BIT_RATE_MBPS  200

/* OV5647 camera sensor format string */
#define BOARD_CAM_FORMAT_NAME  "MIPI_2lane_24Minput_RAW8_800x640_50fps"

/* SD card, SDMMC 4-bit mode */
#define BOARD_SD_CLK_IO    43
#define BOARD_SD_CMD_IO    44
#define BOARD_SD_D0_IO     39
#define BOARD_SD_D1_IO     40
#define BOARD_SD_D2_IO     41
#define BOARD_SD_D3_IO     42

#endif /* BOARD_CONFIG_H */
