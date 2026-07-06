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

static unsigned short g_pcf_state = 0;

static void pcf8575_write(unsigned short val)
{
    g_pcf_state = val;
    unsigned char data[2];
    data[0] = val & 0xFF;         /* 低字节 */
    data[1] = (val >> 8) & 0xFF;  /* 高字节 */
    IoTI2cWrite(I2C_ID, PCF8575_ADDR, data, 2);
}

/* 设置指定 pin 的值 */
static void pcf_set_bit(int bit, int val)
{
    if (val)
        g_pcf_state |= (1 << bit);
    else
        g_pcf_state &= ~(1 << bit);
    pcf8575_write(g_pcf_state);
}

/* ==================================================================
 * 使能控制
 * ================================================================== */

static void car_set_speed(int speed)
{
    if (speed > 0) {
        pcf_set_bit(P_ENA, 1);
        pcf_set_bit(P_ENB, 1);
    } else {
        pcf_set_bit(P_ENA, 0);
        pcf_set_bit(P_ENB, 0);
    }
}

/* ==================================================================
 * 方向控制
 * ================================================================== */

void STOP(void)
{
    pcf_set_bit(P_IN1, 0);
    pcf_set_bit(P_IN2, 0);
    pcf_set_bit(P_IN3, 0);
    pcf_set_bit(P_IN4, 0);
}

void FORWARD(void)
{
    pcf_set_bit(P_IN1, 1);
    pcf_set_bit(P_IN2, 0);
    pcf_set_bit(P_IN3, 1);
    pcf_set_bit(P_IN4, 0);
}

void LEFT(void)
{
    pcf_set_bit(P_IN1, 1);
    pcf_set_bit(P_IN2, 0);
    pcf_set_bit(P_IN3, 0);
    pcf_set_bit(P_IN4, 0);
}

void RIGHT(void)
{
    pcf_set_bit(P_IN1, 0);
    pcf_set_bit(P_IN2, 0);
    pcf_set_bit(P_IN3, 1);
    pcf_set_bit(P_IN4, 0);
}

void BACK(void)
{
    pcf_set_bit(P_IN1, 0);
    pcf_set_bit(P_IN2, 1);
    pcf_set_bit(P_IN3, 0);
    pcf_set_bit(P_IN4, 1);
}

void TANKRIGHT(void)
{
    pcf_set_bit(P_IN1, 0);
    pcf_set_bit(P_IN2, 1);
    pcf_set_bit(P_IN3, 1);
    pcf_set_bit(P_IN4, 0);
}

void TANKLEFT(void)
{
    pcf_set_bit(P_IN1, 1);
    pcf_set_bit(P_IN2, 0);
    pcf_set_bit(P_IN3, 0);
    pcf_set_bit(P_IN4, 1);
}

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
    }
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
