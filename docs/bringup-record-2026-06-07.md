# Bring-Up Record - 2026-06-07

## Summary

This record captures the first validated end-to-end run for the ESP32-P4-NANO
and OV5647 four-target displacement project.

Validated result:

- SD card mounted successfully.
- OV5647 was detected over SCCB/I2C.
- OV5647 streamed `800x640 RAW8 @ 50 fps` sensor format.
- CSV output opened at `/sdcard/displacement.csv`.
- Acquisition finished with `500` processed frames.
- Average frame interval was `29151 us`, about `34.3 fps`.

## Hardware

- Board: Waveshare ESP32-P4-NANO
- Chip: ESP32-P4 revision v1.3
- Camera: RPi Camera(B) OV5647 Rev 2.0
- SD card: detected as SDHC, about 7580 MB

## Build Environment

- ESP-IDF: v5.5.4
- Target: `esp32p4`
- Flash: 16 MB, DIO, 40 MHz
- PSRAM: 32 MB, 80 MHz

Build and flash commands:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'D:\.espressif\v5.5\v5.5.4\esp-idf\export.ps1'
idf.py build
idf.py -p COM8 flash
```

## SD Card Fix

The early SD card code used SDSPI, but the ESP32-P4-NANO demo and board wiring
use SDMMC. The driver was changed to:

- SDMMC 4-bit mode
- CLK GPIO43
- CMD GPIO44
- D0 GPIO39
- D1 GPIO40
- D2 GPIO41
- D3 GPIO42
- ESP32-P4 on-chip LDO4 for SDMMC IO power
- Internal pull-ups enabled by the slot config

FATFS long filename support was enabled through `CONFIG_FATFS_LFN_HEAP=y`
because `/sdcard/displacement.csv` is not an 8.3 short filename.

Validated SD log excerpt:

```text
I sdcard: SDMMC IO power uses on-chip LDO4
I sdcard: mount SDMMC 4-bit: CLK=43 CMD=44 D0=39 D1=40 D2=41 D3=42
Name: DVRBI
Type: SDHC
Speed: 20.00 MHz (limit: 20.00 MHz)
Size: 7580MB
SSR: bus_width=4
I sdcard: filesystem mounted at /sdcard
```

The warning below is currently harmless in the validated run. It appears during
IDF LDO power-control setup, and the card still mounts and writes successfully.

```text
W ldo: The voltage value 0 is out of the recommended range [500, 2700]
```

## Camera Result

Validated camera log excerpt:

```text
I ov5647: Detected Camera sensor PID=0x5647
I p4_camera: sensor fmt[0]: MIPI_2lane_24Minput_RAW8_800x640_50fps 800x640 fps=50
I p4_camera: sensor fmt[1]: MIPI_2lane_24Minput_RAW8_800x800_50fps 800x800 fps=50
I p4_camera: OV5647 streaming: MIPI_2lane_24Minput_RAW8_800x640_50fps 800x640 RAW8 @50 fps
I p4_camera: OV5647 MIPI-CSI ready: 800x640 RAW8 @50 fps
```

## Acquisition Result

Validated acquisition log excerpt:

```text
I acq_mgr: start: node=P4NODE01 test=TEST001 800x640@50 duration=10s
I cam_task: capture start: 800x640@50 fps total=500 batch=30
I csv: opened: /sdcard/displacement.csv
I write_task: written batches=10 frames=300
I cam_task: capture end
I write_task: done frames=500 batches=17 dt_min=29118 dt_avg=29151 dt_max=29226
I node_state: state=FINISHED
```

`dt_avg=29151 us` means the validated full pipeline ran at about:

```text
1000000 / 29151 = 34.3 fps
```

## Current Interpretation

The core goal, 30 fps four-target displacement CSV output, is currently reached
for the validated `800x640 RAW8` mode.

The next work should focus on:

- Calibrating the four ROI rectangles for the real target positions.
- Checking target validity, threshold, pixel count, and quality in the CSV.
- Running longer stability tests.
- Deciding whether output should stay on SD card or also stream live over UART,
  Ethernet, or HTTP.

## 5-Minute Frame-Rate Test Build

The next firmware build changes the default run from the earlier short
500-frame test to a wall-clock `300 s` stability test.

Added diagnostics:

- capture-side progress log every 30 seconds
- writer-side actual fps
- dropped-frame count from CSI sequence gaps
- `process_us` min/avg/max
- final elapsed time and fps summary

The batch buffer path was also changed from two alternating buffers to
free/full queue ownership with four static buffers. This is required for longer
SD-card runs because the capture task must not reuse a buffer while the write
task still owns it.
