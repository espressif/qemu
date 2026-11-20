/*
 * ESP8266 WiFi MAC register and TX descriptor regression test
 *
 * Copyright (c) 2026 AIT Austrian Institute of Technology
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define WIFI_BASE       0x3ff1fe00
#define INT_STATUS      (WIFI_BASE + 0x0e20)
#define INT_CLEAR       (WIFI_BASE + 0x0e24)
#define RX_HEAD         (WIFI_BASE + 0x0208)
#define RX_DONE         (WIFI_BASE + 0x0214)
#define TSF_LOW         (WIFI_BASE + 0x1248)
#define MAC_TIMER_CTRL  (WIFI_BASE + 0x12c8)
#define MAC_TIMER_LOW   (WIFI_BASE + 0x12cc)
#define MAC_TIMER_HIGH  (WIFI_BASE + 0x12d0)
#define TXQ0_KICK       (WIFI_BASE + 0x0fc4)
#define TXQ0_LENGTH     (WIFI_BASE + 0x0fc8)
#define TX_DESC         0x3fff0100
#define TX_FRAME        0x3fff0200
#define RX_DESC         0x3ffe9000
#define RX_DESC_NEXT    (RX_DESC + 12)
#define RX_BUFFER       0x3ffea000
#define RX_BUFFER_NEXT  0x3ffeb000

#define INT_TX_START    (1U << 18)
#define INT_TX_DONE     (1U << 19)
#define INT_RX_DONE     (1U << 8)
#define INT_MAC_TIMER   (1U << 26)

static void test_tx_completion(void)
{
    QTestState *qts = qtest_init("-machine esp8266 -nic none");
    uint8_t frame[24] = { 0 };

    qtest_writel(qts, TX_DESC + 4, TX_FRAME);
    qtest_memwrite(qts, TX_FRAME, frame, sizeof(frame));
    qtest_writel(qts, TXQ0_LENGTH, sizeof(frame));
    qtest_writel(qts, TXQ0_KICK, 0xc0000000 | (TX_DESC & 0x1ffff));

    g_assert_cmphex(qtest_readl(qts, INT_STATUS), ==,
                    INT_TX_START | INT_TX_DONE);
    qtest_writel(qts, INT_CLEAR, INT_TX_START | INT_TX_DONE);
    g_assert_cmphex(qtest_readl(qts, INT_STATUS), ==, 0);
    qtest_quit(qts);
}

static void test_rx_completion(void)
{
    QTestState *qts = qtest_init("-machine esp8266 -nic none");
    uint32_t word0;
    int64_t now;

    qtest_writel(qts, RX_DESC, 0x80000000 | 1600);
    qtest_writel(qts, RX_DESC + 4, RX_BUFFER);
    qtest_writel(qts, RX_DESC + 8, RX_DESC_NEXT);
    qtest_writel(qts, RX_DESC_NEXT, 0x80000000 | 1600);
    qtest_writel(qts, RX_DESC_NEXT + 4, RX_BUFFER_NEXT);
    qtest_writel(qts, RX_DESC_NEXT + 8, 0);
    qtest_writel(qts, RX_HEAD, RX_DESC);
    now = qtest_clock_step_next(qts);
    g_assert_cmpuint(qtest_readl(qts, TSF_LOW), ==, now / 1000);
    qtest_clock_step_next(qts);

    g_assert_cmphex(qtest_readl(qts, INT_STATUS), ==, INT_RX_DONE);
    g_assert_cmphex(qtest_readl(qts, RX_DONE), ==, RX_DESC);
    g_assert_cmphex(qtest_readb(qts, RX_BUFFER), ==, 56);
    word0 = qtest_readl(qts, RX_DESC);
    g_assert_cmphex(word0 & 0xc0000000, ==, 0x40000000);
    g_assert_cmpuint((word0 >> 12) & 0xfff, >, 12 + 24 + 4);
    qtest_quit(qts);
}

static void test_mac_timer(void)
{
    QTestState *qts = qtest_init("-machine esp8266 -nic none");

    qtest_writel(qts, MAC_TIMER_LOW, 1000);
    qtest_writel(qts, MAC_TIMER_HIGH, 0);
    qtest_writel(qts, MAC_TIMER_CTRL, 1U << 31);
    qtest_clock_step_next(qts);

    g_assert_cmphex(qtest_readl(qts, INT_STATUS), ==, INT_MAC_TIMER);
    g_assert_cmphex(qtest_readl(qts, MAC_TIMER_CTRL) & (1U << 31), ==, 0);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/esp8266/wifi/tx-completion", test_tx_completion);
    qtest_add_func("/esp8266/wifi/rx-completion", test_rx_completion);
    qtest_add_func("/esp8266/wifi/mac-timer", test_mac_timer);
    return g_test_run();
}
