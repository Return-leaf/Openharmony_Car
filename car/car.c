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

static void pcf8575_write(unsigned short val)
{
    unsigned char d[2] = { val & 0xFF, (val >> 8) & 0xFF };
    IoTI2cWrite(I2C_ID, PCF8575_ADDR, d, 2);
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

void car_main(void *arg)
{
    (void)arg;

    if (IoTI2cInit(I2C_ID, I2C_BAUDRATE) != 0) {
        printf("[CAR] I2C init fail!\r\n");
        return;
    }
    pcf8575_write(0x0000);
    printf("[CAR] PCF8575 ready (0x%02X)\r\n", PCF8575_ADDR);

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
