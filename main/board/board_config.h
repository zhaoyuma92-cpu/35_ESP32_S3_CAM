#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define BOARD_NAME       "ESP32-P4-NANO"
#define FIRMWARE_VERSION "v0.2.4"

/* Temporary high-resolution frame-rate test.
 * The installed OV5647 driver does not expose 1080x960; the closest supported
 * high mode is 1280x960 RAW10 binning. The sensor mode is slowed from its
 * native 45 fps to 30 fps by overriding OV5647 VTS after set_format(). */
#define BOARD_DEFAULT_FRAME_WIDTH    1280
#define BOARD_DEFAULT_FRAME_HEIGHT    960
#define BOARD_DEFAULT_FRAME_STRIDE   1280
#define BOARD_DEFAULT_FRAME_RATE_HZ    40
#define BOARD_CAM_BITS_PER_PIXEL       10
#define BOARD_CAM_FRAME_BITS_PER_PIXEL 16

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

/* CSI controller lane bit rate for OV5647 RAW10 1280x960 binning native mode. */
#define BOARD_CAM_LANE_BIT_RATE_MBPS  442

/* OV5647 camera sensor format string */
#define BOARD_CAM_FORMAT_NAME  "MIPI_2lane_24Minput_RAW10_1280x960_binning_45fps"
#define BOARD_CAM_PIXEL_FORMAT APP_PIXEL_FORMAT_YUV422

/* 1280x960 RAW10 native timing: PCLK=88.333333 MHz, HTS=1796, VTS=1093.
 * The nominal 45 fps mode measured about 43.2 fps on this board, so tune VTS
 * to target a slower rate. VTS = PCLK / (HTS * target_fps).
 * 1574 -> ~30.0 fps (measured); 1229 -> measured 38.418 fps actual (dt_us~26029.5,
 * 23052 frames over 600.006s, 0 dropped, valid_mask=15 throughout) — about 3.95%
 * slower than the 40fps target, consistent with the same theoretical-vs-actual
 * gap seen on the native 45fps mode. 1180 -> ~40.15 fps theoretical, next try.
 */
#define BOARD_CAM_OV5647_VTS_OVERRIDE 1180

/* SD card, SDMMC 4-bit mode */
#define BOARD_SD_CLK_IO    43
#define BOARD_SD_CMD_IO    44
#define BOARD_SD_D0_IO     39
#define BOARD_SD_D1_IO     40
#define BOARD_SD_D2_IO     41
#define BOARD_SD_D3_IO     42

#endif /* BOARD_CONFIG_H */
