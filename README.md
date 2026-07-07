# OpenHarmony 小车 WebSocket 遥控 (PCF8575 I2C 版)

基于 OpenHarmony (WS63/NL63pro) 的 WiFi 小车，PCF8575 I2C IO 扩展模块驱动 L298N/L9110S 电机 + 四路 HC-SR04 超声波测距，微信小程序摇杆遥控。**零 WS63 GPIO 占用**。

---

## 硬件

| 组件 | 型号/说明 |
|------|----------|
| 开发板 | NL63pro (HiSilicon WS63, LiteOS-M) |
| IO 扩展 | PCF8575 (I2C, 16 路, 地址 0x20) |
| 电机驱动 | L298N 或 L9110S (H 桥, 双路) |
| 超声波 | HC-SR04 ×4 (前/后/左/右) |
| 通信 | WiFi STA 2.4GHz 连接手机热点 |

### 开发板引脚布局 (NL63pro)

```
左排 (从上到下)              右排 (从上到下)
1:  5V                        1:  IIC_SDA (GPIO15)
2:  GND                       2:  IIC_SCL (GPIO16)
3:  3.3V                      3:  IO_14 (GPIO14)
4:  GND                       4:  IO_03 (GPIO3)
5:  IO_01 (GPIO1)             5:  GND
6:  IO_04 (GPIO4)             6:  IO_10 (GPIO10)
7:  IO_13 (GPIO13)            7:  GND
8:  IO_08 (GPIO8)             8:  RST
9:  IO_07 (GPIO7)             9:  IO_00 (GPIO0)
10: IO_05 (GPIO5)             10: IO_06 (GPIO6) ⚠️ 不可用!
```

> ⚠️ **GPIO6 禁用**：配置为任何功能都会导致串口乱码和烧录失败。
> ⚠️ **GPIO5 无 GPIO 功能**：WS63 上 GPIO5 只有 PWM/UART/SPI 复用，不能当普通 GPIO 输出。

### 接线

#### 电机驱动 (PCF8575)

| PCF8575 | 电机驱动板 | 功能 |
|---------|----------|------|
| P0 | IN1 | 右前 |
| P1 | IN2 | 右后 |
| P2 | IN3 | 左前 |
| P3 | IN4 | 左后 |

#### PWM 调速 (WS63 直连)

| WS63 GPIO | 电机驱动板 | PWM 通道 |
|-----------|----------|----------|
| GPIO1 | ENA (右轮) | PWM1 |
| GPIO10 | ENB (左轮) | PWM2 |

#### 超声波 HC-SR04 (PCF8575)

| 超声波 | 引脚 | PCF8575 | 位 |
|--------|------|---------|-----|
| 4路 | VCC | 5V (左排 Pin1) | - |
| 4路 | GND | GND | - |
| 4路 Trig | **并联** | **P4** | bit4 |
| 前 Echo | | **P5** | bit5 |
| 后 Echo | | **P6** | bit6 |
| 左 Echo | | **P7** | bit7 |
| 右 Echo | | **P17** | **bit15** ⚠️ |

> ⚠️ **PCF8575 引脚号≠位号**。P0-P7 = bit0-7，P10-P17 = bit8-15。P17 在代码中是 bit15，**不是 bit17**。
> `1U << 17` 溢出 16 位被截断为 0，导致右 Echo 永远读不到。这是实际踩过的坑。

### PCF8575 引脚分配总览

| PCF8575 | 位 | 功能 |
|---------|-----|------|
| P0 | bit0 | IN1 (右前) |
| P1 | bit1 | IN2 (右后) |
| P2 | bit2 | IN3 (左前) |
| P3 | bit3 | IN4 (左后) |
| P4 | bit4 | Trig (4路超声波并联) |
| P5 | bit5 | 前 Echo |
| P6 | bit6 | 后 Echo |
| P7 | bit7 | 左 Echo |
| P10 | bit8 | 空闲 |
| P11 | bit9 | 空闲 |
| P12 | bit10 | 空闲 |
| P13 | bit11 | 空闲 |
| P14 | bit12 | 空闲 |
| P15 | bit13 | 空闲 |
| P16 | bit14 | 空闲 |
| P17 | bit15 | 右 Echo |

