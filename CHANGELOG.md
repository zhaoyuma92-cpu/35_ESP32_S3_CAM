# 变更记录

本文件按 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 格式维护，版本号遵循语义化版本 (SemVer)。

---

## [未发布]

### 新增
- `APP_PIXEL_FORMAT_YUV422 = 4`：在 `app_config.h` 中新增 YUV422 像素格式枚举值（YUYV 排列，16 bpp）
- `debug_dump_yuv422_first_frame()`：roi_process_task.c 中首帧调试函数，启动后自动将 Y 通道以 PGM 格式保存到 `/sdcard/y_debug.pgm`，并将前 32 字节 raw 数据打印到串口，用于验证 YUYV/UYVY 字节序

### 变更
- **ISP 输出格式**：从 `RGB565` 切换为 `YUV422 (YUYV)`
  - CSI 控制器输出：`CAM_CTLR_COLOR_RGB565` → `CAM_CTLR_COLOR_YUV422`
  - ISP 处理器输出：`ISP_COLOR_RGB565` → `ISP_COLOR_YUV422`
  - `board_config.h`：`BOARD_CAM_PIXEL_FORMAT` 改为 `APP_PIXEL_FORMAT_YUV422`
- **ROI 灰度提取**：`roi_tracker.c` 中 `sample_luma()` 和 `track_one_roi()` 新增 YUV422 分支，直接从 YUYV 流读取 Y 字节（偏移 `x*2`），不再做 BT.601 加权运算
- `p4_camera.c` 就绪日志增加像素格式 id 和帧缓冲总字节数输出

### 性能对比（1280×960 @30 fps）

| 指标 | RGB565 版本 | YUV422 版本 | 变化 |
|------|------------|------------|------|
| ROI 处理耗时（avg） | 21.4 ms | 8.0 ms | **↓ 62%** |
| 帧缓冲大小 | 2,457,600 B | 2,457,600 B | 不变 |
| 稳定帧率 | 30.0 fps | 30.0 fps | 不变 |
| 稳定丢帧 | 0 | 0 | 不变 |

---

## [v0.1.0] — 2026-06-08

首个端到端完整验证版本，ESP32-P4-NANO + OV5647 RAW10 @30 fps，SD 卡 CSV 输出稳定。

### 新增
- **OV5647 RAW10 1280×960 binning 模式**：通过 VTS 寄存器覆盖将传感器从 45 fps 降至 30 fps
- **三任务流水线**：
  - `camera_capture_task`（CPU0，优先级 5）— 从 CSI 取帧，送 frame_queue
  - `roi_process_task`（CPU1，优先级 5）— 阈值分割 + 质心计算，组装批次
  - `sdcard_write_task`（CPU0，优先级 2）— 批次写入 `/sdcard/displacement.csv`
- `timing.h`：共用 min/avg/max 计时统计工具，供 capture 和 write 任务共享
- PSRAM 双帧缓冲（每帧 2,457,600 字节，64 字节 DMA 对齐）
- `roi_tracker.c`：单 ROI 阈值分割 + 质心计算，支持 GRAY8 / RAW8 / RGB565

### 关键修复
- **SD 卡 ESP_ERR_TIMEOUT**：根本原因为供电方式错误。原代码通过 GPIO45 控制 AO3401 PMOS 给 SD 卡供电，ESP32-P4-NANO 实际使用片内 LDO4。改用 `sd_pwr_ctrl_by_on_chip_ldo.h` API 后 SD 卡正常挂载
- **I2C 扫描无条件运行**：用 `#if CONFIG_LOG_DEFAULT_LEVEL_DEBUG` 守卫，仅调试日志级别下执行
- **PWDN 引脚误配**：改为 `BOARD_CAM_PWDN_IO = -1`，与 Waveshare 示例和 IDF OV5647 参考保持一致
- **VERBOSE 日志编译缺失**：在 `sdkconfig.defaults` 加 `CONFIG_LOG_MAXIMUM_LEVEL_VERBOSE=y`
- **idf.py flash GBK 编码错误**：Windows 下改用 `python -m esptool` 直接烧录绕过问题
- **timing_stat_t 重复定义**：从两个任务文件中提取到共用 `acquisition/timing.h`

### 稳定性测试结果（120 秒 / 3600 帧）

```
帧率：29.997 ~ 30.005 fps
帧间隔均值：33319 μs（理论 33333 μs，误差 < 0.05%）
帧间隔抖动（max−avg）：89 μs（< 0.27%）
丢帧：0
ROI 处理耗时均值：21.4 ms
CSV 每批次写入均值：27.3 ms
```

[v0.1.0]: https://github.com/zhaoyuma92-cpu/35_ESP32_S3_CAM/releases/tag/v0.1.0
