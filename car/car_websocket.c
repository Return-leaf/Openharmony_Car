/*
 * 轻量 WebSocket Server - 实现
 * NL63pro (WS63) 开发板
 *
 * RFC 6455 WebSocket 协议子集：
 *   - 握手：HTTP Upgrade + Sec-WebSocket-Key/Accept (SHA1 + base64)
 *   - 数据帧：仅处理 TEXT/CLOSE/PING 帧，client→server 必须带 mask
 *   - 单客户端：listen backlog = 1
 *
 * 依赖：
 *   lwIP socket API (lwip/sockets.h)
 *   mbedtls SHA1   (mbedtls/sha1.h)
 *   mbedtls base64 (mbedtls/base64.h)
 */

#include "car_websocket.h"
#include "car.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "lwip/sockets.h"
#include "lwip/netif.h"
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"
#include "cmsis_os2.h"

#define CAR_WS_LOG          "[CAR_WS]"

/* RFC 6455 定义的 WebSocket GUID */
#define WS_MAGIC_GUID       "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_GUID_LEN         36

/* WebSocket 帧操作码 */
#define WS_OPCODE_TEXT      0x1
#define WS_OPCODE_CLOSE     0x8
#define WS_OPCODE_PING      0x9
#define WS_OPCODE_PONG      0xA

/* 缓冲区大小 */
#define WS_RECV_BUF_SIZE    512
#define WS_HTTP_REQ_MAX     1024

static int g_client_fd = -1;  /* 当前连接的客户端 fd */
#define WS_PREBUF_SIZE      256   /* 握手后残留数据（如 PING 帧）的预读缓冲 */

/* 预读缓冲：握手阶段 lwip_recv 可能读走 HTTP 头之后的 WebSocket 帧数据
 * recv_exact 优先从这里取数据，避免帧解析器失同步 */
static uint8_t g_ws_prebuf[WS_PREBUF_SIZE];
static size_t   g_ws_prebuf_len = 0;

/* ==================================================================
 * 握手：计算 Sec-WebSocket-Accept
 * accept = base64( sha1( client_key + "258EAFA5-..." ) )
 * ================================================================== */

static int ws_compute_accept_key(const char *client_key, char *accept_out, size_t out_len)
{
    unsigned char sha1_hash[20];
    char combined[256];
    size_t key_len = strlen(client_key);
    size_t total_len = key_len + WS_GUID_LEN;

    if (total_len >= sizeof(combined)) {
        return -1;
    }

    memcpy(combined, client_key, key_len);
    memcpy(combined + key_len, WS_MAGIC_GUID, WS_GUID_LEN);

    /* SHA1 哈希 */
    if (mbedtls_sha1((const unsigned char*)combined, total_len, sha1_hash) != 0) {
        return -1;
    }

    /* Base64 编码 */
    size_t b64_len = 0;
    if (mbedtls_base64_encode((unsigned char*)accept_out, out_len, &b64_len,
                              sha1_hash, 20) != 0) {
        return -1;
    }
    accept_out[b64_len] = '\0';
    return 0;
}

/* ==================================================================
 * 握手：解析 HTTP Upgrade 请求并返回 101 响应
 * ================================================================== */

static int ws_handshake(int client_fd)
{
    char buf[WS_HTTP_REQ_MAX];
    int n = lwip_recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        printf("%s::Handshake recv fail (n=%d)\r\n", CAR_WS_LOG, n);
        return -1;
    }
    buf[n] = '\0';

    /* 验证是 GET 请求 */
    if (strncmp(buf, "GET ", 4) != 0) {
        printf("%s::Not a GET request\r\n", CAR_WS_LOG);
        return -1;
    }

    /* ---- 关键修复：查找 HTTP 头结束标记 \r\n\r\n，保存之后的数据 ---- */
    g_ws_prebuf_len = 0;
    char *hdr_end = strstr(buf, "\r\n\r\n");
    if (hdr_end != NULL) {
        int header_len = (int)(hdr_end - buf) + 4;  /* +4 for \r\n\r\n */
        int extra = n - header_len;
        if (extra > 0 && extra <= (int)sizeof(g_ws_prebuf)) {
            memcpy(g_ws_prebuf, buf + header_len, (size_t)extra);
            g_ws_prebuf_len = (size_t)extra;
            printf("%s::Saved %d bytes after HTTP headers\r\n", CAR_WS_LOG, extra);
        }
        /* 截断 buf 到 HTTP 头结束，防止后续 strstr 匹配到 WebSocket 帧数据 */
        buf[header_len] = '\0';
    }

    /* 查找 Sec-WebSocket-Key 头 */
    const char *key_tag = "Sec-WebSocket-Key:";
    char *key_start = strstr(buf, key_tag);
    if (key_start == NULL) {
        printf("%s::No Sec-WebSocket-Key header\r\n", CAR_WS_LOG);
        return -1;
    }
    key_start += strlen(key_tag);

    /* 跳过冒号后的空格 */
    while (*key_start == ' ') key_start++;

    /* 提取 key 值（到 \r 或 \n 为止） */
    char client_key[128];
    int i = 0;
    while (*key_start != '\r' && *key_start != '\n' && *key_start != '\0'
           && i < (int)(sizeof(client_key) - 1)) {
        client_key[i++] = *key_start++;
    }
    client_key[i] = '\0';

    printf("%s::Client key: %s\r\n", CAR_WS_LOG, client_key);

    /* 计算 Accept Key */
    char accept_key[64];
    if (ws_compute_accept_key(client_key, accept_key, sizeof(accept_key)) != 0) {
        printf("%s::Compute accept key fail\r\n", CAR_WS_LOG);
        return -1;
    }

    /* 构建并发送 HTTP 101 响应 */
    char response[256];
    int resp_len = snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n",
        accept_key);

    if (lwip_send(client_fd, response, resp_len, 0) != resp_len) {
        printf("%s::Handshake send fail\r\n", CAR_WS_LOG);
        return -1;
    }

    printf("%s::Handshake OK\r\n", CAR_WS_LOG);
    return 0;
}

