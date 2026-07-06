/*
 * 小车 WiFi STA 连接 - 实现
 * NL63pro (WS63) 开发板
 *
 * 参考 SDK 示例: device/soc/hisilicon/ws63v100/sdk/application/samples/wifi/sta_sample/sta_sample.c
 *
 * 流程：注册事件回调 → 等待 WiFi 初始化 → 开启 STA → 连接热点 → DHCP 获取 IP
 * 失败自动重试（连接断开时回调会重置状态机）
 */

#include "car_wifi.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "wifi_device.h"
#include "wifi_device_config.h"
#include "wifi_event.h"
#include "wifi_linked_info.h"
#include "lwip/netifapi.h"
#include "lwip/netif.h"
#include "cmsis_os2.h"

/* ========== 配置项（编译前修改） ========== */
#define CAR_WIFI_SSID       "OPPO"    /* 手机热点名称 */
#define CAR_WIFI_PASSWORD   "1379258046"          /* 手机热点密码 */
#define CAR_WIFI_SEC_TYPE   WIFI_SEC_TYPE_WPA2PSK  /* 安全类型 */

#define CAR_WIFI_LOG        "[CAR_WIFI]"

/* ========== 状态机 ========== */
enum wifi_state {
    WIFI_STATE_INIT = 0,        /* 初始态 */
    WIFI_STATE_SCANNING,        /* 扫描中 */
    WIFI_STATE_SCAN_DONE,       /* 扫描完成 */
    WIFI_STATE_FOUND_TARGET,    /* 已匹配目标AP */
    WIFI_STATE_CONNECTING,      /* 连接中 */
    WIFI_STATE_CONNECTED,       /* 关联成功 */
    WIFI_STATE_GET_IP,          /* 获取IP中 */
    WIFI_STATE_DONE,            /* 完成 */
};

static volatile int g_wifi_state = WIFI_STATE_INIT;
static char g_ip_str[16] = "0.0.0.0";

/* ========== 事件回调（与 sta_sample.c 完全一致） ========== */

static void on_scan_state_changed(int32_t state, int32_t size)
{
    (void)state;
    (void)size;
    printf("%s::Scan done!\r\n", CAR_WIFI_LOG);
    g_wifi_state = WIFI_STATE_SCAN_DONE;
}

static void on_connection_changed(int32_t state, const wifi_linked_info_stru *info,
                                  int32_t reason_code)
{
    (void)info;
    (void)reason_code;

    if (state == 0) {  /* WIFI_NOT_AVALLIABLE / WIFI_DISCONNECTED */
        printf("%s::Connect fail, retry...\r\n", CAR_WIFI_LOG);
        g_wifi_state = WIFI_STATE_INIT;  /* 触发重连 */
    } else {
        printf("%s::Connect success!\r\n", CAR_WIFI_LOG);
        g_wifi_state = WIFI_STATE_CONNECTED;
    }
}

static wifi_event_stru g_event_cb = {
    .wifi_event_connection_changed = on_connection_changed,
    .wifi_event_scan_state_changed = on_scan_state_changed,
};

/* ========== 辅助：获取 IP 地址字符串 ========== */

static void save_ip_string(struct netif *netif_p)
{
    if (netif_p == NULL) return;
    const ip4_addr_t *ip4 = netif_ip4_addr(netif_p);
    snprintf(g_ip_str, sizeof(g_ip_str), "%d.%d.%d.%d",
             (int)ip4_addr1_16(ip4),
             (int)ip4_addr2_16(ip4),
             (int)ip4_addr3_16(ip4),
             (int)ip4_addr4_16(ip4));
    printf("%s::Got IP: %s\r\n", CAR_WIFI_LOG, g_ip_str);
}

/* ========== 主连接流程 ========== */

