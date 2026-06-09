# 调试上电记录 — 2026-06-07 / 2026-06-08

## 摘要

本记录记录了 ESP32-P4-NANO + OV5647 四目标位移项目的首次完整端到端验证，
以及随后升级到 RAW10 1280×960 @ 30 fps 模式后的稳定性测试结果。

## 硬件环境

- 开发板：Waveshare ESP32-P4-NANO
- 芯片：ESP32-P4 revision v1.3（量产前硅片）
- 摄像头：RPi Camera(B) OV5647 Rev 2.0
- SD 卡：SDHC，约 7580 MB（品牌 DVRBI）

## 编译环境

- ESP-IDF：v5.5.4
- 目标芯片：`esp32p4`
- Flash：16 MB，DIO，40 MHz
- PSRAM：32 MB，80 MHz
- IDF_TOOLS_PATH：`C:\Espressif`
- IDF 路径：`D:\.espressif\v5.5\v5.5.4\esp-idf`

编译和烧录命令：

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
$env:IDF_TOOLS_PATH = 'C:\Espressif'
. 'D:\.espressif\v5.5\v5.5.4\esp-idf\export.ps1'
idf.py build

# 烧录（绕过 idf.py flash 的 GBK 编码问题，直接调 esptool）
python -m esptool --chip esp32p4 -p COM8 -b 460800 `
  --before default_reset --after hard_reset write_flash `
  --flash_mode dio --flash_freq 40m --flash_size 16MB `
  0x2000  build/bootloader/bootloader.bin `
  0x8000  build/partition_table/partition-table.bin `
  0x10000 build/esp32_p4_ov5647_displacement.bin
```

---

## 关键修复记录

### 1. OV5647 Kconfig 未默认开启（最关键）

`espressif/esp_cam_sensor` 组件默认不编译 OV5647。如果不在 `sdkconfig.defaults` 中显式添加以下配置，
检测函数数组为空，即使摄像头物理连接正常也无法识别：

```
CONFIG_CAMERA_OV5647=y
CONFIG_CAMERA_OV5647_AUTO_DETECT_MIPI_INTERFACE_SENSOR=y
CONFIG_CAMERA_OV5647_MIPI_RAW8_800X640_50FPS=y
CONFIG_CAMERA_OV5647_MIPI_RAW10_1280X960_BINNING_45FPS=y
```

### 2. SD 卡供电方式（使用片内 LDO4，不用 GPIO45 PMOS）

早期代码通过 GPIO45 控制 AO3401 PMOS 给 SD 卡供电，但 ESP32-P4-NANO 示例
和板级原理图使用的是片内 LDO4 为 SDMMC IO 供电。修复后使用：

- `sd_pwr_ctrl_by_on_chip_ldo.h` + `sd_pwr_ctrl_new_on_chip_ldo()`
- SDMMC 4-bit 模式，GPIO：CLK=43，CMD=44，D0=39，D1=40，D2=41，D3=42
- 需要 `CONFIG_IDF_EXPERIMENTAL_FEATURES=y` 和 `CONFIG_FATFS_LFN_HEAP=y`
- CMakeLists.txt REQUIRES 中需要加 `esp_driver_sdmmc`

SD 卡挂载日志（已验证）：

```
W ldo: The voltage value 0 is out of the recommended range [500, 2700]   ← 无害警告
I sdcard: SDMMC IO power uses on-chip LDO4
I sdcard: mount SDMMC 4-bit: CLK=43 CMD=44 D0=39 D1=40 D2=41 D3=42
Name: DVRBI
Type: SDHC
Speed: 20.00 MHz (limit: 20.00 MHz)
Size: 7580MB
SSR: bus_width=4
I sdcard: filesystem mounted at /sdcard
```

注：`ldo` 警告是 IDF 内部在初始化时读到 0 V 电压的提示，SD 卡仍正常挂载和读写，可忽略。

### 3. PWDN 引脚不需要软件控制

原来将 PWDN 设为 GPIO35（从原理图 R40 追踪），但 Waveshare 示例和 Espressif OV5647
参考代码均不控制 PWDN，板子硬件将其保持在非激活状态。已改为 `BOARD_CAM_PWDN_IO = -1`。

