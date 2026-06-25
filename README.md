# ESP32-P4 OV5647 四目标位移检测节点

项目目录：`35_ESP32_S3_CAM`

## 目标

使用 Waveshare ESP32-P4-NANO 开发板和 OV5647 MIPI-CSI 摄像头，以 ≥ 30 fps 的帧率采集图像，对四个 ROI 目标进行质心追踪，将位移数据写入 SD 卡 CSV 文件，并通过 HTTP API 和 UDP 广播向上位机暴露控制接口，与项目 33/34 保持统一协议。

## 当前状态（v0.2.4，已验证 2026-06-25）

| 项目 | 数值 |
|------|------|
| 固件版本 | v0.2.4 |
| 开发板 | Waveshare ESP32-P4-NANO，ESP32-P4 rev v1.3 |
| 摄像头 | RPi Camera(B)，OV5647 Rev 2.0 |
| 传感器模式 | `1280×960 RAW10 binning @40fps`（VTS 覆盖，实测约38.4fps） |
| 像素格式 | YUV422（YUYV），Y 通道用于 ROI 灰度追踪 |
| 帧缓冲 | PSRAM 双缓冲，每帧 2,457,600 字节 |
| SD 卡 | SDMMC 4-bit，20 MHz，ESP32-P4 片内 LDO4 供电 |
| 输出文件 | `/sdcard/{test_id}_{node_id}_{时间戳}.csv`（每次采集独立文件，与项目33命名一致） |
| WiFi | ESP32-C6 经 SDIO 提供，采集期间主动关闭，采集完成后重连 |
| HTTP API | `http://<IP>/`，11 条路由 |
| UDP 广播 | port 33330，兼容项目 33/34 协议 |
| 串口命令 | `s` 启动 / `o` offline 启动 / `q` 停止 / `?` 状态 |

详细验证日志见 `docs/bringup-record-2026-06-07.md` 和 `docs/bringup-record-2026-06-13.md`。

## 节点运行模式

### 在线模式（`/api/start` 或 串口 `s`）
WiFi 全程保持连接，采集期间 HTTP API 继续响应（可随时 `/api/stop`）。

### 离线采集模式（`/api/start-offline` 或 串口 `o`）
HTTP 响应 `{"ok":true}` → 停止 HTTP → 停止 WiFi → 延迟 4.5s 等射频完全静默 → 执行采集 → 采集完成后重新连接 WiFi → 重启 HTTP，数据可通过 `/api/files` + `/download` 取回。

## 启动序列

```
上电
 ├─ SD 卡挂载（SLOT_0，GPIO 39-44）
 ├─ OV5647 初始化（1280×960 RAW10 @30fps）
 ├─ acq_manager 初始化（三任务流水线）
 ├─ WiFi 连接（C6 SDIO，SLOT_1，GPIO 14-19）
 ├─ HTTP 服务器启动
 ├─ UDP 广播任务启动（port 33330）
 └─ NODE_IDLE — 等待信号量
      ↓  收到触发（HTTP /api/start 或 串口 s/o）
      NODE_RECORDING — 采集 + 写 CSV
      ↓  acq_manager 完成或 stop 请求
      NODE_FLUSHING → NODE_STOPPED
      ↓  [offline 路径] 重连 WiFi + HTTP
      NODE_IDLE（循环）
```

## HTTP API

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/` | 服务存活检查，返回 `{"ok":true}` |
| GET | `/api/status` | 节点状态、WiFi IP、当前配置 |
| GET | `/api/config` | 读取运行参数（node_id / test_id / duration / roi） |
| POST | `/api/config` | 更新运行参数（JSON body） |
| GET | `/api/start` | 触发在线采集 |
| GET | `/api/start-offline` | 触发离线采集（先停 WiFi 再采集） |
| GET | `/api/stop` | 停止当前采集 |
| GET | `/api/files` | 列出 SD 卡可下载文件（.csv/.log/.json/.pgm） |
| GET | `/download?file=xxx` | 下载指定文件 |
| POST | `/api/files/clear` | 删除 SD 卡全部可下载文件 |
| POST | `/api/time` | 设置系统时间（`{"epoch":1234567890}`） |

### 示例

```bash
# 查看状态
curl http://192.168.31.75/api/status

# 触发离线采集（采集期间无 WiFi 干扰）
curl http://192.168.31.75/api/start-offline

# 采集完成后（~10s）查看文件
curl http://192.168.31.75/api/files

# 下载 CSV
curl http://192.168.31.75/download?file=displacement.csv -o data.csv
```

## UDP 广播协议（兼容项目 33/34）

- **端口**：33330
- **发现魔数**：`ADXL355_DISCOVER_V1`（UDP 单播/广播请求）
- **响应格式**：

```json
{
  "type": "displacement_node",
  "node_id": "CAM_NODE_01",
  "ip": "192.168.31.75",
  "firmware": "v0.2.4",
  "state": "NODE_IDLE"
}
```

## 串口命令接口

设备通过 UART0（115200 baud）接受单字节命令，无需 WiFi 即可测试：

| 命令 | 动作 |
|------|------|
| `s` | 触发在线采集（等同 `/api/start`） |
| `o` | 触发离线采集（等同 `/api/start-offline`） |
| `q` | 停止采集（等同 `/api/stop`） |
| `?` | 打印当前状态、WiFi 连接情况、IP |

## 数据流路径

```
OV5647 RAW10 传感器
  → MIPI-CSI 2-lane，442 Mbps/lane
  → ESP32-P4 CSI 控制器（RAW10 输入，ISP 输出 YUV422）
  → PSRAM 双帧缓冲

camera_capture_task（CPU0，优先级 5）  — 读 CSI 帧，发送到 ROI 任务
  → frame_queue（深度 2）