/* ==================================================================
 * 辅助：精确读取 len 字节（处理 TCP 部分读取）
 * 标准 recv() 不保证一次返回 len 字节，必须循环读取
 * ================================================================== */

static int recv_exact(int fd, void *buf, size_t len)
{
    size_t total = 0;
    uint8_t *ptr = (uint8_t*)buf;

    /* 优先从预读缓冲区取数据（握手阶段可能吞掉的 WebSocket 帧） */
    if (g_ws_prebuf_len > 0) {
        size_t take = (len < g_ws_prebuf_len) ? len : g_ws_prebuf_len;
        memcpy(ptr, g_ws_prebuf, take);
        ptr += take;
        total += take;
        len  -= take;
        /* 移动预读缓冲区剩余数据 */
        if (take < g_ws_prebuf_len) {
            memmove(g_ws_prebuf, g_ws_prebuf + take, g_ws_prebuf_len - take);
        }
        g_ws_prebuf_len -= take;
    }

    /* 剩余数据从 socket 读 */
    while (total < len) {
        int n = lwip_recv(fd, ptr + total, (int)(len - total), 0);
        if (n <= 0) {
            printf("%s::recv_exact fail: fd=%d, ret=%d, total=%zu/%zu\r\n",
                   CAR_WS_LOG, fd, n, total, len + total);
            return -1;
        }
        total += (size_t)n;
    }
    return 0;
}

/* ==================================================================
 * 数据帧：接收并解掩码一个 WebSocket 帧
 *
 * 帧格式（client → server，MASK 必须为 1）：
 *   Byte 0:    FIN(1) RSV(3) OPCODE(4)
 *   Byte 1:    MASK(1) PAYLOAD_LEN(7)
 *   Bytes 2-3 或 2-9: 扩展长度 (PAYLOAD_LEN==126 或 127)
 *   Bytes +4:  4 字节 masking key
 *   剩余:      掩码后的 payload
 *
 * 所有读取使用 recv_exact() 确保不会因 TCP 部分读取而失同步
 * client_fd 用于收到 PING 时回复 PONG
 * ================================================================== */

static int ws_recv_frame(int client_fd, char *payload, size_t max_len)
{
    unsigned char hdr[2];
    if (recv_exact(client_fd, hdr, 2) != 0) return -1;

    int opcode  = hdr[0] & 0x0F;
    int masked  = (hdr[1] & 0x80) != 0;
    size_t plen = hdr[1] & 0x7F;

    /* 扩展长度 */
    if (plen == 126) {
        unsigned char ext[2];
        if (recv_exact(client_fd, ext, 2) != 0) return -1;
        plen = ((size_t)ext[0] << 8) | ext[1];
    } else if (plen == 127) {
        unsigned char ext[8];
        if (recv_exact(client_fd, ext, 8) != 0) return -1;
        plen = 0;
        for (int i = 0; i < 8; i++) {
            plen = (plen << 8) | ext[i];
        }
    }

    /* 读取 masking key */
    unsigned char mask_key[4] = {0, 0, 0, 0};
    if (masked) {
        if (recv_exact(client_fd, mask_key, 4) != 0) return -1;
    }

    /* 限制 payload 大小 */
    if (plen > max_len - 1) {
        plen = max_len - 1;
    }

    /* 精确读取 payload */
    if (plen > 0) {
        if (recv_exact(client_fd, payload, plen) != 0) return -1;
    }
    payload[plen] = '\0';

    /* 解掩码 */
    if (masked) {
        for (size_t i = 0; i < plen; i++) {
            payload[i] ^= mask_key[i % 4];
        }
    }

    /* 处理控制帧 */
    switch (opcode) {
    case WS_OPCODE_CLOSE:
        printf("%s::Client sent CLOSE\r\n", CAR_WS_LOG);
        return -1;

    case WS_OPCODE_PING:
        /* RFC 6455: 收到 PING 必须回复 PONG，否则微信客户端会断开连接 */
        {
            unsigned char pong[130];
            pong[0] = 0x8A;  /* FIN + PONG */
            if (plen <= 125) {
                pong[1] = (unsigned char)plen;
                if (plen > 0) {
                    memcpy(pong + 2, payload, plen);  /* echo payload */
                }
                lwip_send(client_fd, pong, 2 + (int)plen, 0);
            }
        }
        printf("%s::Got PING (len=%u), sent PONG\r\n", CAR_WS_LOG, (unsigned)plen);
        return 0;

    case WS_OPCODE_PONG:
        return 0;  /* 忽略 */

    case WS_OPCODE_TEXT:
        return (int)plen;  /* 返回文本长度 */

    default:
        printf("%s::Unknown opcode: 0x%x\r\n", CAR_WS_LOG, opcode);
        return 0;
    }
}

