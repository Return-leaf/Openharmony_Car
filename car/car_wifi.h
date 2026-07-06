/*
 * 小车 WiFi STA 连接 - 头文件
 * NL63pro (WS63) 开发板
 *
 * 功能：连接开发板到手机热点（WiFi STA 模式），获取 IP 地址
 */

#ifndef CAR_WIFI_H
#define CAR_WIFI_H

/**
 * @brief  连接 WiFi 热点（阻塞式，成功返回前会一直重试）
 * @return 0=成功, -1=失败
 */
int car_wifi_connect(void);

/**
 * @brief  获取当前 WiFi STA 的 IP 地址字符串
 * @return IP 字符串（如 "192.168.43.100"），未连接时返回 "0.0.0.0"
 */
const char* car_wifi_get_ip_str(void);

#endif /* CAR_WIFI_H */
