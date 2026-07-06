/*
 * 小车控制 - 头文件
 * NL63pro (WS63) 开发板
 */

#ifndef CAR_H
#define CAR_H

void STOP(void);
void FORWARD(void);
void BACK(void);
void LEFT(void);
void RIGHT(void);
void TANKLEFT(void);
void TANKRIGHT(void);

/**
 * @brief  执行遥控指令
 * @param  dir   方向: "forward"/"backward"/"left"/"right"/"drift_l"/"drift_r"/"stop"
 * @param  speed 速度: 120/180/255（GPIO 模式下仅日志记录）
 */
void car_execute_command(const char *dir, int speed);

#endif /* CAR_H */
