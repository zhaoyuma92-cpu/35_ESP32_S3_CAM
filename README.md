# ESP32-P4 OV5647 Four-Target Displacement Node

Built inside `35_ESP32_S3_CAM`.

## Goal

Use a Waveshare ESP32-P4-NANO with an OV5647 MIPI-CSI camera to acquire frames,
track four ROI targets, and write four-target displacement data to an SD card as
CSV at ≥ 30 fps.  No full-frame image is saved in the hot path.

## Current Status (validated 2026-06-07)

| Item | Value |
|------|-------|
| Board | Waveshare ESP32-P4-NANO, ESP32-P4 rev v1.3 |
| Camera | RPi Camera(B), OV5647 Rev 2.0 |
| Sensor mode | `1280×960 RAW10 binning` format |
| Effective frame rate | **30 fps** (VTS overridden from 45 → 30 fps) |
| Frame format | ISP RAW10 → RGB565 conversion, PSRAM double buffer |
| SD card | SDMMC 4-bit, 20 MHz, ESP32-P4 on-chip LDO4 IO power |
| Output | `/sdcard/displacement.csv` |
| Run duration | 600 s (configurable in `app_config.c`) |

See `docs/bringup-record-2026-06-07.md` for the validated run log.

## Data Path

```
OV5647 RAW10 sensor
  → MIPI-CSI 2-lane, 442 Mbps/lane
  → ESP32-P4 CSI controller (RAW10 in, RGB565 out via ISP)
  → PSRAM double frame buffer

camera_capture_task (CPU0, pri 5)   — reads CSI frames, sends to ROI task
  → frame_queue (depth 2)
roi_process_task    (CPU1, pri 5)   — center-of-mass tracking, batch assembly
  → batch_queue (depth 3)
sdcard_write_task   (CPU0, pri 2)   — CSV write to SD card
```

## CSV Output Format

One row per processed frame:

```
frame_index, t_us, dt_us, capture_wait_us, process_us, batch_wait_us,
dropped_frames, valid_mask,
t1_valid, t1_cx_px, t1_cy_px, t1_dx_px, t1_dy_px, t1_threshold, t1_pixels, t1_quality,
… (× 4 targets)
```

Pixel center and displacement are Q8 fixed-point printed as decimal.

## Important Files

| File | Purpose |
|------|---------|
| `main/board/board_config.h` | GPIO, camera mode, LDO channel constants |
| `main/drivers/p4_camera.c` | OV5647 MIPI-CSI init and frame acquisition |
| `main/drivers/sdcard.c` | SDMMC 4-bit mount via on-chip LDO4 |
| `main/vision/roi_tracker.c` | Single-ROI threshold + center-of-mass |
| `main/acquisition/camera_capture_task.c` | Camera → frame_queue |
| `main/acquisition/roi_process_task.c` | frame_queue → batch_queue |
| `main/acquisition/sdcard_write_task.c` | batch_queue → CSV |
| `main/acquisition/timing.h` | Shared min/avg/max timing accumulator |
| `main/storage/csv_writer.c` | FATFS CSV write helper |
| `main/config/app_config.c` | Default ROI and run configuration |

## Build and Flash

Requires ESP-IDF v5.5.4.

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
$env:IDF_TOOLS_PATH = 'C:\Espressif'
. 'D:\.espressif\v5.5\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Flash (COM8 is the ESP32-P4-NANO programming port on this machine):

```powershell
python -m esptool --chip esp32p4 -p COM8 -b 460800 `
  --before default_reset --after hard_reset write_flash `
  --flash_mode dio --flash_freq 40m --flash_size 16MB `
  0x2000  build/bootloader/bootloader.bin `
  0x8000  build/partition_table/partition-table.bin `
  0x10000 build/esp32_p4_ov5647_displacement.bin
```

Monitor:

```powershell
idf.py -p COM8 monitor
```

## Known Limits

- ROIs default to static bring-up coordinates; calibrate for real target positions.
- No live host output (UART/Ethernet/HTTP) — SD card only.
- PWDN/RESET/XCLK are not driven from software; the board holds these inactive
  in hardware.
