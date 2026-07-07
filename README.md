# OpenHarmony 小车 WebSocket 遥控 (PCF8575 I2C 版)

基于 OpenHarmony (WS63/NL63pro) 的 WiFi 小车，通过 **PCF8575 I2C IO 扩展模块** 驱动 L298N/L9110S 电机驱动板，微信小程序摇杆实时遥控。**零 WS63 GPIO 占用**，全部 IO 留给后续外设。

---

## 硬件

| 组件 | 型号/说明 |
|------|----------|
| 开发板 | NL63pro (HiSilicon WS63, LiteOS-M) |
| IO 扩展 | PCF8575 (I2C, 16 路输出, 默认地址 0x20) |
| 电机驱动 | L298N 或 L9110S (H 桥, 双路) |
| 小车底盘 | 4 轮, 左右两侧电机并联 |
| 通信 | WiFi STA 连接手机热点 (2.4GHz) |

### 开发板引脚布局 (NL63pro)

```
左排 (从上到下)                    右排 (从上到下)
1:  5V                              1:  IIC_SDA (GPIO15)
2:  GND                             2:  IIC_SCL (GPIO16)
3:  3.3V                            3:  IO_14 (GPIO14)
4:  GND                             4:  IO_03 (GPIO3)
5:  IO_01 (GPIO1)                   5:  GND
6:  IO_04 (GPIO4)                   6:  IO_10 (GPIO10)
7:  IO_13 (GPIO13)                  7:  GND
8:  IO_08 (GPIO8)                   8:  RST
9:  IO_07 (GPIO7)                   9:  IO_00 (GPIO0)
10: IO_05 (GPIO5)                   10: IO_06 (GPIO6) ⚠️ 不可用!
```

> ⚠️ **GPIO6 禁用**：配置为任何功能都会导致串口乱码和烧录失败。
> ⚠️ **GPIO5 无 GPIO 功能**：WS63 上 GPIO5 只有 PWM/UART/SPI 复用，不能当普通 GPIO 输出。

### 接线

| PCF8575 | 开发板 | 电机驱动板 |
|---------|--------|----------|
| VCC | 3.3V (左排 Pin3) | - |
| GND | GND (左排 Pin2) | - |
| SDA | IIC_SDA (右排 Pin1) | - |
| SCL | IIC_SCL (右排 Pin2) | - |
| INT (上划线) | 不接 | - |
| P0 | - | IN1 (右轮前进) |
| P1 | - | IN2 (右轮后退) |
| P2 | - | IN3 (左轮前进) |
| P3 | - | IN4 (左轮后退) |
| P4 | - | ENA (右轮使能, 全速) |
| P5 | - | ENB (左轮使能, 全速) |
| P6-P15 | - | 空闲, 预留给其他外设 |

---

## 已尝试 & 失败的方案 (踩坑记录)

### ❌ WS63 硬件 PWM

**尝试**：GPIO5(PWM5) + GPIO10(PWM2) 输出 PWM 调速。

**失败原因**：
- WS63 使用 **V151 PWM 硬件**，正确 API 是 `uapi_pwm_open → uapi_pwm_set_group → uapi_pwm_start_group`
- SDK 预编译的 `iot_pwm.c` 适配层用的是 **V150 API** (`uapi_pwm_start`)，在 V151 上是空函数
- `hal_iot_pwm.c` 有正确的 V151 代码，但在两个预编译 `.a` 库中重复定义 (`libhal_iothardware.a` + `libnl63pro_peripheral.a`)，链接冲突
- 任何引用了 PWM 符号的代码都会导致链接器拉入 PWM 驱动库，其静态初始化破坏 WiFi 时钟，导致 WiFi 连不上

### ❌ 直接 GPIO 使能 (ENA/ENB)

**尝试**：GPIO5 + GPIO10 作为普通 GPIO 控制 ENA/ENB 通断。

**失败原因**：GPIO5 在 WS63 上没有 `IOT_IO_FUNC_GPIO_5_GPIO` 功能（仅 PWM5/UART2/SPI）。

### ❌ I2C 盲目扫描 (第一版)

**尝试**：直接 `IoTI2cInit(0)` 后扫描 0x20-0x27。

**失败原因**：WS63 的 I2C 信号必须先用 `IoSetFunc` 配置 GPIO15/16 的引脚复用为 I2C 功能，否则 I2C 控制器不会路由到物理引脚。

### ✅ PCF8575 I2C (最终方案)

**关键发现**：参考 `b5_oled_i2c` 和 `f7_HealthService/aht30_i2c_example`，WS63 的正确 I2C 初始化：

```c
IoTGpioInit(IOT_IO_NAME_GPIO_15);
IoTGpioInit(IOT_IO_NAME_GPIO_16);
IoSetFunc(IOT_IO_NAME_GPIO_15, IOT_IO_FUNC_GPIO_15_I2C1_SDA);
IoSetFunc(IOT_IO_NAME_GPIO_16, IOT_IO_FUNC_GPIO_16_I2C1_SCL);
IoTI2cInit(1, 100000);  // I2C 总线 1
```

### ❌ 遥控不稳定 (已修复)

**症状**：按住摇杆不动 → 小车几秒后停止或锁死方向。

**根因**：微信小程序 `touchMove` 事件只在手指_移动_时触发。手指按住不动 → 无事件 → 不发新指令。