int car_wifi_connect(void)
{
    /* 1. 注册事件回调 */
    if (wifi_register_event_cb(&g_event_cb) != 0) {
        printf("%s::Event callback register fail!\r\n", CAR_WIFI_LOG);
        return -1;
    }
    printf("%s::Event callback registered\r\n", CAR_WIFI_LOG);

    /* 2. 等待 WiFi 子系统初始化完成 */
    while (wifi_is_wifi_inited() == 0) {
        osDelay(10);  /* 100ms */
    }
    printf("%s::WiFi init done\r\n", CAR_WIFI_LOG);

    /* 3. 开启 STA 模式 */
    if (wifi_sta_enable() != 0) {
        printf("%s::STA enable fail!\r\n", CAR_WIFI_LOG);
        return -1;
    }
    printf("%s::STA enabled\r\n", CAR_WIFI_LOG);

    /* 4. 状态机循环：扫描 → 连接 → DHCP */
    wifi_sta_config_stru config;
    struct netif *netif_p = NULL;
    int dhcp_wait_count = 0;

    while (1) {
        switch (g_wifi_state) {
        case WIFI_STATE_INIT:
            /* 发起扫描 */
            g_wifi_state = WIFI_STATE_SCANNING;
            printf("%s::Scan start!\r\n", CAR_WIFI_LOG);
            if (wifi_sta_scan() != 0) {
                printf("%s::Scan fail!\r\n", CAR_WIFI_LOG);
                g_wifi_state = WIFI_STATE_INIT;
            }
            break;

        case WIFI_STATE_SCAN_DONE:
            /* 直接使用预定义的 SSID/密码连接，不遍历扫描结果 */
            g_wifi_state = WIFI_STATE_FOUND_TARGET;
            break;

        case WIFI_STATE_FOUND_TARGET:
            /* 填充连接参数 */
            (void)memset(&config, 0, sizeof(config));
            strncpy((char*)config.ssid, CAR_WIFI_SSID, WIFI_MAX_SSID_LEN - 1);
            strncpy((char*)config.pre_shared_key, CAR_WIFI_PASSWORD, WIFI_MAX_KEY_LEN - 1);
            config.security_type = CAR_WIFI_SEC_TYPE;
            config.ip_type = DHCP;
            g_wifi_state = WIFI_STATE_CONNECTING;
            printf("%s::Connecting to %s...\r\n", CAR_WIFI_LOG, CAR_WIFI_SSID);

            if (wifi_sta_connect(&config) != 0) {
                printf("%s::Connect request fail!\r\n", CAR_WIFI_LOG);
                g_wifi_state = WIFI_STATE_INIT;
            }
            break;

        case WIFI_STATE_CONNECTING:
            /* 等待连接回调更新状态，不做操作 */
            break;

        case WIFI_STATE_CONNECTED:
            /* 关联成功，启动 DHCP */
            g_wifi_state = WIFI_STATE_GET_IP;
            netif_p = netifapi_netif_find("wlan0");
            if (netif_p == NULL || netifapi_dhcp_start(netif_p) != 0) {
                printf("%s::Find netif or DHCP start fail!\r\n", CAR_WIFI_LOG);
                g_wifi_state = WIFI_STATE_INIT;
                continue;
            }
            printf("%s::DHCP started\r\n", CAR_WIFI_LOG);
            dhcp_wait_count = 0;
            break;

        case WIFI_STATE_GET_IP:
            /* 轮询直到获取 IP */
            if (netif_p != NULL &&
                !ip_addr_isany(&netif_p->ip_addr)) {
                save_ip_string(netif_p);
                g_wifi_state = WIFI_STATE_DONE;
            } else if (++dhcp_wait_count > 300) {
                /* DHCP 超时（300 * 100ms = 30秒），重试 */
                printf("%s::DHCP timeout, retry...\r\n", CAR_WIFI_LOG);
                g_wifi_state = WIFI_STATE_INIT;
            }
            break;

        case WIFI_STATE_DONE:
            return 0;  /* 成功 */

        default:
            break;
        }
        osDelay(10);  /* 100ms per tick */
    }
}

/* ========== 获取 IP ========== */

const char* car_wifi_get_ip_str(void)
{
    return g_ip_str;
}
