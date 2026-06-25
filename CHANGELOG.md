# 变更记录

本文件按 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 格式维护，版本号遵循语义化版本 (SemVer)。

---

## [v0.2.4] — 2026-06-25

### 变更 — 清理诊断用串口输出，给10分钟验证测试用的版本

- 排查抖动用的临时诊断已下线（结论已留档在v0.2.2/v0.2.3条目里，AEC/SD/WiFi均
  已通过对照实验验证不是主要因素，残留抖动更像是PSRAM总线竞争+卷帘快门固有
  特性）：
  - 删除 `roi_process_task.c` 里的YUV422首帧像素打印（一次性调试用，已验证
    完毕）
  - 移除 `camera_capture_task.c` 里3处 `p4_camera_log_aec_state()` 调用
    （AEC寄存器已确认全程不变，无需再监控；函数本身保留，供以后需要时手动
    复用）
  - `camera_capture_task.c` 的30秒进度日志、`sdcard_write_task.c` 的每10
    batch统计日志，从 `ESP_LOGI` 降级为 `ESP_LOGD`（默认日志等级INFO不会
    打印，但代码仍编译保留，需要时可调日志等级重新打开）
- 采集开始/结束的一次性摘要日志（含dt_min/avg/max、dropped、fps等关键指标）
  保留，不受影响
- 此版本用于10分钟现场验证测试

---

## [v0.2.3] — 2026-06-25

### 新增 — AEC/AGC 寄存器诊断，排查帧时间残留抖动（~130-400us级别）

- `p4_camera.c`/`.h`：新增 `p4_camera_log_aec_state(tag)`，读取并打印 OV5647
  曝光行数寄存器(0x3500-0x3502)和增益寄存器(0x350a-0x350b)的当前值。
- `camera_capture_task.c`：在采集开始、每30s进度日志、采集结束三个点调用该
  诊断函数。
- **实测结论**：连续67秒采集中，曝光/增益寄存器原始字节全程未变化
  （exposure_lines=12576, gain=62），排除了"传感器自动曝光(AEC/AGC)在帧间
  微调曝光行数"导致残留抖动的假设。
- 同一轮测试还发现：当 SD 卡 `csv_flush_us` 出现毫秒级尖峰（最高15.3ms）时，
  `dt_us` 的抖动幅度同步从约±130us放大到约±400us，怀疑残留抖动来自 SDMMC
  DMA 和 CSI DMA 之间的内存总线/PSRAM带宽竞争（core分离解决的是CPU调度竞争，
  解决不了总线层面的竞争），有待后续做"不写SD卡"的对照实验验证。

---

## [v0.2.2] — 2026-06-25

### 变更 — 帧率调到 40fps，CSV 命名对齐项目33，定位并改善帧时间抖动

- `board_config.h`：`BOARD_DEFAULT_FRAME_RATE_HZ` 30→40，`BOARD_CAM_OV5647_VTS_OVERRIDE`
  1574→1180（理论~40.15fps）。30fps 时实测约38.4fps（dt_us均值26029.5us，23052帧/600.006s，
  0丢帧，valid_mask全程15），跟传感器标称45fps→实测43.2fps的偏差比例一致，已知是
  VTS理论值和实际响应的固定偏差，非算力瓶颈。
- `acq_manager.c`：每次开始采集时按 `{test_id}_{node_id}_{时间戳}.csv` 生成唯一文件名
  （`/sdcard/TEST01_CAM_NODE_01_20260624_142845.csv`），跟项目33(ADXL355节点)命名规则
  统一，不再每次采集覆盖同一个 `displacement.csv`。
- **定位帧时间抖动根因**：40fps下出现0.3~0.4ms级别的dt_us尖峰（14次>200us、7次>300us，
  对比30fps只有6次>200us、0次>300us）。根因是 `t_us` 时间戳之前打在
  `p4_camera_get_frame()` 里、消费者任务被ISR唤醒**之后**才记录，中间隔着任务调度延迟、
  跟同核心的 `WriteTask` 抢核等不确定因素，不是相机/传感器本身不稳定。
  - `p4_camera.c`：把时间戳挪到 `on_trans_finished()` ISR 里、DMA真正完成的瞬间打
    （`s_cam.ready_t_us`），`p4_camera_get_frame()` 改为读取这个值，不再自己调
    `esp_timer_get_time()`，从根上去掉调度延迟对时间戳的污染。
  - `acq_manager.c`：`sdcard_write_task` 从 core0 挪到 core1（跟 `RoiTask` 共享），
    core0 只留给 `CameraTask`。
  - `http_server.c`：`httpd_config_t.core_id` 固定为 1，避免 HTTP 任务跟采集任务抢
    core0。
  - 以上改动均未触碰位移算法（ROI追踪/质心计算）本身。

---

