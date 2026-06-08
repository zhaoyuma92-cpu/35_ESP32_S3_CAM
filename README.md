# ESP32-P4 OV5647 四目标位移检测节点

项目目录：`35_ESP32_S3_CAM`

## 目标

使用 Waveshare ESP32-P4-NANO 开发板和 OV5647 MIPI-CSI 摄像头，以 ≥ 30 fps 的帧率采集图像，对四个 ROI 目标进行质心追踪，将位移数据写入 SD 卡 CSV 文件。热路径不保存完整图像帧。

## 当前状态（已验证 2026-06-08）

| 项目 | 数值 |
|------|------|
| 开发板 | Waveshare ESP32-P4-NANO，ESP32-P4 rev v1.3 |
| 摄像头 | RPi Camera(B)，OV5647 Rev 2.0 |
| 传感器模式 | `1280×960 RAW10 binning 45fps` 格式 |
| 实际帧率 | **30 fps**（通过 VTS 寄存器覆盖，将 45fps 降至 30fps） |
| 帧缓冲 | ISP RAW10 → RGB565 转换，PSRAM 双缓冲 |
| SD 卡 | SDMMC 4-bit，20 MHz，ESP32-P4 片内 LDO4 供电 |
| 输出文件 | `/sdcard/displacement.csv` |
| 运行时长 | 600 秒（可在 `app_config.c` 中配置） |

详细验证日志见 `docs/bringup-record-2026-06-07.md`。

## 稳定性测试结果（120 秒 / 3600 帧）

| 指标 | 数值 |
|------|------|
| 实测帧率 | 29.997 ~ 30.005 fps |
| 帧间隔均值 | 33319 μs（理论 33333 μs，误差 < 0.05%） |
| 帧间隔抖动 (max-avg) | 89 μs（< 0.27%） |
| 丢帧数 | 0 |
| ROI 处理耗时均值 | 21.4 ms |
| CSV 每批次写入均值 | 27.3 ms |

## 数据流路径

```
OV5647 RAW10 传感器
  → MIPI-CSI 2-lane，442 Mbps/lane
  → ESP32-P4 CSI 控制器（RAW10 输入，ISP 输出 RGB565）
  → PSRAM 双帧缓冲

camera_capture_task（CPU0，优先级 5）  — 读 CSI 帧，发送到 ROI 任务
  → frame_queue（深度 2）
roi_process_task   （CPU1，优先级 5）  — 质心追踪，组装批次
  → batch_queue（深度 3）
sdcard_write_task  （CPU0，优先级 2）  — 批次写入 CSV
```

## CSV 输出格式

每行对应一帧处理结果：

```
frame_index, t_us, dt_us, capture_wait_us, process_us, batch_wait_us,
dropped_frames, valid_mask,
t1_valid, t1_cx_px, t1_cy_px, t1_dx_px, t1_dy_px, t1_threshold, t1_pixels, t1_quality,
…（× 4 个目标）
```

质心坐标和位移以 Q8 定点数存储，输出为十进制像素值。

## 重要文件说明

| 文件 | 作用 |
|------|------|
| `main/board/board_config.h` | GPIO、摄像头模式、LDO 通道等硬件常量 |
| `main/drivers/p4_camera.c` | OV5647 MIPI-CSI 初始化和帧采集 |
| `main/drivers/sdcard.c` | SDMMC 4-bit 挂载（片内 LDO4 供电） |
| `main/vision/roi_tracker.c` | 单 ROI 阈值分割 + 质心计算 |
| `main/acquisition/camera_capture_task.c` | 摄像头帧 → frame_queue |
| `main/acquisition/roi_process_task.c` | frame_queue → batch_queue |
| `main/acquisition/sdcard_write_task.c` | batch_queue → CSV 文件 |
| `main/acquisition/timing.h` | 共用 min/avg/max 计时统计工具 |
| `main/storage/csv_writer.c` | FATFS CSV 写入封装 |
| `main/config/app_config.c` | 默认 ROI 配置和运行参数 |

## 编译和烧录

需要 ESP-IDF v5.5.4。

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
$env:IDF_TOOLS_PATH = 'C:\Espressif'
. 'D:\.espressif\v5.5\v5.5.4\esp-idf\export.ps1'
idf.py build
```

烧录（COM8 为 ESP32-P4-NANO 编程口）：

```powershell
python -m esptool --chip esp32p4 -p COM8 -b 460800 `
  --before default_reset --after hard_reset write_flash `
  --flash_mode dio --flash_freq 40m --flash_size 16MB `
  0x2000  build/bootloader/bootloader.bin `
  0x8000  build/partition_table/partition-table.bin `
  0x10000 build/esp32_p4_ov5647_displacement.bin
```

串口监视：

```powershell
idf.py -p COM8 monitor
```

## 已知限制

- ROI 坐标为调试默认值，需针对实际目标位置重新标定。
- 无实时主机输出（UART/以太网/HTTP），当前仅支持 SD 卡写入。
- PWDN/RESET/XCLK 不由软件控制，硬件已将其保持在非激活状态。