---

## 已尝试 & 失败的方案 (踩坑记录)

### ❌ WS63 硬件 PWM (第一版)

- SDK 预编译 `iot_pwm.c` 用 V150 API (`uapi_pwm_start`)，V151 硬件上是空函数
- `hal_iot_pwm.c` 有正确 V151 代码，但两个预编译 `.a` 库重复定义 → 链接冲突
- 引用 PWM 符号 → 链接器拉入 PWM 驱动 → 静态初始化破坏 WiFi 时钟 → WiFi 连不上

**最终方案**：依赖 `nl63pro_drivers`，使用板级 `hal_iot_pwm.c`（V151 正确实现），PWM 延迟初始化（第一条指令才初始化，不干扰 WiFi）。

### ❌ 直接 GPIO 使能 (ENA/ENB)

GPIO5 在 WS63 上没有 `IOT_IO_FUNC_GPIO_5_GPIO`，只能做 PWM/UART/SPI。

### ❌ I2C 盲目扫描

直接 `IoTI2cInit(0)` 后扫描 0x20-0x27 找不到设备——必须先 `IoSetFunc` 配置 GPIO15/16 为 I2C 功能。

**正确序列**（参考 `b5_oled_i2c` 和 `aht30_i2c_example`）：
```c
IoTGpioInit(GPIO15); IoSetFunc(GPIO15, IOT_IO_FUNC_GPIO_15_I2C1_SDA);
IoTGpioInit(GPIO16); IoSetFunc(GPIO16, IOT_IO_FUNC_GPIO_16_I2C1_SCL);
IoTI2cInit(1, 100000);
```

### ❌ 遥控不稳定 (手指按住不动就停)

`touchMove` 只在手指移动时触发。手指按住不动 → 无事件 → 不发指令。

**修复**：加 300ms `setInterval` 心跳，手指按下期间自动重发 `currentCmd`。

### ❌ 小程序首次连接无法遥控

`onReady` 自动连接上次保存的 IP → 旧连接状态异常。**修复**：改为手动输入 IP 点确定才连。

### ❌ 超声波线程创建失败

`osPriorityLow` 不在 WS63 允许范围（`osPriorityLow3` ~ `osPriorityHigh`）。**修复**：改用 `osPriorityLow3`。

### ❌ 右超声波始终无数据

PCF8575 P17 = bit15，但代码写了 `1U << 17` → 溢出 16 位被截断为 0。**修复**：`#define P_ECHO_R 15`。

### ⚠️ 超声波精度有限 (已知限制)

PCF8575 I2C 读一次 ~200us，而 Arduino `pulseIn()` 微秒级。计时分辨率 ~3.4cm，无法精确测距。**适合障碍检测，不适合精确测量。**

### ⚠️ I2C 总线竞争

`car_sonar_thread` 和 `car_main` 共享 I2C1 总线。**修复**：`osMutex` 保护所有 I2C 读写。

---

## 软件架构

```
微信小程序                        WS63 开发板
┌─────────────────┐  WebSocket   ┌──────────────────────┐
│ index.js        │ ←─────────→ │ car_websocket.c       │
│ 摇杆 + 心跳      │ ws://IP:8080 │ lwIP TCP Server      │
│ 超声波距离显示    │             │ PING/PONG 回复        │
└─────────────────┘             │          ↓             │
                   sonar JSON ← │ car_sonar_thread      │
                   {type:sonar}  │ 500ms 测距            │
                                │          ↓             │
                                │ car_ultrasonic.c      │
                                │ PCF8575 I2C pulseIn   │
                                │          ↓             │
                                │ car.c                 │
                                │ PCF8575 方向 + PWM 调速 │
                                │          ↓             │
                                │ car_wifi.c            │
                                │ STA + DHCP, 失败重连    │
                                └──────────────────────┘
```

