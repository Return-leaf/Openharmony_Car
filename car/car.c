/*
 * 小车控制 + WebSocket 遥控 - 主文件
 * NL63pro (WS63) 开发板
 *
 * GPIO 方向引脚:
 *   GPIO1  - IN1 - 右边前进
 *   GPIO4  - IN2 - 右边后退
 *   GPIO14 - IN3 - 左边前进
 *   GPIO3  - IN4 - 左边后退
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "cmsis_os2.h"
#include "iot_i2c.h"
#include "ohos_init.h"
#include "car.h"
#include "car_wifi.h"
#include "car_websocket.h"

/* ---------- PCF8575 I2C ---------- */
#define PCF8575_ADDR  0x20
#define I2C_ID        0
#define I2C_BAUDRATE  100000

/* PCF8575 位映射: P0=IN1(右前) P1=IN2(右后) P2=IN3(左前) P3=IN4(左后) P4=ENA P5=ENB */
#define P_IN1  0
#define P_IN2  1
#define P_IN3  2
#define P_IN4  3
#define P_ENA  4
#define P_ENB  5
#define M_IN1  (1 << P_IN1)
#define M_IN2  (1 << P_IN2)
#define M_IN3  (1 << P_IN3)
#define M_IN4  (1 << P_IN4)
#define M_ENA  (1 << P_ENA)
#define M_ENB  (1 << P_ENB)

static int pcf8575_write(unsigned short val)
{
    unsigned char d[2] = { val & 0xFF, (val >> 8) & 0xFF };
    unsigned int ret = IoTI2cWrite(g_i2c_id, g_pcf_addr, d, 2);
    if (ret != 0) {
        printf("[PCF] write fail bus=%d addr=0x%02X ret=%u\r\n", g_i2c_id, g_pcf_addr, ret);
    }
    return (ret == 0) ? 0 : -1;
}

static unsigned short g_dir = 0;

static void car_flush(void)
{
    pcf8575_write(g_dir | M_ENA | M_ENB);  /* ENA/ENB 始终 HIGH，全速 */
}

/* ==================================================================
 * 方向控制（只在内存设方向位，car_flush 统一写 I2C）
 * ================================================================== */

void STOP(void)       { g_dir = 0; }
void FORWARD(void)    { g_dir = M_IN1 | M_IN3; }
void BACK(void)       { g_dir = M_IN2 | M_IN4; }
void LEFT(void)       { g_dir = M_IN1; }
void RIGHT(void)      { g_dir = M_IN3; }
void TANKRIGHT(void)  { g_dir = M_IN1 | M_IN4; }
void TANKLEFT(void)   { g_dir = M_IN2 | M_IN3; }

/* ==================================================================
 * 遥控指令解析
 * ================================================================== */

void car_execute_command(const char *dir, int speed)
{
    if (dir == NULL) return;
    (void)speed;  /* ENA/ENB 始终 HIGH，全速，speed 暂不使用 */

    if (strcmp(dir, "stop") == 0) {
        STOP();
    } else if (strcmp(dir, "forward") == 0) {
        FORWARD();
    } else if (strcmp(dir, "backward") == 0) {
        BACK();
    } else if (strcmp(dir, "left") == 0) {
        LEFT();
    } else if (strcmp(dir, "right") == 0) {
        RIGHT();
    } else if (strcmp(dir, "drift_l") == 0) {
        TANKLEFT();
    } else if (strcmp(dir, "drift_r") == 0) {
        TANKRIGHT();
    } else {
        printf("[CAR] Unknown: %s:%d\r\n", dir, speed);
        return;
    }
    car_flush();
}

/* ==================================================================
 * 主控制线程
 * ================================================================== */

/* I2C 自动扫描：试不同总线号和 PCF8575 地址 (0x20-0x27) */
static int g_i2c_id  = 0;
static int g_pcf_addr = 0x20;

static void i2c_scan(void)
{
    int bus, addr, found = 0;
    unsigned char dummy[2] = {0, 0};

    for (bus = 0; bus < 2; bus++) {
        if (IoTI2cInit(bus, I2C_BAUDRATE) != 0) {
            printf("[I2C] bus %d init fail\r\n", bus);
            continue;
        }
        for (addr = 0x20; addr <= 0x27; addr++) {
            if (IoTI2cWrite(bus, addr, dummy, 2) == 0) {
                printf("[I2C] FOUND device at bus=%d addr=0x%02X\r\n", bus, addr);
                g_i2c_id   = bus;
                g_pcf_addr = addr;
                found = 1;
            }
        }
        if (!found) IoTI2cDeinit(bus);
    }
    if (found) {
        printf("[I2C] using bus=%d addr=0x%02X\r\n", g_i2c_id, g_pcf_addr);
    } else {
        printf("[I2C] PCF8575 not found on any bus! Check wiring\r\n");
        /* 用默认配置继续跑，方便远程调试 */
        IoTI2cInit(0, I2C_BAUDRATE);
    }
}

void car_main(void *arg)
{
    (void)arg;

    i2c_scan();

    if (car_wifi_connect() != 0) {
        printf("[CAR] WiFi connect failed!\r\n");
        return;
    }
    printf("[CAR] WiFi connected, IP=%s\r\n", car_wifi_get_ip_str());

    car_websocket_server_start(8080);
}

static void car_example(void)
{
    osThreadAttr_t attr = {0};
    attr.name       = "car_main";
    attr.stack_size = 8192;
    attr.priority   = osPriorityNormal;
    if (osThreadNew(car_main, NULL, &attr) == NULL) {
        printf("[CAR] Failed to create car_main thread!\r\n");
    }
}

APP_FEATURE_INIT(car_example);