**修复**：加 300ms 心跳定时器 (`setInterval`)，手指按下期间自动重发当前指令。

---

## 软件架构

```
微信小程序                        WS63 开发板
┌───────────────┐   WebSocket    ┌─────────────────────┐
│ index.js      │ ←──────────→  │ car_websocket.c      │
│ 摇杆 + 心跳    │ ws://IP:8080  │ lwIP TCP Server     │
│ (300ms 重发)   │               │ RFC 6455 + PING/PONG │
└───────────────┘               │          ↓            │
                                │ car.c                │
                                │ PCF8575 I2C 驱动      │
                                │ 6 路输出, 单次写入     │
                                │ I2C 失败自动重试       │
                                │          ↓            │
                                │ car_wifi.c           │
                                │ STA 模式 + DHCP       │
                                │ 失败自动重连           │
                                └─────────────────────┘
```

### 通信协议

WebSocket 文本帧：`direction:speed`

| 指令 | 小车动作 | 说明 |
|------|---------|------|
| `forward:180` | 前进 | speed: 120(低速)/180(中速)/255(全速) |
| `backward:180` | 后退 | 同上 |
| `left:180` | 左前转 | 仅左侧前进 |
| `right:180` | 右前转 | 仅右侧前进 |
| `drift_l:255` | 左漂移 | 左侧后退+右侧前进 (原地旋转) |
| `drift_r:255` | 右漂移 | 右侧后退+左侧前进 (原地旋转) |
| `stop:0` | 停止 | 全部停机 |

> 当前版本 ENA/ENB 始终 HIGH（全速运行），speed 参数在 car.c 中被 `(void)speed` 忽略，保留该字段为后续调速做准备。

---

## 编译 & 烧录

### 前置条件

- OpenHarmony 完整 SDK 环境
- `hb` 构建工具
- RISC-V 交叉编译工具链

### 修改 WiFi 凭据

编辑 `car/car_wifi.c`：

```c
#define CAR_WIFI_SSID       "你的手机热点名"
#define CAR_WIFI_PASSWORD   "你的热点密码"
#define CAR_WIFI_SEC_TYPE   WIFI_SEC_TYPE_WPA2PSK  // 根据实际加密类型修改
```

### 编译

```bash
cd /path/to/openharmony
hb build
```

固件输出路径：`out/nl63pro/nl63pro/`

### 烧录

使用串口工具 (HiBurn 或类似) 烧录 `.bin` 文件到 NL63pro。

### 串口监控

波特率 921600（或 115200），上电后关键日志：

```
[CAR] PCF8575 OK (bus=1, addr=0x20)   ← I2C 通信正常
[CAR_WIFI]::Got IP: 10.11.222.xxx     ← WiFi 获取 IP
[CAR_WS]::Listening on port 8080      ← WebSocket 就绪
[CAR_WS]::Client connected!           ← 小程序连上
[CAR_WS]::Cmd: forward:180            ← 收到遥控指令
```

如果看到 `PCF8575 no response!` → 检查 SDA/SCL 接线和模块供电。

---

## 使用

1. **手机开热点** — 必须设置为 **2.4GHz** 频段 (WS63 不支持 5GHz)
2. **开发板上电** — LED 亮起，等待串口打印 `Got IP: x.x.x.x`
3. **微信开发者工具** 打开 `wechat/` 目录 (AppID: `wx0a9a37c4eb006536`)
4. **点击顶部状态栏** → 输入串口显示的 IP 地址 → 确认
5. **摇杆遥控** — 推方向行走，松手即停

---

## 目录结构

```
car/
├── car.c              # PCF8575 I2C 电机控制 + 指令解析
├── car.h              # 电机控制 API 声明
├── car_wifi.c         # WiFi STA 状态机 (扫描→连接→DHCP)
├── car_wifi.h         # WiFi API 声明
├── car_websocket.c    # RFC 6455 WebSocket Server (lwIP + mbedtls)
├── car_websocket.h    # WebSocket API 声明
└── BUILD.gn           # GN 构建配置

wechat/
├── app.js / app.json / app.wxss   # 小程序入口和全局配置
├── index.js                       # 摇杆逻辑 + 心跳定时器
├── index.wxml                     # UI 布局
├── index.wxss                     # 暗色主题样式
├── project.config.json
└── project.private.config.json
```

---

## 技术要点

| 方面 | 实现 |
|------|------|
| I2C 引脚 | GPIO15→SDA, GPIO16→SCL, 必须先 IoSetFunc 再 IoTI2cInit |
| I2C 总线 | 总线 1, 速率 100kHz |
| PCF8575 地址 | 0x20 (无地址跳线) |
| PCF8575 写入 | 2 字节小端序, 每次指令只写一次 I2C, 失败自动重试 |
| WebSocket | mbedtls SHA1+Base64 握手, 支持 RFC 6455 掩码帧, PING→PONG 回复 |
| 握手防吞帧 | 查找 `\r\n\r\n` 保存溢出数据到预读缓冲区 |
| 心跳 | 小程序 300ms setInterval, touchEnd 清除 |
| I2C 容错 | 写入失败打印错误并重试一次 |
| WiFi 安全 | 默认 WPA2-PSK, 可改为 WPA3/SAE |

## License

Apache License 2.0
