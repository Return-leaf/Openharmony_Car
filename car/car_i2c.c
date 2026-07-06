/*
 * 小车控制 + WebSocket 遥控 — PCF8575 I2C IO 扩展版
 * NL63pro (WS63) 开发板
 *
 * PCF8575 P0-P5 映射:
 *   P0 = IN1 右前    P1 = IN2 右后
 *   P2 = IN3 左前    P3 = IN4 左后
 *   P4 = ENA 右使能  P5 = ENB 左使能
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

/* ---------- PCF8575 ---------- */
#define PCF8575_ADDR   0x20
#define I2C_ID         0
#define I2C_BAUDRATE   100000

/* PCF8575 输出位 */
#define P_IN1   0   /* 右前 */
#define P_IN2   1   /* 右后 */
#define P_IN3   2   /* 左前 */
#define P_IN4   3   /* 左后 */
#define P_ENA   4   /* 右使能 */
#define P_ENB   5   /* 左使能 */

/* 位掩码 */
#define M_IN1   (1 << P_IN1)
#define M_IN2   (1 << P_IN2)
#define M_IN3   (1 << P_IN3)
#define M_IN4   (1 << P_IN4)
#define M_ENA   (1 << P_ENA)
#define M_ENB   (1 << P_ENB)
#define M_DIR   (M_IN1 | M_IN2 | M_IN3 | M_IN4)

static void pcf8575_write(unsigned short val)
{
    unsigned char data[2];
    data[0] = val & 0xFF;
    data[1] = (val >> 8) & 0xFF;
    IoTI2cWrite(I2C_ID, PCF8575_ADDR, data, 2);
}

/* 方向位模式 */
#define DIR_STOP      0x0000
#define DIR_FORWARD   (M_IN1 | M_IN3)
#define DIR_BACK      (M_IN2 | M_IN4)
#define DIR_LEFT      (M_IN1)
#define DIR_RIGHT     (M_IN3)
#define DIR_TANK_L    (M_IN2 | M_IN3)
#define DIR_TANK_R    (M_IN1 | M_IN4)

static unsigned short g_dir = DIR_STOP;
static int g_speed_on = 0;

/* 一次 I2C 写入：方向 + 使能合并 */
static void car_flush(void)
{
    unsigned short val = g_dir;
    if (g_speed_on) val |= (M_ENA | M_ENB);
    pcf8575_write(val);
}

/* ==================================================================
 * 使能控制
 * ================================================================== */

static void car_set_speed(int speed)
{
    g_speed_on = (speed > 0);
}

/* ==================================================================
 * 方向控制（只改内存，不写 I2C，car_flush 统一写入） ================================================================== */

void STOP(void)       { g_dir = DIR_STOP; }
void FORWARD(void)    { g_dir = DIR_FORWARD; }
void BACK(void)       { g_dir = DIR_BACK; }
void LEFT(void)       { g_dir = DIR_LEFT; }
void RIGHT(void)      { g_dir = DIR_RIGHT; }
void TANKRIGHT(void)  { g_dir = DIR_TANK_R; }
void TANKLEFT(void)   { g_dir = DIR_TANK_L; }

/* ==================================================================
 * 遥控指令解析
 * ================================================================== */

void car_execute_command(const char *dir, int speed)
{
    if (dir == NULL) return;

    if (strcmp(dir, "stop") == 0) {
        car_set_speed(0);
        STOP();
    } else if (strcmp(dir, "forward") == 0) {
        car_set_speed(speed);
        FORWARD();
    } else if (strcmp(dir, "backward") == 0) {
        car_set_speed(speed);
        BACK();
    } else if (strcmp(dir, "left") == 0) {
        car_set_speed(speed);
        LEFT();
    } else if (strcmp(dir, "right") == 0) {
        car_set_speed(speed);
        RIGHT();
    } else if (strcmp(dir, "drift_l") == 0) {
        car_set_speed(255);
        TANKLEFT();
    } else if (strcmp(dir, "drift_r") == 0) {
        car_set_speed(255);
        TANKRIGHT();
    } else {
        printf("[CAR] Unknown command: %s:%d\r\n", dir, speed);
        return;
    }
    car_flush();  /* 方向+速度合并一次 I2C 写入 */
}

/* ==================================================================
 * 主控制线程
 * ================================================================== */

void car_main(void *arg)
{
    (void)arg;

    /* I2C 初始化 */
    if (IoTI2cInit(I2C_ID, I2C_BAUDRATE) != 0) {
        printf("[CAR] I2C init failed!\r\n");
        return;
    }

    /* 全部输出清零 */
    pcf8575_write(0x0000);
    STOP();
    printf("[CAR] PCF8575 I2C ready (addr=0x%02X)\r\n", PCF8575_ADDR);

    /* WiFi */
    if (car_wifi_connect() != 0) {
        printf("[CAR] WiFi connect failed!\r\n");
        return;
    }
    printf("[CAR] WiFi connected, IP=%s\r\n", car_wifi_get_ip_str());

    /* WebSocket */
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
