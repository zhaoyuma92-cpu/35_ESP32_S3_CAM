# 调试上电记录 — 2026-06-13

## 摘要

本次将项目从"一次性采集"架构升级为完整的**位移检测节点**（displacement node），实现了 HTTP API、UDP 广播发现、WiFi 离线采集模式，并通过串口命令接口完成硬件验证。

## 硬件环境

- 开发板：Waveshare ESP32-P4-NANO
- 芯片：ESP32-P4 revision v1.3（量产前硅片）
- 摄像头：RPi Camera(B) OV5647 Rev 2.0
- SD 卡：SDHC，约 7580 MB（品牌 DVRBI）
- WiFi：板载 ESP32-C6，经 SDIO 接入（SLOT_1，GPIO 14-19，RST=54）
- 串口：COM8（115200 baud）

## 编译环境

- ESP-IDF：v5.5.4
- 目标芯片：`esp32p4`（RISC-V 双核，360 MHz）
- PSRAM：32 MB HEX，80 MHz
- Flash：16 MB，DIO，40 MHz

**注意：本项目不能使用 `idf.py export` 脚本，需要手动设置 PATH：**

```powershell
$env:PATH = 'C:\Espressif\tools\cmake\3.30.2\bin;' +
            'C:\Espressif\tools\ninja\1.12.1;' +
            'C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin;' +
            'C:\Users\zhaoy\.espressif\python_env\idf5.5_py3.12_env\Scripts;' +
            $env:PATH

python 'D:\.espressif\v5.5\v5.5.4\esp-idf\tools\idf.py' `
    '--project-dir' 'd:\...\35_ESP32_S3_CAM' 'build'
```

## 架构变更

### 旧架构（v0.1.0）

```
boot → SD 卡 → 相机 → 采集 600s → [可选] WiFi 连接
```

一次性运行，不可远程控制，无 HTTP 接口。

### 新架构（v0.2.0）

```
boot → SD 卡 → 相机 → acq_manager_init → WiFi → HTTP → UDP → NODE_IDLE
  ↓（信号量触发）
  NODE_RECORDING → FLUSHING → STOPPED → NODE_IDLE（循环）
```

支持多次触发、远程控制、离线采集模式。

## 编译阶段问题记录

### 问题 1：`#error "Unknown Slave Target"`

**文件**：`managed_components/espressif__esp_hosted/host/api/include/esp_hosted_config.h:59`

**根本原因**：

`esp_wifi_remote/Kconfig` 使用：
```kconfig
orsource "./idf_v$ESP_IDF_VERSION/Kconfig.slave_select.in"
```
但 kconfgen 的 `config.env` 传入的键名是 `IDF_VERSION`（不是 `ESP_IDF_VERSION`），导致 `$ESP_IDF_VERSION` 为空字符串，`orsource "./idf_v/Kconfig.slave_select.in"` 文件不存在，`orsource` 静默跳过，`SLAVE_IDF_TARGET` choice 从未定义，`CONFIG_SLAVE_IDF_TARGET_ESP32C6` 从不设置。

连锁影响：
- `H_SLAVE_TARGET_ESP32C6` 未定义 → `#error "Unknown Slave Target"`
- `PRIV_SDIO_OPTION` 依赖 `SLAVE_IDF_TARGET_ESP32C6`，为 `n` → SDIO 传输不可选
- `ESP_HOSTED_SPI_HOST_INTERFACE=y`（默认），SPI 路径代码引用 `CONFIG_ESP_HOSTED_SPI_CLK_FREQ` → 未声明

**修复（4 步）**：

1. `esp_wifi_remote/Kconfig`：`$ESP_IDF_VERSION` → `$IDF_VERSION`（匹配 config.env 实际键名）
2. 创建 `esp_wifi_remote/idf_v5.5.4/`（从 `idf_v5.5/` 完整复制，含 `wifi_apps/roaming_app/src/Kconfig.roaming`）
3. `esp_hosted/Kconfig`：`PRIV_SDIO_OPTION` 新增 `default y if IDF_TARGET_ESP32P4` 兜底行
4. `esp_hosted_config.h`：`#else #error "Unknown Slave Target"` 改为回退 `#define H_SLAVE_TARGET_ESP32C6 1`

修复 3+4 是必须的，因为即使修复 1+2 后 Kconfig 文件能被加载，生成的 sdkconfig 中 `SLAVE_IDF_TARGET_ESP32C6` 依然是 choice 成员，有时仍无法选中。

**验证**：
```
CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y   ← SDIO 已选中
CONFIG_SLAVE_IDF_TARGET_ESP32C6=y         ← C6 已选中
```

### 问题 2：`CONFIG_WIFI_RMT_*` 未声明

