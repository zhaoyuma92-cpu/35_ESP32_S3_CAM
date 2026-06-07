# Codex Project Goal - ESP32-P4 OV5647 Displacement

## Core Goal

Build a stable ESP32-P4-NANO + OV5647 camera displacement acquisition node.

The target is:

- Acquire camera data at about `1024x960` level resolution.
- Maintain stable `30 fps` data acquisition and processing.
- Track four target ROI regions in each frame.
- Calculate each target center point.
- Output four-target displacement data relative to the reference center.
- Prefer direct numeric displacement output over image output.

The primary output is displacement data, not full photos and not JPEG.

## Preferred Output

Main output:

- frame index
- timestamp
- frame interval
- processing time
- valid mask
- four target center points
- four target dx/dy displacement values
- threshold, pixel count, quality for diagnostics

Current output file:

```text
/sdcard/displacement.csv
```

If performance margin is enough, add optional ROI raw output:

- one ROI image per target per frame, or
- four ROI raw blocks per frame

ROI image output should use raw grayscale/RAW8 binary data, not JPEG.

## Current Validated Version

The current project has already validated an end-to-end pipeline:

- Board: Waveshare ESP32-P4-NANO
- Camera: RPi Camera(B), OV5647 Rev 2.0
- Current validated camera mode: `800x640 RAW8 @ 50 fps`
- SD card: SDMMC 4-bit, mounted at `/sdcard`
- Output: `/sdcard/displacement.csv`
- Four ROI displacement pipeline is running
- Validated run: `500` frames
- Measured average frame interval: `29151 us`
- Approximate measured output rate: `34.3 fps`

This means the current version reaches the 30 fps target in `800x640` mode.

## Important Target Resolution Note

The user target is `1024x960` level resolution.

Current validated mode is only:

```text
800x640 RAW8
```

The installed OV5647 driver currently exposes:

```text
800x640 RAW8 @ 50 fps
800x800 RAW8 @ 50 fps
```

There may also be a candidate mode:

```text
1280x960 RAW10 binning @ 45 fps
```

But that is not currently validated in this project. It is RAW10, not RAW8.

Do not assume `1024x960 @ 30 fps` is solved until a camera-mode test proves it.

## Performance Direction

The project should optimize for stable displacement output first.

Priority order:

1. Stable camera acquisition.
2. Stable 30 fps four-target displacement output.
3. Dropped-frame detection and real output fps measurement.
4. ROI calibration and threshold tuning.
5. Optional ROI raw image output.
6. Live streaming over UART/Ethernet/HTTP if needed.

Do not prioritize JPEG, full-frame image saving, UI, or complex networking before
the hot path is stable.

## Known Current Limits

- Current ROI rectangles are hard-coded bring-up defaults.
- Current output is SD card CSV only.
- Current default run is frame-count based: `frame_rate_hz * duration_s`.
- Sensor FPS and output FPS are currently coupled in config.
- Dropped-frame statistics are not yet explicit.
- Batch buffer ownership should be hardened for long-running tests.

## Recommended Next Steps

### Step 1 - Camera Mode Test

Create a focused camera-mode test for higher resolution.

Goals:

- Test whether OV5647 can run a practical `1024x960` or `1280x960` mode.
- Measure real camera frame arrival interval.
- Do not write ROI images during this test.
- Keep displacement output minimal.

### Step 2 - Add Runtime Statistics

Add explicit runtime stats:

- processed frame count
- sensor sequence count
- skipped/dropped frame count
- min/avg/max `dt_us`
- min/avg/max `process_us`
- actual output fps

### Step 3 - Separate Sensor FPS And Output FPS

Keep camera sensor mode independent from target output rate.

Example:

```text
sensor_fps = 45 or 50
target_output_fps = 30
```

If processing falls behind, keep the latest frame and drop older frames.

### Step 4 - Harden Batch Buffers

Current code uses two static batch buffers. For long runs, make buffer ownership
explicit so the writer task cannot read a buffer after the capture task reuses
it.

Safer options:

- match batch buffer count to queue depth
- use a free-buffer queue
- use a small ring with ownership states

### Step 5 - Optional ROI Raw Dump

Only after stable 30 fps displacement output:

- add a debug-only ROI raw dump mode
- store binary grayscale/RAW8 ROI data
- avoid JPEG
- avoid full-frame writes

## What Not To Do First

Do not start with:

- full-frame image saving
- JPEG encoding
- complex web UI
- heavy image processing over the full frame
- large architecture rewrites

The useful path is conservative:

```text
camera -> ROI center -> displacement -> compact output
```

## Success Criteria

A good next milestone is:

- Higher-resolution mode validated near `1024x960`.
- Stable `>=30 fps` displacement output for four targets.
- CSV or compact numeric output has no missing timing information.
- Dropped frames are counted.
- ROI validity and quality are visible.
- The system can run longer than a short 500-frame test without buffer or SD
  write instability.