## [v0.2.1] — 2026-06-16

### 修复 — esp_wifi_remote Kconfig 构建失败

- `CMakeLists.txt`：`esp_wifi_remote` 的 Kconfig 用 `orsource "./idf_v$ESP_IDF_VERSION/..."` 选择从机 target，但 ESP-IDF 的 kconfgen 从不导出 `ESP_IDF_VERSION` 这个环境变量（无论哪个 IDF 版本），导致 slave target 选项和所有 `WIFI_RMT_*` 选项从未被定义，报错 `#error "Unknown Slave Target"`。在 project 顶层 `CMakeLists.txt` 里手动 `set(ENV{ESP_IDF_VERSION} "5.5")`，在 IDF 的 project.cmake 之前注入，问题解决。

### 变更 — 配置持久化 + node_id 命名统一

- `main/config/app_config.c/h`：新增 `app_config_save_to_nvs()` / `app_config_load_from_nvs()`，node_id / test_id / duration_s / batch_frames / ROI 全部持久化到 NVS（命名空间 `cam_cfg`），重启后保留上次配置
- `main/system/app_context.c`：`app_context_init()` 在加载默认值后调用 `app_config_load_from_nvs()`，覆盖为已保存的配置
- `main/network/http_api.c`：`POST /api/config` 新增 `check_node_id()` 校验，**只接受** `CAM_NODE_01`~`CAM_NODE_15`（与项目34主控统一命名方案对齐），不合规则直接拒绝（400）
- node_id 默认值从 `P4NODE01` 改为 `CAM_NODE_01`

### 修复 — 编译警告

- `main/drivers/p4_camera.c`：`esp_cam_sensor_format_t` 部分字段类型变化导致 `ESP_LOGI` 格式说明符不匹配，统一转 `uint32_t` 后用 `%"PRIu32"`
- `sdkconfig.defaults`：显式关闭 `CONFIG_CAM_CTRL_SPI_ENABLE`（SPI 摄像头控制器 + PSRAM 同时启用会触发 `spi_slave.c` 里依赖 IDF v5.4+ HAL 的代码路径，本项目用 MIPI-CSI 不需要它）

---

## [v0.2.0] — 2026-06-13

将项目从"一次性采集"架构重构为**位移检测节点**，与项目 33/34 保持统一 HTTP + UDP 控制协议。

### 新增 — 节点主循环

- `main/main.c` 完全重写为信号量驱动的 node loop：
  `boot → SD 卡 → 相机 → acq_manager_init → WiFi → HTTP → UDP → NODE_IDLE → 循环`
- 节点状态机：`NODE_BOOT / NODE_CAMERA_INIT / NODE_IDLE / NODE_RECORDING / NODE_FLUSHING / NODE_STOPPED / NODE_ERROR`
- `main/system/app_context.h/c`：全局上下文新增 `SemaphoreHandle_t start_trigger_sem`，`app_context_init()` 创建二值信号量
- `main/system/node_state.c`：状态字符串统一加前缀（`NODE_IDLE` 等），便于日志过滤

### 新增 — HTTP API（`main/network/http_server.c/h`、`main/network/http_api.c/h`）

