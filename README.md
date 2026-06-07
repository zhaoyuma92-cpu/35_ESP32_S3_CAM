# ESP32-P4 OV5647 Four-Target Displacement Node

This project is built inside `35_ESP32_S3_CAM` only.

## Goal

Use an ESP32-P4-NANO board with an OV5647 MIPI-CSI camera to acquire grayscale
camera frames, track four target ROIs, calculate each target center point, and
output four-target displacement data at 30 fps or higher.

The main output is numeric displacement data. The firmware does not save JPEG
or full-frame images in the hot path.

## Current Status

Current validated mode:

- Board: Waveshare ESP32-P4-NANO, ESP32-P4 rev v1.3
- Camera: RPi Camera(B), OV5647 Rev 2.0
- Camera mode: `800x640 RAW8 @ 50 fps` sensor format
- Runtime output: `/sdcard/displacement.csv`
- SD card: SDMMC 4-bit, 20 MHz, on-chip LDO4 IO power
- Acquisition: 500 frames, four ROIs, CSV output
- Measured frame interval: average `29151 us`, about `34.3 fps`

The 30 fps four-target displacement target is currently reached in this
validated 800x640 mode.

## Data Path

```text
OV5647 RAW8 frame
  -> ESP32-P4 MIPI-CSI
  -> ISP RAW8 pass-through
  -> PSRAM double frame buffer
  -> four ROI threshold + center-of-mass tracking
  -> displacement sample batch queue
  -> SD card CSV writer
```

## Output Format

One CSV row is one processed frame:

```text
frame_index,t_us,dt_us,process_us,valid_mask,
t1_valid,t1_cx_px,t1_cy_px,t1_dx_px,t1_dy_px,t1_threshold,t1_pixels,t1_quality,
...
t4_valid,t4_cx_px,t4_cy_px,t4_dx_px,t4_dy_px,t4_threshold,t4_pixels,t4_quality
```

Pixel center and displacement values are stored internally as Q8 fixed-point
values and printed as decimal pixel values.

## Important Files

- `main/drivers/p4_camera.c`: OV5647 MIPI-CSI camera backend
- `main/drivers/sdcard.c`: SDMMC 4-bit SD card mount
- `main/vision/roi_tracker.c`: four-ROI center tracking
- `main/acquisition/camera_capture_task.c`: frame acquisition and ROI processing
- `main/acquisition/sdcard_write_task.c`: batch write task
- `main/storage/csv_writer.c`: CSV writer
- `main/config/app_config.c`: default ROI and run configuration
- `main/board/board_config.h`: board pin and camera mode configuration

## Build And Flash

Use ESP-IDF v5.5.4 for ESP32-P4 rev v1.3:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'D:\.espressif\v5.5\v5.5.4\esp-idf\export.ps1'
idf.py build
idf.py -p COM8 flash
```

Monitor:

```powershell
idf.py -p COM8 monitor
```

## Current Limits

- The validated camera mode is `800x640 RAW8`; `1280x1040` is not currently a
  supported OV5647 RAW8 mode in the installed driver.
- ROIs are currently fixed defaults for bring-up. They must be calibrated for
  the real target positions.
- The default run is frame-count based: `50 fps * 10 s = 500 frames`. Because
  the measured full pipeline rate is about 34 fps, that run takes longer than
  10 wall-clock seconds.
- Real-time host output over UART, Ethernet, or HTTP is not implemented yet.

See `docs/bringup-record-2026-06-07.md` for the validated run record.
