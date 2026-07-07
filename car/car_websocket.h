/*
 * 轻量 WebSocket Server - 头文件
 * NL63pro (WS63) 开发板
 *
 * 基于 lwIP TCP socket 实现 RFC 6455 WebSocket 协议
 * 单客户端模型：同一时间只允许一个微信小程序连接
 */

#ifndef CAR_WEBSOCKET_H
#define CAR_WEBSOCKET_H

/**
 * @brief  启动 WebSocket Server（阻塞式，无限循环）
 * @param  port  监听端口号（推荐 8080）
 * @return 仅在严重错误时返回 -1
 *
 * 收到 WebSocket 文本帧后，解析 "direction:speed" 格式的指令，
 * 调用 car_execute_command() 执行电机动作。
 * 客户端断开后自动回到 accept 等待新连接。
 */
int car_websocket_server_start(int port);

/**
 * @brief  向当前连接的客户端发送文本帧（供超声波等模块调用）
 * @param  msg  要发送的文本（null-terminated）
 * @return 0=成功, -1=无客户端或发送失败
 */
int car_websocket_send(const char *msg);

#endif /* CAR_WEBSOCKET_H */