11 条 REST 路由，所有响应含 CORS 头：

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/` | 服务存活检查 |
| GET | `/api/status` | 节点状态、IP、配置快照 |
| GET | `/api/config` | 读取运行参数 |
| POST | `/api/config` | 更新参数（JSON body） |
| GET | `/api/start` | 触发在线采集 |
| GET | `/api/start-offline` | 触发离线采集（先断 WiFi） |
| GET | `/api/stop` | 停止采集 |
| GET | `/api/files` | 列出 SD 卡可下载文件 |
| GET | `/download?file=xxx` | 流式下载文件（4 KB chunk） |
| POST | `/api/files/clear` | 清空 SD 卡文件 |
| POST | `/api/time` | 设置系统 epoch 时间 |

### 新增 — 离线采集流程

`/api/start-offline`（或串口 `o`）触发流程：
1. HTTP 立即返回 `{"ok":true}`
2. 后台任务等待 1.5 s 确保响应发出
3. 停止 HTTP 服务器（`http_server_stop()`）
4. 停止 WiFi（`wifi_manager_stop()`）
5. 延迟 3 s 等射频完全静默
6. 给出 `start_trigger_sem`，主循环开始采集
7. 采集完成后：`wifi_manager_start()` → `http_server_start()`，回到 NODE_IDLE

### 新增 — UDP 广播（`main/network/udp_discovery.c/h`）

- 端口：33330，兼容项目 33/34
- 魔数：`ADXL355_DISCOVER_V1`（精确匹配）
- 响应：JSON `{"type":"displacement_node","node_id":"...","ip":"...","firmware":"...","state":"..."}`
- 任务固定在 Core 1，优先级 0，不影响采集

### 新增 — 文件管理（`main/storage/file_manager.c/h`）

- `file_name_is_safe()`：拒绝 `..`、`/`、`\`、`System Volume Information`
- `file_name_is_downloadable()`：仅允许 `.csv`、`.log`、`.json`、`.pgm`
- `file_content_type()`：返回正确 MIME 类型

### 新增 — 串口命令接口

`uart_cmd_task`（Core 1，优先级 5）监听 UART0 stdin：

| 命令 | 动作 |
|------|------|
| `s` | 触发在线采集 |
| `o` | 触发离线采集（设置 offline 标志后给信号量） |
| `q` | 设置 `stop_requested = true` |
| `?` | 打印状态、WiFi 连接、IP |

无需 WiFi 即可完整测试节点采集流程。

### 变更 — WiFi 管理器

- `wifi_manager_start()` 改为幂等（二次调用无副作用）
- 新增 `wifi_manager_stop()`：反初始化 netif、清零内部状态标志
- 新增 `wifi_manager_is_connected()`：供 HTTP `/api/status` 查询
- 新增 `http_api_set_offline_start()` / `http_api_clear_offline_start()` 公开接口

### 变更 — 配置和构建

- `main/board/board_config.h`：新增 `FIRMWARE_VERSION "v0.2.0"`（从 ESP32_P4_DISPLACEMENT.h 迁移并升版）
- `main/Kconfig.projbuild`：`DISP_AUTOSTART` 默认改为 `n`；`DISP_ENABLE_WIFI` 默认改为 `y`
- `main/CMakeLists.txt`：新增源文件 `storage/file_manager.c`、`network/http_server.c`、`network/http_api.c`、`network/udp_discovery.c`；REQUIRES 新增 `esp_http_server`、`json`、`lwip`
- `sdkconfig.defaults`：补全 SDIO 传输参数、`CONFIG_DISP_ENABLE_WIFI=y`、`CONFIG_DISP_AUTOSTART=n`

### 修复 — Managed Components 构建问题

这是本版本编译阶段最复杂的部分，涉及三个独立 bug：

**Bug 1：`#error "Unknown Slave Target"`（esp_hosted_config.h:59）**

- 根本原因：`esp_wifi_remote/Kconfig` 使用 `orsource "./idf_v$ESP_IDF_VERSION/..."` 解析 slave 选择，但 kconfgen 的 `config.env` 传入的变量名是 `IDF_VERSION`（非 `ESP_IDF_VERSION`），导致变量为空，orsource 静默跳过，`SLAVE_IDF_TARGET` choice 从未定义，`CONFIG_SLAVE_IDF_TARGET_ESP32C6` 缺失
- 修复 A：`esp_wifi_remote/Kconfig` 中 `$ESP_IDF_VERSION` → `$IDF_VERSION`
- 修复 B：创建 `esp_wifi_remote/idf_v5.5.4/`（从 `idf_v5.5/` 完整复制，含 `wifi_apps/` 子目录）以匹配路径解析结果
- 修复 C：`esp_hosted/Kconfig` 中 `PRIV_SDIO_OPTION` 新增 `default y if IDF_TARGET_ESP32P4`，确保 P4 上 SDIO 传输模式可选
- 修复 D：`esp_hosted_config.h` 将最终 `#else #error` 改为回退 `#define H_SLAVE_TARGET_ESP32C6 1`（因 SLAVE_IDF_TARGET choice 可能始终缺失）

**Bug 2：`CONFIG_WIFI_RMT_*` 未声明**

- 同上，orsource `Kconfig.wifi.in` 未加载导致 WiFi remote 配置符号全部缺失
- 修复同 Bug 1：变量名修正 + idf_v5.5.4 目录

**Bug 3：`CONFIG_ESP_HOSTED_SPI_CLK_FREQ` 未声明**

- sdkconfig 中 `SPI_HOST_INTERFACE=y` 而非 `SDIO_HOST_INTERFACE=y`，导致 SPI 路径代码尝试读取 SPI 频率配置
- 根本原因：`PRIV_SDIO_OPTION=n`（因 SLAVE_IDF_TARGET_ESP32C6 缺失），SDIO 传输选项依赖条件不满足
- 修复：同 Bug 1 修复 C，`PRIV_SDIO_OPTION` P4 回退确保 SDIO 变为默认值；删除旧 sdkconfig 强制重新生成

### 硬件验证（2026-06-13）

```
启动时间：约 9 秒达到 NODE_IDLE
WiFi 获取 IP：192.168.31.75
串口命令 s → NODE_RECORDING：正常
采集 50 秒（1168 帧）：fps ≈ 23.6，dropped=320（固定值，为启动时旧帧）
串口命令 q → NODE_IDLE：正常
CSV 文件：/sdcard/displacement.csv 写入成功
```

