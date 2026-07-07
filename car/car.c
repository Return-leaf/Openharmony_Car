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
#include "iot_i2c.h"
#include "iot_pwm.h"
#include "ohos_init.h"
#include "car.h"
#include "car_ultrasonic.h"
#include "car_wifi.h"
#include "car_websocket.h"

/* ---------- PCF8575 I2C (GPIO15=SDA, GPIO16=SCL, bus=1) ---------- */
#define PCF8575_ADDR  0x20
#define I2C_BUS       1

/* PCF8575 位映射: P0=IN1 P1=IN2 P2=IN3 P3=IN4 P4=ENA P5=ENB */
#define P_IN1  0
#define P_IN2  1
#define P_IN3  2
#define P_IN4  3
#define P_ENA  4
#define P_ENB  5
#define M(p)   (1 << (p))

static int pcf8575_write(unsigned short val)
{
    unsigned char d[2] = { val & 0xFF, (val >> 8) & 0xFF };
    unsigned int ret = IoTI2cWrite(I2C_BUS, PCF8575_ADDR, d, 2);
    if (ret != 0) printf("[PCF] I2C err=%u\r\n", ret);
    return (ret == 0) ? 0 : -1;
}

/* ---------- PWM 调速: GPIO1(PWM1,ch1,grp0) + GPIO10(PWM2,ch2,grp1) ---------- */
#define PWM_R  1   /* 右轮 */
#define PWM_L  2   /* 左轮 */
#define PWM_F  2000

static int g_pwm_ok = 0;

static void pwm_lazy_init(void)
{
    if (g_pwm_ok) return;
    IoTGpioInit(IOT_IO_NAME_GPIO_1);
    IoSetFunc(IOT_IO_NAME_GPIO_1, IOT_IO_FUNC_GPIO_1_PWM1_OUT);
    IoTGpioInit(IOT_IO_NAME_GPIO_10);
    IoSetFunc(IOT_IO_NAME_GPIO_10, IOT_IO_FUNC_GPIO_10_PWM2_OUT);
    if (IoTPwmInit(PWM_R) != 0 || IoTPwmInit(PWM_L) != 0) {
        printf("[CAR] PWM init fail!\r\n");
        return;
    }
    g_pwm_ok = 1;
    printf("[CAR] PWM ready (ch1+ch2)\r\n");
}

static unsigned short speed_to_duty(int speed)
{
    if (speed <= 0) return 0;
    if (speed >= 255) return 100;
    return (unsigned short)((speed * 100) / 255);
}

static void car_set_speed(int speed)
{
    unsigned short duty = speed_to_duty(speed);
    printf("[CAR] Speed=%d -> duty=%u%%\r\n", speed, (unsigned)duty);
    if (duty > 0) {
        pwm_lazy_init();
        if (g_pwm_ok) { IoTPwmStart(PWM_R, duty, PWM_F); IoTPwmStart(PWM_L, duty, PWM_F); }
    } else {
        if (g_pwm_ok) { IoTPwmStop(PWM_R); IoTPwmStop(PWM_L); }
    }
}

static unsigned short g_dir = 0;

static void car_flush(void)
{
    if (pcf8575_write(g_dir) != 0) {
        pcf8575_write(g_dir);
    }
}

/* ==================================================================
 * 方向控制（只设内存位，car_flush 统一写 I2C）
 * ================================================================== */

void STOP(void)       { g_dir = 0; }
void FORWARD(void)    { g_dir = M(P_IN1) | M(P_IN3); }
void BACK(void)       { g_dir = M(P_IN2) | M(P_IN4); }
void LEFT(void)       { g_dir = M(P_IN1); }
void RIGHT(void)      { g_dir = M(P_IN3); }
void TANKLEFT(void)   { g_dir = M(P_IN1) | M(P_IN4); }
void TANKRIGHT(void)  { g_dir = M(P_IN2) | M(P_IN3); }

/* ==================================================================
 * 遥控指令解析
 * ================================================================== */

void car_execute_command(const char *dir, int speed)
{
    if (dir == NULL) return;

    if (strcmp(dir, "stop") == 0)         { car_set_speed(0);   STOP(); }
    else if (strcmp(dir, "forward") == 0)  { car_set_speed(speed); FORWARD(); }
    else if (strcmp(dir, "backward") == 0) { car_set_speed(speed); BACK(); }
    else if (strcmp(dir, "left") == 0)     { car_set_speed(speed); LEFT(); }
    else if (strcmp(dir, "right") == 0)    { car_set_speed(speed); RIGHT(); }
    else if (strcmp(dir, "drift_l") == 0)  { car_set_speed(255); TANKLEFT(); }
    else if (strcmp(dir, "drift_r") == 0)  { car_set_speed(255); TANKRIGHT(); }
    else { printf("[CAR] Unknown: %s:%d\r\n", dir, speed); return; }
    car_flush();
}

/* ==================================================================
 * 主控制线程
 * ================================================================== */

void car_main(void *arg)
{
    (void)arg;

    /* I2C 引脚配置：GPIO15=SDA, GPIO16=SCL（参考 OLED/AHT30 示例） */
    IoTGpioInit(IOT_IO_NAME_GPIO_15);
    IoTGpioInit(IOT_IO_NAME_GPIO_16);
    IoSetFunc(IOT_IO_NAME_GPIO_15, IOT_IO_FUNC_GPIO_15_I2C1_SDA);
    IoSetFunc(IOT_IO_NAME_GPIO_16, IOT_IO_FUNC_GPIO_16_I2C1_SCL);
    IoTI2cInit(I2C_BUS, 100000);

    /* 测试 PCF8575 是否响应 */
    unsigned char t[2] = {0, 0};
    if (IoTI2cWrite(I2C_BUS, PCF8575_ADDR, t, 2) == 0) {
        printf("[CAR] PCF8575 OK (bus=%d, addr=0x%02X)\r\n", I2C_BUS, PCF8575_ADDR);
    } else {
        printf("[CAR] PCF8575 no response! Check wiring\r\n");
    }

    ultrasonic_init();

    if (car_wifi_connect() != 0) {
        printf("[CAR] WiFi connect failed!\r\n");
        return;
    }
    printf("[CAR] WiFi connected, IP=%s\r\n", car_wifi_get_ip_str());

    car_websocket_server_start(8080);
}

/* 超声波测距 + JSON 上报线程 (每 500ms 一次) */
static void car_sonar_thread(void *arg)
{
    (void)arg;
    osDelay(300);  /* 等 WiFi+WS 就绪 */

    while (1) {
        int d[4];
        ultrasonic_get_all(d);
        char buf[128];
        snprintf(buf, sizeof(buf),
            "{\"type\":\"sonar\",\"front\":%d,\"back\":%d,\"left\":%d,\"right\":%d}",
            d[0], d[1], d[2], d[3]);
        printf("[SONAR] %s\r\n", buf);
        car_websocket_send(buf);
        osDelay(50);  /* 500ms */
    }
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

    osThreadAttr_t attr2 = {0};
    attr2.name       = "car_sonar";
    attr2.stack_size = 4096;
    attr2.priority   = osPriorityLow;
    if (osThreadNew(car_sonar_thread, NULL, &attr2) == NULL) {
        printf("[CAR] Failed to create sonar thread!\r\n");
    }
}

APP_FEATURE_INIT(car_example);
