# Agent Instructions

This repository is the active project for the ESP32-P4 OV5647 four-target
displacement node.

All agents, including Codex, should read this file before making changes.

## Project Goal

The user's goal is to build a stable camera displacement acquisition node:

- ESP32-P4-NANO board
- OV5647 MIPI-CSI camera
- target resolution around `1024x960`
- stable `30 fps` data acquisition and processing
- four target ROI tracking
- center point calculation for each target
- displacement output for each target
- numeric displacement output first
- optional ROI raw grayscale output only if performance margin allows

The hot path should output displacement data, not JPEG and not full-frame
photos.

Read the detailed goal file:

```text
docs/codex-project-goal.md
```

Read the latest validated bring-up record:

```text
docs/bringup-record-2026-06-07.md
```

## Current Validated Status

The current validated mode is:

- `800x640 RAW8 @ 50 fps` OV5647 sensor mode
- SD card mounted through SDMMC 4-bit
- CSV output at `/sdcard/displacement.csv`
- four ROI displacement pipeline running
- 500-frame run completed
- measured average frame interval `29151 us`
- measured full-pipeline rate about `34.3 fps`

This proves 30 fps displacement output in `800x640` mode, but it does not yet
prove the final `1024x960` target.

## Important Constraints

- Do not modify sibling MCU folders. Work only inside this repository unless the
  user explicitly says otherwise.
- Preserve the current ESP32-P4 bring-up path.
- Do not replace the displacement-first design with image-first or JPEG-first
  logic.
- Do not add heavy UI/network features before the acquisition hot path is
  stable.
- Keep changes small and measurable.
- Prefer direct numeric outputs: center, displacement, timing, validity, quality.

## Recommended Next Work

Priority order:

1. Validate higher camera resolution modes near `1024x960`.
2. Add skipped-frame and actual-output-fps statistics.
3. Separate sensor FPS from target output FPS.
4. Harden batch buffer ownership for long runs.
5. Calibrate real ROI rectangles and thresholds.
6. Add optional ROI raw dump mode only after stable 30 fps displacement output.

## Build Notes

Use ESP-IDF v5.5.4 for the current ESP32-P4 rev v1.3 board.

Typical local commands:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'D:\.espressif\v5.5\v5.5.4\esp-idf\export.ps1'
idf.py build
idf.py -p COM8 flash
```

## Key Files

- `main/drivers/p4_camera.c`
- `main/drivers/sdcard.c`
- `main/vision/roi_tracker.c`
- `main/acquisition/camera_capture_task.c`
- `main/acquisition/sdcard_write_task.c`
- `main/storage/csv_writer.c`
- `main/config/app_config.c`
- `main/board/board_config.h`

