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
#include "iot_gpio.h"
#include "iot_gpio_ex.h"
#include "ohos_init.h"
#include "car.h"
#include "car_wifi.h"
#include "car_websocket.h"

/* ---------- 方向引脚 ---------- */
#define PIN_IN1  IOT_IO_NAME_GPIO_1
#define PIN_IN2  IOT_IO_NAME_GPIO_4
#define PIN_IN3  IOT_IO_NAME_GPIO_14
#define PIN_IN4  IOT_IO_NAME_GPIO_3

#define FUNC_IN1  IOT_IO_FUNC_GPIO_1_GPIO
#define FUNC_IN2  IOT_IO_FUNC_GPIO_4_GPIO
#define FUNC_IN3  IOT_IO_FUNC_GPIO_14_GPIO
#define FUNC_IN4  IOT_IO_FUNC_GPIO_3_GPIO

/* ==================================================================
 * 方向控制
 * ================================================================== */

void STOP(void)
{
    IoTGpioSetOutputVal(PIN_IN1, 0);
    IoTGpioSetOutputVal(PIN_IN2, 0);
    IoTGpioSetOutputVal(PIN_IN3, 0);
    IoTGpioSetOutputVal(PIN_IN4, 0);
}

void FORWARD(void)
{
    IoTGpioSetOutputVal(PIN_IN1, 1);
    IoTGpioSetOutputVal(PIN_IN2, 0);
    IoTGpioSetOutputVal(PIN_IN3, 1);
    IoTGpioSetOutputVal(PIN_IN4, 0);
}

void LEFT(void)
{
    IoTGpioSetOutputVal(PIN_IN1, 1);
    IoTGpioSetOutputVal(PIN_IN2, 0);
    IoTGpioSetOutputVal(PIN_IN3, 0);
    IoTGpioSetOutputVal(PIN_IN4, 0);
}

void RIGHT(void)
{
    IoTGpioSetOutputVal(PIN_IN1, 0);
    IoTGpioSetOutputVal(PIN_IN2, 0);
    IoTGpioSetOutputVal(PIN_IN3, 1);
    IoTGpioSetOutputVal(PIN_IN4, 0);
}

void BACK(void)
{
    IoTGpioSetOutputVal(PIN_IN1, 0);
    IoTGpioSetOutputVal(PIN_IN2, 1);
    IoTGpioSetOutputVal(PIN_IN3, 0);
    IoTGpioSetOutputVal(PIN_IN4, 1);
}

void TANKRIGHT(void)
{
    IoTGpioSetOutputVal(PIN_IN1, 0);
    IoTGpioSetOutputVal(PIN_IN2, 1);
    IoTGpioSetOutputVal(PIN_IN3, 1);
    IoTGpioSetOutputVal(PIN_IN4, 0);
}

void TANKLEFT(void)
{
    IoTGpioSetOutputVal(PIN_IN1, 1);
    IoTGpioSetOutputVal(PIN_IN2, 0);
    IoTGpioSetOutputVal(PIN_IN3, 0);
    IoTGpioSetOutputVal(PIN_IN4, 1);
}

/* ==================================================================
 * 遥控指令解析
 * ================================================================== */

void car_execute_command(const char *dir, int speed)
{
    if (dir == NULL) return;

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
        printf("[CAR] Unknown command: %s:%d\r\n", dir, speed);
    }
}

/* ==================================================================
 * 主控制线程
 * ================================================================== */

void car_main(void *arg)
{
    (void)arg;

    IoTGpioInit(PIN_IN1);
    IoTGpioInit(PIN_IN2);
    IoTGpioInit(PIN_IN3);
    IoTGpioInit(PIN_IN4);

    IoSetFunc(PIN_IN1, FUNC_IN1);
    IoSetFunc(PIN_IN2, FUNC_IN2);
    IoSetFunc(PIN_IN3, FUNC_IN3);
    IoSetFunc(PIN_IN4, FUNC_IN4);

    IoTGpioSetDir(PIN_IN1, IOT_GPIO_DIR_OUT);
    IoTGpioSetDir(PIN_IN2, IOT_GPIO_DIR_OUT);
    IoTGpioSetDir(PIN_IN3, IOT_GPIO_DIR_OUT);
    IoTGpioSetDir(PIN_IN4, IOT_GPIO_DIR_OUT);

    STOP();
    printf("[CAR] GPIO initialized\r\n");

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
