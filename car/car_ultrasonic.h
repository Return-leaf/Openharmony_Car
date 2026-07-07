/*
 * 四路超声波 HC-SR04 — PCF8575 I2C 测距
 */

#ifndef CAR_ULTRASONIC_H
#define CAR_ULTRASONIC_H

/* 初始化（必须在 PCF8575 I2C 已就绪后调用） */
void ultrasonic_init(void);

/* 触发一次测量 + 读取四个方向距离 (cm)，0=无障碍/超时 */
void ultrasonic_get_all(int dist[4]);

/* 传感器索引 */
#define SONAR_FRONT  0
#define SONAR_BACK   1
#define SONAR_LEFT   2
#define SONAR_RIGHT  3

#endif
