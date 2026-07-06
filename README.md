# OpenHarmony 小车 WebSocket 遥控

基于 OpenHarmony (WS63/NL63pro) 的 WiFi 小车，通过微信小程序摇杆实时遥控。

## 硬件

| 组件 | 型号 |
|------|------|
| 开发板 | NL63pro (HiSilicon WS63) |
| 系统 | LiteOS-M (OpenHarmony mini) |
| 电机驱动 | L298N / L9110S (H 桥) |
| 通信 | WiFi STA 连接手机热点 |

### 接线

| 开发板 GPIO | 电机驱动板 | 功能 |
|------------|-----------|------|
| GPIO1 | IN1 | 右轮前进 |
| GPIO4 | IN2 | 右轮后退 |
| GPIO14 | IN3 | 左轮前进 |
| GPIO3 | IN4 | 左轮后退 |

## 软件架构

```
微信小程序                   WS63 开发板
┌──────────┐    WebSocket    ┌──────────────────┐
│ 摇杆 UI   │ ←──────────→  │ car_websocket.c   │
│ index.js │  ws://IP:8080  │ lwIP TCP Server  │
└──────────┘                │         ↓         │
                            │ car.c             │
                            │ GPIO 电机控制      │
                            │         ↓         │
                            │ car_wifi.c        │
                            │ WiFi STA 热点连接   │
                            └──────────────────┘
```

### 通信协议

WebSocket 文本帧，格式 `direction:speed`

| 指令 | 动作 | 速度 |
|------|------|------|
| `forward:180` | 前进 | 120/180/255 |
| `backward:180` | 后退 | 120/180/255 |
| `left:180` | 左转 | 120/180/255 |
| `right:180` | 右转 | 120/180/255 |
| `drift_l:255` | 左漂移 | 255 |
| `drift_r:255` | 右漂移 | 255 |
| `stop:0` | 停止 | 0 |

## 编译 & 烧录

### 前置条件

- OpenHarmony 完整 SDK 环境
- `hb` 构建工具

### 修改 WiFi 凭据

编辑 `car/car_wifi.c`：

```c
#define CAR_WIFI_SSID       "你的热点名"
#define CAR_WIFI_PASSWORD   "你的热点密码"
#define CAR_WIFI_SEC_TYPE   WIFI_SEC_TYPE_WPA2PSK
```

### 编译

```bash
hb build
```

固件输出路径：`out/nl63pro/nl63pro/`

### 烧录

使用 HiBurn 或串口工具烧录到 NL63pro 开发板。

## 使用

1. **手机开热点** — 设置为 2.4GHz 频段（WS63 不支持 5GHz）
2. **开发板上电** — 自动连接热点，串口打印 IP 地址
3. **打开微信小程序** — 添加项目（AppID: `wx0a9a37c4eb006536`）
4. **点击顶部状态栏** — 输入小车 IP（串口显示的地址）
5. **摇杆遥控** — 虚拟摇杆：推方向行走，松手停止

### 小程序界面

- 🕹 **摇杆** — 360° 方向控制，死区 30px
- 🔥 **DRIFT 模式** — 原地漂移转向
- ⚡ **三挡调速** — 低速 (120) / 中速 (180) / 全速 (255)
- 📡 **状态栏** — 点击设置 IP，绿色 = 已连接，红色 = 未连接

## 目录结构

```
car/
├── car.c              # GPIO 方向控制 + 指令解析
├── car.h              # 头文件
├── car_wifi.c         # WiFi STA 连接 (热点)
├── car_wifi.h         # WiFi 头文件
├── car_websocket.c    # WebSocket Server (RFC 6455, lwIP)
├── car_websocket.h    # WebSocket 头文件
└── BUILD.gn           # GN 构建配置

wechat/
├── app.js             # 小程序入口
├── app.json           # 全局配置
├── app.wxss           # 全局样式
├── index.js           # 摇杆控制器逻辑
├── index.wxml         # UI 布局
├── index.wxss         # 样式 (暗色主题)
├── project.config.json
└── project.private.config.json
```

## 技术细节

- **WebSocket 握手**: mbedtls SHA1 + Base64 计算 `Sec-WebSocket-Accept`
- **帧解析**: 支持 RFC 6455 掩码帧、分片读取（`recv_exact`）
- **握手优化**: 查找 `\r\n\r\n` 结束标记，防止吞掉 WebSocket 帧
- **PING/PONG**: 收到 PING 自动回复 PONG（微信客户端要求）
- **WiFi 连接**: STA 状态机，失败自动重试

## License

Apache License 2.0
