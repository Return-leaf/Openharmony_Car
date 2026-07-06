# OpenHarmony 小车 WebSocket 遥控 (PCF8575 I2C 版)

基于 OpenHarmony (WS63/NL63pro) 的 WiFi 小车，通过 PCF8575 I2C IO 扩展模块驱动电机，微信小程序摇杆实时遥控。

## 硬件

| 组件 | 型号 |
|------|------|
| 开发板 | NL63pro (HiSilicon WS63) |
| 系统 | LiteOS-M (OpenHarmony mini) |
| IO 扩展 | PCF8575 (I2C, 地址 0x20) |
| 电机驱动 | L298N / L9110S (H 桥) |
| 通信 | WiFi STA 连接手机热点 |

### 接线

| PCF8575 | WS63 开发板 | 电机驱动 |
|---------|-----------|---------|
| SDA | IIC_SDA (GPIO15) | - |
| SCL | IIC_SCL (GPIO16) | - |
| VCC | 3.3V | - |
| GND | GND | - |
| P0 | - | IN1 (右前) |
| P1 | - | IN2 (右后) |
| P2 | - | IN3 (左前) |
| P3 | - | IN4 (左后) |
| P4 | - | ENA (右使能) |
| P5 | - | ENB (左使能) |

> WS63 的 I2C 在 GPIO15(SDA) + GPIO16(SCL)，总线号 1，必须先用 IoSetFunc 配置引脚复用再 IoTI2cInit。

## 编译

```bash
hb build
```

修改 WiFi 凭据：编辑 `car/car_wifi.c` 中 `CAR_WIFI_SSID` 和 `CAR_WIFI_PASSWORD`。

## 使用

1. 手机开热点 (2.4GHz)
2. 开发板上电，串口显示 `PCF8575 OK` 和 IP
3. 微信小程序输入 IP → 摇杆遥控

## 通信协议

WebSocket 文本帧 `direction:speed`

| 指令 | 动作 |
|------|------|
| forward:180 | 前进 |
| backward:180 | 后退 |
| left:180 | 左转 |
| right:180 | 右转 |
| drift_l:255 | 左漂移 |
| drift_r:255 | 右漂移 |
| stop:0 | 停止 |

## License

Apache License 2.0
