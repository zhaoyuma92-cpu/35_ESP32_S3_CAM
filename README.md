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

## SDMMC 槽位分工

ESP32-P4 有两个独立的 SDMMC 控制器，本项目将它们分配给不同设备：

| 设备 | SDMMC Slot | GPIO | 说明 |
|------|-----------|------|------|
| ESP32-C6 WiFi（esp_hosted） | **SLOT_1**（GPIO matrix） | CLK=18, CMD=19, D0-D3=14-17, RST=54 | esp_hosted 默认值，不需要修改 |
| SD 卡 | **SLOT_0**（GPIO matrix） | CLK=43, CMD=44, D0-D3=39-42 | `sdcard.c` 中显式设置 SLOT_0 |

两个 slot 相互独立，可以同时初始化而不冲突。

## WiFi 可选功能

WiFi 由板载 ESP32-C6 协处理器通过 SDIO 提供，默认**关闭**，不影响 30fps 采集性能。

### 开启 WiFi

在 `sdkconfig.defaults` 中修改一行：

```
CONFIG_DISP_ENABLE_WIFI=y
```

或通过 `idf.py menuconfig → "WiFi (optional)"` 配置。

### 启动顺序

WiFi 连接在采集完成后才启动，避免 esp_hosted 后台任务（优先级 23）在采集期间抢占 WriteTask（优先级 2）：

```
boot → SD 卡挂载(SLOT_0) → 相机初始化 → 采集(30fps CSV) → [采集完成] → WiFi连接(SLOT_1)
```

## 重要文件说明

| 文件 | 作用 |
|------|------|
| `main/board/board_config.h` | GPIO、摄像头模式、LDO 通道等硬件常量 |
| `main/drivers/p4_camera.c` | OV5647 MIPI-CSI 初始化和帧采集 |
| `main/drivers/sdcard.c` | SDMMC 4-bit 挂载（SLOT_0，片内 LDO4 供电） |
| `main/network/wifi_manager.c` | WiFi STA 连接封装（`CONFIG_DISP_ENABLE_WIFI` 保护） |
| `main/vision/roi_tracker.c` | 单 ROI 阈值分割 + 质心计算 |
| `main/acquisition/camera_capture_task.c` | 摄像头帧 → frame_queue |
| `main/acquisition/roi_process_task.c` | frame_queue → batch_queue |
| `main/acquisition/sdcard_write_task.c` | batch_queue → CSV 文件 |
| `main/acquisition/timing.h` | 共用 min/avg/max 计时统计工具 |
| `main/storage/csv_writer.c` | FATFS CSV 写入封装 |
| `main/config/app_config.c` | 默认 ROI 配置和运行参数 |
| `experiments/wifi_only/` | 独立 WiFi 最小验证工程（不含摄像头/SD 卡） |

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
- WiFi 支持已集成（`CONFIG_DISP_ENABLE_WIFI=y`），但 HTTP 服务器和数据上传尚未实现。
- PWDN/RESET/XCLK 不由软件控制，硬件已将其保持在非激活状态。
- SD 卡与 C6 SDIO 同时运行时的长时间稳定性尚未全面测试（槽位分离后理论上无冲突）。