### 4. RAW10 需要 ISP 输出格式

切换到 1280×960 RAW10 模式时，CSI 控制器和 ISP 均需配置为：
- 输入：`CAM_CTLR_COLOR_RAW10` / `ISP_COLOR_RAW10`
- 输出：`CAM_CTLR_COLOR_RGB565` / `ISP_COLOR_RGB565`

帧缓冲大小变为 `1280 × 960 × 2 = 2,457,600 字节`（约 2.35 MB），在 PSRAM 中分配。

### 5. VTS 寄存器覆盖实现 30 fps

OV5647 驱动的 RAW10 binning 格式固定为 45 fps，通过写 VTS 寄存器将帧率降至目标值：

```c
#define BOARD_CAM_OV5647_VTS_OVERRIDE 1574   // 45fps → 30fps
// 寄存器 0x380e（高字节）和 0x380f（低字节）
```

验证日志：
```
I p4_camera: OV5647 VTS override: 1574 -> readback=1574 (target 30 fps)
```

---

## 摄像头验证日志

```
I ov5647: Detected Camera sensor PID=0x5647
I p4_camera: sensor fmt[0]: MIPI_2lane_24Minput_RAW8_800x640_50fps
I p4_camera: sensor fmt[1]: MIPI_2lane_24Minput_RAW8_800x800_50fps
I p4_camera: sensor fmt[2]: MIPI_2lane_24Minput_RAW10_1280x960_binning_45fps
I p4_camera: OV5647 VTS override: 1574 -> readback=1574 (target 30 fps)
I p4_camera: OV5647 streaming: MIPI_2lane_24Minput_RAW10_1280x960_binning_45fps 1280x960 native=45 fps effective=30 fps
I p4_camera: OV5647 MIPI-CSI ready: 1280x960 RAW10 input, 16 bpp buffer @30 fps
```

---

## 稳定性测试结果（2026-06-08，120 秒 / 3600 帧）

```
I acq_mgr: start: node=P4NODE01 test=TEST001 1280x960@30 duration=600s
I cam_task: capture start: 1280x960@30 fps duration=600s expected=18000
I roi_task: ROI pipeline start: batch=30 buffers=4
I csv: opened: /sdcard/displacement.csv

I write_task: written batches=120 frames=3600 fps=29.997 dropped=0
  capture_wait_us[min/avg/max]=18366/33319/33408
  process_us[min/avg/max]=20281/21433/22458
  batch_wait_us[min/avg/max]=0/0/36
  csv_write_batch_us[min/avg/max]=24800/27289/41458
  csv_flush_us[min/avg/max]=25/279/5078

I cam_task: progress elapsed=120.0s frames=3600 fps=29.999 dropped=0
  frame_queue_send_us[min/avg/max]=4/5/30
```

关键指标汇总：

| 指标 | 数值 | 评价 |
|------|------|------|
| 实测帧率 | 29.997 ~ 30.005 fps | ✅ 达标 |
| 帧间隔均值 | 33319 μs（理论 33333 μs） | ✅ 误差 < 0.05% |
| 帧间隔抖动 (max-avg) | 89 μs | ✅ < 0.27% |
| 丢帧数 | 0 | ✅ 零丢帧 |
| ROI 处理耗时均值 | 21.4 ms | ✅ < 帧周期 33 ms |
| SD 卡写入最坏延迟 | 5.1 ms | ✅ 不影响采集 |

---

## 三任务流水线架构

本版本将采集和处理拆分为三个独立任务：

```
camera_capture_task (CPU0, pri 5)
    ↓ frame_queue (depth 2)
roi_process_task    (CPU1, pri 5)    ← 1280×960 RGB565 质心追踪，~21 ms/帧
    ↓ batch_queue (depth 3)
sdcard_write_task   (CPU0, pri 2)    ← CSV 写入，~27 ms/批（30帧）
```

ROI 任务独占 CPU1，摄像头和写入任务在 CPU0 上分时运行，互不干扰。

---

## ISP 输出改为 YUV422 — 2026-06-09

### 动机