### 通信协议

**小程序 → 小车** (WebSocket 文本帧)：

| 指令 | 动作 | 速度 |
|------|------|------|
| `forward:180` | 前进 | 锁车(120)/中速(180)/全速(255) |
| `backward:180` | 后退 | 同上 |
| `left:180` | 左转 | 同上 |
| `right:180` | 右转 | 同上 |
| `drift_l:255` | 左漂移 | 满速 |
| `drift_r:255` | 右漂移 | 满速 |
| `stop:0` | 停止 | 0 |

**小车 → 小程序** (JSON)：

```json
{"type":"sonar","front":17,"back":55,"left":3,"right":10}
```

`0` = 无回波（无障碍或 >4m）。

---

## 编译 & 烧录

```bash
# 修改 WiFi 凭据
vim car/car_wifi.c  # CAR_WIFI_SSID / CAR_WIFI_PASSWORD

# 编译
hb build

# 固件: out/nl63pro/nl63pro/
```

### 串口关键日志

```
[CAR] PCF8575 OK (bus=1, addr=0x20)   ← I2C 正常
[SONAR] ready (P4=Trig, Echo P5/P6/P7/P17)  ← 超声波就绪
[CAR] PWM ready (ch1+ch2)             ← PWM 就绪 (延迟初始化)
[CAR_WIFI]::Got IP: 10.11.222.xxx     ← IP 地址
[SONAR] {"type":"sonar","front":17,...}  ← 超声波数据
```

---

## 使用

1. 手机开 **2.4GHz** 热点
2. 开发板上电，串口看 IP
3. 微信开发者工具打开 `wechat/` (AppID: `wx0a9a37c4eb006536`)
4. 点击状态栏 → 输入 IP → 确定
5. 摇杆遥控；界面下方显示四方向超声波距离

---

## 目录结构

```
car/
├── car.c              # 主控: PCF8575 方向 + PWM 调速 + sonar 线程
├── car.h              # 电机控制 API
├── car_ultrasonic.c   # 四路 HC-SR04 PCF8575 I2C 测距
├── car_ultrasonic.h   # 超声波 API + I2C 互斥锁
├── car_wifi.c         # WiFi STA (扫描→连接→DHCP, 失败重试)
├── car_wifi.h
├── car_websocket.c    # RFC 6455 WebSocket Server (lwIP + mbedtls)
├── car_websocket.h    # WebSocket API + car_websocket_send()
└── BUILD.gn           # GN 构建 (nl63pro_drivers + mbedtls 依赖)

wechat/
├── app.js / app.json / app.wxss
├── index.js           # 摇杆 + 心跳 + onMessage 超声波解析
├── index.wxml         # UI (摇杆 + 挡位 + 超声波显示)
├── index.wxss         # 暗色主题
├── project.config.json
└── project.private.config.json
```

## 技术要点

| 方面 | 实现 |
|------|------|
| I2C | GPIO15(SDA)+GPIO16(SCL), bus=1, 100kHz, 先 IoSetFunc 再 IoTI2cInit |
| PCF8575 | addr=0x20, 2字节小端序, osMutex 保护, 失败重试 |
| WebSocket | mbedtls SHA1+Base64 握手, RFC 6455 掩码帧, PING→PONG, 预读缓冲防吞帧 |
| PWM | GPIO1(PWM1)+GPIO10(PWM2), nl63pro_drivers, 延迟初始化, 锁车0%/中速70%/全速100% |
| 超声波 | PCF8575 I2C 读, ~200us/轮, ~3.4cm 分辨率, 四路同时触发 |
| 心跳 | 小程序 300ms setInterval, touchEnd 清除 |
| 线程 | car_main(prio=normal, 8KB) + car_sonar(PrioLow3, 4KB) |

## License

Apache License 2.0
