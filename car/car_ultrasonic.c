/*
 * 四路超声波 HC-SR04 — PCF8575 I2C 测距
 *
 * Trig: P4 (4路并联), Echo: P5=前 P6=后 P7=左 P8=右
 * 一次 IoTI2cRead 返回 16 位，四个 Echo 同时读到
 */

#include "car_ultrasonic.h"
#include <stdio.h>
#include "iot_i2c.h"

#define PCF8575_ADDR  0x20
#define I2C_BUS       1

#define P_TRIG   4
#define P_ECHO_F 5
#define P_ECHO_B 6
#define P_ECHO_L 7
#define P_ECHO_R 17
#define M(p)     (1U << (p))
#define M_ECHO   (M(P_ECHO_F) | M(P_ECHO_B) | M(P_ECHO_L) | M(P_ECHO_R))

static unsigned short g_echo_mask = 0;

/* ------- PCF8575 读写 ------- */

static int pcf_write(unsigned short val)
{
    unsigned char d[2] = { val & 0xFF, (val >> 8) & 0xFF };
    return (IoTI2cWrite(I2C_BUS, PCF8575_ADDR, d, 2) == 0) ? 0 : -1;
}

static unsigned short pcf_read(void)
{
    unsigned char d[2] = {0, 0};
    if (IoTI2cRead(I2C_BUS, PCF8575_ADDR, d, 2) != 0) return 0;
    return d[0] | ((unsigned short)d[1] << 8);
}

/* ------- 初始化 ------- */

void ultrasonic_init(void)
{
    /* Echo 引脚设输入模式 (写 1 = 高阻输入) */
    g_echo_mask = M_ECHO;
    pcf_write(M_ECHO);
    printf("[SONAR] ready (P4=Trig, Echo P5/P6/P7/P17)\r\n");
}

/* ------- 四路测距 ------- */

void ultrasonic_get_all(int dist[4])
{
    /* 1. 触发: P4=HIGH, 延时 50us, P4=LOW */
    pcf_write(g_echo_mask | M(P_TRIG));
    for (volatile int i = 0; i < 200; i++) { __asm__ __volatile__("nop"); }
    pcf_write(g_echo_mask);

    /* 2. 轮询 Echo, 用计数代替微秒计时 (每轮 ~200us I2C 开销) */
    int start[4] = {-1, -1, -1, -1};
    int end[4]   = {0, 0, 0, 0};
    int tick = 0;
    int timeout = 200;  /* 200 * 200us ≈ 40ms 超时 */

    while (tick < timeout) {
        unsigned short v = pcf_read();
        int f = (v & M(P_ECHO_F)) ? 1 : 0;
        int b = (v & M(P_ECHO_B)) ? 1 : 0;
        int l = (v & M(P_ECHO_L)) ? 1 : 0;
        int r = (v & M(P_ECHO_R)) ? 1 : 0;
        int bits[4] = {f, b, l, r};

        for (int i = 0; i < 4; i++) {
            if (bits[i]) {
                if (start[i] < 0) start[i] = tick;
            } else {
                if (start[i] >= 0 && end[i] == 0) end[i] = tick;
            }
        }
        tick++;
    }

    /* 3. 计算距离: 时间(us) = ticks * 200, 距离(cm) = 时间 / 58 */
    for (int i = 0; i < 4; i++) {
        if (start[i] >= 0 && end[i] > start[i]) {
            int us = (end[i] - start[i]) * 200;
            dist[i] = us / 58;
            if (dist[i] < 1) dist[i] = 1;
        } else {
            dist[i] = 0;
        }
    }
}