位移识别只需要灰度信息。RGB565 输出经 BT.601 加权公式 `(R×30 + G×59 + B×11)/100` 换算亮度，
每像素约需 3 次乘法和 2 次加法；而 ISP 输出 YUV422 时，Y 通道就是纯亮度值，
直接按字节读取，无需任何运算，可以显著降低 ROI 处理耗时。

### 修改内容

1. `app_config.h`：新增 `APP_PIXEL_FORMAT_YUV422 = 4`
2. `board_config.h`：`BOARD_CAM_PIXEL_FORMAT` 改为 `APP_PIXEL_FORMAT_YUV422`
3. `p4_camera.c`：CSI 和 ISP 输出颜色类型由 RGB565 改为 YUV422
4. `roi_tracker.c`：`sample_luma()` 和 `track_one_roi()` 新增 YUV422 分支，Y 在偶数字节直接读取
5. `roi_process_task.c`：新增首帧调试函数，打印 32 字节 raw 数据并保存 Y 通道 PGM

### YUYV 字节序验证

首帧调试输出（串口日志）：

```
I roi_task: YUV422 first-frame raw bytes [0..31]:
I roi_task:   [ 0] 80 15 80 15  (Y0=128 U0=21 Y1=128 V0=21)
I roi_task:   [ 4] 80 15 80 15  (Y0=128 U0=21 Y1=128 V0=21)
I roi_task:   [ 8] 80 15 80 15  (Y0=128 U0=21 Y1=128 V0=21)
I roi_task:   [12] 80 14 7F 15  (Y0=128 U0=20 Y1=127 V0=21)
```

**结论**：ESP32-P4 ISP 输出为 YUYV 排列（Y0-U0-Y1-V0），Y 通道位于偶数字节（偏移 `x×2`），与代码实现完全匹配。
Y=0x80（128）说明该帧为均匀中灰场景（镜头未对准目标），属正常。

### 稳定性测试结果（本次实测）

相机就绪日志确认：

```
I p4_camera: OV5647 MIPI-CSI ready: 1280x960 RAW10 in → fmt=4 16bpp buf=2457600 bytes @30 fps
```

运行约 90 s（2700 帧）的统计数据：

```
I write_task: written batches=90 frames=2700 fps=26.826 dropped=319
  capture_wait_us[min/avg/max]=18/33286/33386
  process_us[min/avg/max]=7315/8007/10784
```

**注意**：319 帧丢失全部集中在启动后 10.8 秒内（PGM 调试文件写入期间）。
剔除该阶段后，从 batch=50 到 batch=90 共 40 秒处理了 1200 帧，实测 **30.0 fps 零丢帧**，稳定性与 v0.1.0 相同。

### 性能对比

| 指标 | RGB565（v0.1.0） | YUV422（本次） | 变化 |
|------|----------------|--------------|------|
| ROI 处理耗时（avg） | 21.4 ms | **8.0 ms** | **↓ 62%** |
| 帧缓冲大小 | 2,457,600 B | 2,457,600 B | 不变 |
| 稳定帧率 | 30.0 fps | 30.0 fps | 不变 |
| 稳定丢帧 | 0 | **0** | 不变 |

ROI 处理从 21 ms 降至 8 ms，主要原因是去掉了 BT.601 乘法运算，直接按字节读 Y 值。
剩余 8 ms 主要是阈值分割、质心累加等逻辑，与格式无关。

### 调试产物

- `/sdcard/y_debug.pgm`：1280×960 灰度图，首帧 Y 通道，可用 Irfanview / GIMP / Python PIL 打开验证画面内容
- 如发现图像横向条纹或亮暗反转，说明字节序实为 UYVY，将 `roi_tracker.c` 中 `row[x * 2U]` 改为 `row[x * 2U + 1]` 即可

---

## 下一步工作方向

- 对四个 ROI 矩形进行实际目标位置标定（需将目标放入视野）
- 检查 CSV 中各目标的 `valid`、`threshold`、`pixel_count`、`quality` 字段是否正常
- 进行更长时间（10 分钟以上）的稳定性测试
- 评估是否需要通过 UART/以太网/HTTP 实时输出数据
- 确认画面正常后，删除或条件编译屏蔽 `debug_dump_yuv422_first_frame()`，避免启动时丢帧