/* ==================================================================
 * 简易发送文本帧（用于将来可能的双向通信）
 * ================================================================== */

static int ws_send_text(int client_fd, const char *msg)
{
    size_t msg_len = strlen(msg);
    unsigned char frame[256];
    int idx = 0;

    /* FIN + TEXT opcode */
    frame[idx++] = 0x81;  /* FIN=1, RSV=0, OPCODE=TEXT(1) */

    /* Payload length（服务器→客户端，不加 mask） */
    if (msg_len <= 125) {
        frame[idx++] = (unsigned char)(msg_len & 0x7F);
    } else if (msg_len <= 0xFFFF) {
        frame[idx++] = 126;
        frame[idx++] = (unsigned char)((msg_len >> 8) & 0xFF);
        frame[idx++] = (unsigned char)(msg_len & 0xFF);
    }
    /* 小车指令很短，不会超过 0xFFFF，不处理 127 的情况 */

    memcpy(frame + idx, msg, msg_len);
    idx += msg_len;

    int sent = lwip_send(client_fd, frame, idx, 0);
    return (sent == idx) ? 0 : -1;
}

/* ==================================================================
 * 主服务器循环
 * ================================================================== */

int car_websocket_server_start(int port)
{
    int listen_fd;
    struct sockaddr_in addr;
    int opt = 1;

    /* 创建 TCP socket */
    listen_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        printf("%s::socket() fail\r\n", CAR_WS_LOG);
        return -1;
    }

    lwip_setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    (void)memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = lwip_htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;  /* 0.0.0.0 — 监听所有网络接口 */

    if (lwip_bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("%s::bind() fail\r\n", CAR_WS_LOG);
        lwip_close(listen_fd);
        return -1;
    }

    if (lwip_listen(listen_fd, 1) < 0) {
        printf("%s::listen() fail\r\n", CAR_WS_LOG);
        lwip_close(listen_fd);
        return -1;
    }

    printf("%s::Listening on port %d\r\n", CAR_WS_LOG, port);

    /* ===== 主循环：accept → handshake → recv loop → disconnect ===== */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        char payload[WS_RECV_BUF_SIZE];

        printf("%s::Waiting for client...\r\n", CAR_WS_LOG);

        int client_fd = lwip_accept(listen_fd,
                                     (struct sockaddr*)&client_addr,
                                     &client_len);
        if (client_fd < 0) {
            osDelay(50);  /* 500ms 后重试 */
            continue;
        }

        printf("%s::Client connected!\r\n", CAR_WS_LOG);
        g_client_fd = client_fd;

        /* WebSocket 握手 */
        if (ws_handshake(client_fd) != 0) {
            printf("%s::Handshake failed, closing\r\n", CAR_WS_LOG);
            g_client_fd = -1;
            lwip_close(client_fd);
            STOP();
            continue;
        }

        /* 接收循环 */
        while (1) {
            int len = ws_recv_frame(client_fd, payload, sizeof(payload));
            if (len < 0) {
                /* 客户端断开或错误 */
                break;
            }
            if (len == 0) {
                /* Ping/Pong 或空帧 */
                continue;
            }

            /* 解析 "direction:speed" 协议 */
            char *colon = strchr(payload, ':');
            if (colon != NULL) {
                *colon = '\0';
                const char *dir = payload;
                int speed = atoi(colon + 1);

                printf("%s::Cmd: %s:%d\r\n", CAR_WS_LOG, dir, speed);
                car_execute_command(dir, speed);
            } else {
                printf("%s::Bad frame: %s\r\n", CAR_WS_LOG, payload);
            }
        }

        printf("%s::Client disconnected\r\n", CAR_WS_LOG);
        g_client_fd = -1;
        lwip_close(client_fd);
        STOP();  /* 安全停车 */
    }

    lwip_close(listen_fd);
    return -1;
}

/* 供外部模块（超声波等）向客户端发送文本帧 */
int car_websocket_send(const char *msg)
{
    if (g_client_fd < 0 || msg == NULL) return -1;
    return ws_send_text(g_client_fd, msg);
}