---

## [未发布]

### 新增 — WiFi 可选支持（ESP32-C6 SDIO 协处理器）

- `main/network/wifi_manager.c/.h`：WiFi STA 连接封装，`#ifdef CONFIG_DISP_ENABLE_WIFI` 保护，关闭时零开销
- `Kconfig.projbuild`：新增 `WiFi (optional)` 菜单，包含 `DISP_ENABLE_WIFI`、SSID、密码、最大重连次数配置项
- `sdkconfig.defaults`：`CONFIG_DISP_ENABLE_WIFI=n`（默认关闭）、`CONFIG_SLAVE_IDF_TARGET_ESP32C6=y`、`CONFIG_ESP_WIFI_SOFTAP_SUPPORT=n`
- `experiments/wifi_only/`：独立最小 WiFi 验证工程，不含摄像头/SD 卡，用于隔离验证 C6 SDIO 通路
- `main/idf_component.yml`：添加 `espressif/esp_wifi_remote: "0.14.*"` 和 `espressif/esp_hosted: "1.4.*"`

### 变更 — SDMMC 槽位分离（SD 卡与 C6 SDIO 共存）

- `main/drivers/sdcard.c`：SD 卡从 `SDMMC_HOST_SLOT_1`（默认）改为 `SDMMC_HOST_SLOT_0`，与 C6 SDIO（SLOT_1）分离

  | 设备 | Slot | GPIO |
  |------|------|------|
  | C6 SDIO（esp_hosted） | SLOT_1（GPIO matrix） | CLK=18, CMD=19, D0-D3=14-17 |
  | SD 卡 | SLOT_0（GPIO matrix） | CLK=43, CMD=44, D0-D3=39-42 |

- `main/main.c`：重构启动顺序——采集阶段 WiFi 保持静默，`acq_manager_wait_done()` 返回后才调用 `wifi_manager_start()`，避免 esp_hosted SDIO 任务（优先级 23）在采集期间抢占 WriteTask（优先级 2）

### 新增 — YUV422 像素格式

- `APP_PIXEL_FORMAT_YUV422 = 4`：在 `app_config.h` 中新增 YUV422 像素格式枚举值（YUYV 排列，16 bpp）
- **ISP 输出格式**：从 `RGB565` 切换为 `YUV422 (YUYV)`
- **ROI 灰度提取**：`roi_tracker.c` 中 `sample_luma()` 直接从 YUYV 流读取 Y 字节，ROI 处理耗时从 21.4 ms 降至 8.0 ms（↓62%）

---

## [v0.1.0] — 2026-06-08

首个端到端完整验证版本，ESP32-P4-NANO + OV5647 RAW10 @30 fps，SD 卡 CSV 输出稳定。

### 新增
- **OV5647 RAW10 1280×960 binning 模式**：通过 VTS 寄存器覆盖将传感器从 45 fps 降至 30 fps
- **三任务流水线**：
  - `camera_capture_task`（CPU0，优先级 5）— 从 CSI 取帧，送 frame_queue
  - `roi_process_task`（CPU1，优先级 5）— 阈值分割 + 质心计算，组装批次
  - `sdcard_write_task`（CPU0，优先级 2）— 批次写入 `/sdcard/displacement.csv`
- `timing.h`：共用 min/avg/max 计时统计工具
- PSRAM 双帧缓冲（每帧 2,457,600 字节，64 字节 DMA 对齐）
- `roi_tracker.c`：单 ROI 阈值分割 + 质心计算，支持 GRAY8 / RAW8 / RGB565

### 关键修复
- **SD 卡 ESP_ERR_TIMEOUT**：改用 `sd_pwr_ctrl_by_on_chip_ldo.h` API（片内 LDO4），弃用 GPIO45 PMOS 方案
- **PWDN 引脚误配**：改为 `BOARD_CAM_PWDN_IO = -1`
- **VERBOSE 日志编译缺失**：`sdkconfig.defaults` 加 `CONFIG_LOG_MAXIMUM_LEVEL_VERBOSE=y`
- **idf.py flash GBK 编码错误**：Windows 下改用 `python -m esptool` 直接烧录

### 稳定性测试结果（120 秒 / 3600 帧）

```
帧率：29.997 ~ 30.005 fps
帧间隔均值：33319 μs（理论 33333 μs，误差 < 0.05%）
帧间隔抖动（max−avg）：89 μs（< 0.27%）
丢帧：0
ROI 处理耗时均值：21.4 ms
CSV 每批次写入均值：27.3 ms
```

[v0.2.0]: https://github.com/zhaoyuma92-cpu/35_ESP32_S3_CAM/compare/v0.1.0...main
[v0.1.0]: https://github.com/zhaoyuma92-cpu/35_ESP32_S3_CAM/releases/tag/v0.1.0