roi_process_task   （CPU1，优先级 5）  — Y 通道质心追踪，组装批次
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

| 设备 | Slot | GPIO | 说明 |
|------|------|------|------|
| ESP32-C6 WiFi（esp_hosted SDIO） | **SLOT_1**（GPIO matrix） | CLK=18, CMD=19, D0-D3=14-17, RST=54 | 采集期间静默 |
| SD 卡 | **SLOT_0**（GPIO matrix） | CLK=43, CMD=44, D0-D3=39-42 | 始终可写 |

## 重要文件说明

| 文件 | 作用 |
|------|------|
| `main/main.c` | 节点主循环：boot → WiFi → HTTP → UDP → IDLE → 信号量驱动采集 |
| `main/board/board_config.h` | GPIO、摄像头模式、LDO 通道、固件版本等硬件常量 |
| `main/network/http_server.c/h` | httpd 启动/停止封装 |
| `main/network/http_api.c/h` | 11 条 REST 路由实现 |
| `main/network/udp_discovery.c/h` | UDP 广播响应器（port 33330） |
| `main/network/wifi_manager.c/h` | WiFi STA 连接封装，支持幂等 start/stop |
| `main/storage/file_manager.c/h` | SD 卡文件列表/下载/路径安全检查 |
| `main/system/app_context.c/h` | 全局上下文（配置 + 信号量 + 状态） |
| `main/system/node_state.c/h` | 节点状态机枚举和字符串转换 |
| `main/drivers/p4_camera.c` | OV5647 MIPI-CSI 初始化和帧采集 |
| `main/drivers/sdcard.c` | SDMMC 4-bit 挂载（SLOT_0，片内 LDO4 供电） |
| `main/vision/roi_tracker.c` | 单 ROI 阈值分割 + 质心计算（YUV422 Y 通道） |
| `main/acquisition/camera_capture_task.c` | 摄像头帧 → frame_queue |
| `main/acquisition/roi_process_task.c` | frame_queue → batch_queue |
| `main/acquisition/sdcard_write_task.c` | batch_queue → CSV 文件 |
| `main/storage/csv_writer.c` | FATFS CSV 写入封装 |
| `main/config/app_config.c` | 默认 ROI 配置和运行参数 |
| `experiments/wifi_only/` | 独立 WiFi 最小验证工程 |

## 编译和烧录

需要 ESP-IDF v5.5.4。

**注意：不要使用 `idf.py export`，直接用以下路径调用工具链：**

```powershell
$env:PATH = 'C:\Espressif\tools\cmake\3.30.2\bin;' +
            'C:\Espressif\tools\ninja\1.12.1;' +
            'C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin;' +
            'C:\Users\zhaoy\.espressif\python_env\idf5.5_py3.12_env\Scripts;' +
            $env:PATH

# 编译
python 'D:\.espressif\v5.5\v5.5.4\esp-idf\tools\idf.py' `
    '--project-dir' 'd:\...\35_ESP32_S3_CAM' 'build'

# 烧录
python 'D:\.espressif\v5.5\v5.5.4\esp-idf\tools\idf.py' `
    '--project-dir' 'd:\...\35_ESP32_S3_CAM' '-p' 'COM8' '-b' '460800' 'flash'
```

## Managed Components 补丁说明

由于 ESP-IDF v5.5.4 与 esp_wifi_remote 0.14.x / esp_hosted 1.4.7 之间存在版本变量名不匹配问题，以下 managed component 文件经过手动修改（不纳入 git）：

| 文件 | 修改内容 |
|------|--------|
| `managed_components/espressif__esp_hosted/Kconfig` | `PRIV_SDIO_OPTION` 增加 `default y if IDF_TARGET_ESP32P4` 回退 |
| `managed_components/espressif__esp_hosted/host/api/include/esp_hosted_config.h` | `#else #error "Unknown Slave Target"` 改为回退 `H_SLAVE_TARGET_ESP32C6 1` |
| `managed_components/espressif__esp_hosted/host/drivers/transport/sdio/sdio_wrapper.c` | `config.slot = sdio_config->slot` |
| `managed_components/espressif__esp_hosted/host/drivers/transport/sdio/sdio_drv.c` | 任务退出前 `vTaskDelete(NULL)` |
| `managed_components/espressif__esp_wifi_remote/Kconfig` | `$ESP_IDF_VERSION` → `$IDF_VERSION` |
| `managed_components/espressif__esp_wifi_remote/idf_v5.5.4/` | 从 `idf_v5.5/` 完整复制以匹配版本路径 |

重新 `idf.py menuconfig` 或执行 `idf.py update-dependencies` 后需重新应用这些补丁。

## 稳定性测试结果（v0.1.0，120 秒 / 3600 帧）

| 指标 | 数值 |
|------|------|
| 实测帧率 | 29.997 ~ 30.005 fps |
| 帧间隔均值 | 33319 μs（理论 33333 μs，误差 < 0.05%） |
| 帧间隔抖动 (max-avg) | 89 μs（< 0.27%） |
| 丢帧数 | 0 |
| ROI 处理耗时均值 | 8.0 ms（YUV422 Y 通道） |
| CSV 每批次写入均值 | 26.5 ms |

## 已知限制

- ROI 坐标为调试默认值，需针对实际目标位置重新标定。
- PWDN/RESET/XCLK 不由软件控制，硬件已将其保持在非激活状态。
- v0.2.0 实测采集帧率约 23 fps（目标 30 fps），与 v0.1.0 的 30 fps 存在差距，原因待排查（疑为 YUV422 批次处理与 WiFi 初始化后任务优先级调度变化的组合效应）。
- SD 卡与 C6 SDIO 槽位分离后长时间同时运行的稳定性尚未全面测试。