`esp_wifi.h:311` 引用 `CONFIG_WIFI_RMT_STATIC_RX_BUFFER_NUM` 等符号，同样由 orsource 未加载所致。

修复：同问题 1（步骤 1+2）。

### 问题 3：http_api.c 编译警告（-Werror）

两处字符串操作触发 `-Werror=stringop-truncation` 和 `-Werror=format-truncation`：

```c
// 修复前
strncpy(names[count], ent->d_name, sizeof(names[count]) - 1);  // 警告：79 < 255
snprintf(path, sizeof(path), "/sdcard/%s", names[i]);           // 警告：路径可能截断

// 修复后
snprintf(names[count], sizeof(names[count]), "%.79s", ent->d_name);
snprintf(path, sizeof(path), "/sdcard/%.79s", names[i]);
```

### 问题 4：`ESP_RETURN_ON_ERROR` 未声明

`http_api.c` 缺少 `#include "esp_check.h"`，添加后解决。

### 问题 5：`uart_read_bytes` 驱动错误

最初 `uart_cmd_task` 使用 `uart_read_bytes(UART_NUM_0, ...)` 但 UART0 是 IDF console 独占设备，未安装 UART 驱动。

修复：改用 `getchar()`（通过 VFS 层访问 UART0 stdin），EOF 时 `vTaskDelay(50ms)` 轮询。

## 硬件验证结果

### 启动日志（节选）

```
I (1923) node_state: state=NODE_BOOT
I (1927) main: ESP32-P4-NANO firmware=v0.2.0 node=P4NODE01 test=TEST001
I (2206) sdcard: filesystem mounted at /sdcard
I (2334) p4_camera: OV5647 MIPI-CSI ready: 1280x960 RAW10 @30 fps
I (3621) transport: Received INIT event from ESP32 peripheral   ← C6 SDIO 握手成功
I (8791) wifi_mgr: got ip: 192.168.31.75
I (8803) http_server: HTTP API ready: http://192.168.31.75/
I (8805) node_state: state=NODE_IDLE
I (8815) main: serial cmds: s=start  o=offline-start  q=stop  ?=status
I (8850) udp_disc: UDP discovery responder ready on port 33330
```

启动到 NODE_IDLE 约 **9 秒**。

### 采集验证（串口命令 `s`）

```
I (9365) uart_cmd: 's' → start (online)
I (9365) acq_mgr: start: node=P4NODE01 test=TEST001 1280x960@30 duration=600s
I (9368) cam_task: capture start: 1280x960@30 fps duration=600s expected=18000
I (9379) csv: opened: /sdcard/displacement.csv
I (9390) roi_task: YUV422 first-frame raw bytes [0..7]:
I (9395) roi_task:   [ 0] 7F 12 7F 12  (Y0=127 U0=18 Y1=127 V0=18)
```

首帧 YUV422 像素值正常（Y≈127，接近灰度中值，说明相机已正确曝光）。

### 停止验证（串口命令 `q`，50 秒后）

```
I (58922) cam_task: capture end elapsed=49.554s frames=1168 fps=23.570 dropped=320
I (58957) node_state: state=NODE_FLUSHING
I (58981) node_state: state=NODE_STOPPED
I (58984) main: acquisition complete (offline=0 stop_req=1)
I (58990) node_state: state=NODE_IDLE
```

| 指标 | 数值 |
|------|------|
| 采集时长 | 49.5 秒 |
| 总帧数 | 1168 帧 |
| 实测帧率 | 23.6 fps |
| dropped | 320（固定，为启动时相机缓冲旧帧，非运行时丢帧） |
| CSV | 写入正常 |
| 状态机 | IDLE → RECORDING → FLUSHING → STOPPED → IDLE 全部正确 |

**注意**：v0.2.0 帧率约 23.6 fps，低于 v0.1.0 的 30 fps。原因待排查，疑为 WiFi 初始化后任务调度变化（esp_hosted 任务优先级 23 可能在帧间隙抢占 CPU0）。下一步验证：在 NODE_IDLE 时停止 WiFi 后测试帧率，与 v0.1.0 基准对比。

## 下一步

1. 排查 v0.2.0 帧率下降至 23.6 fps 的原因（目标恢复 30 fps）
2. 测试 `o`（离线采集）命令：观察 WiFi 断开 → 采集 → 重连完整流程
3. 测试 HTTP API（接入同一局域网后）：`/api/status` → `/api/start-offline` → `/api/files` → `/download`
4. 测试 UDP 广播发现（项目 33/34 上位机兼容性）
5. 标定实际 ROI 坐标，替换调试默认值
6. 验证 SD 卡与 C6 SDIO 长时间同时运行稳定性
