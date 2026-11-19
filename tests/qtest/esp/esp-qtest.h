/*
 * Shared helpers and suite registration for ESP C qtests.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef TESTS_QTEST_ESP_QTEST_H
#define TESTS_QTEST_ESP_QTEST_H

#include "libqtest.h"
#include "hw/registerfields.h"

typedef void (*EspQtestRegisterFn)(void);

void esp_qtest_register_suite(const char *name, EspQtestRegisterFn fn);

/*
 * Register a test-suite setup function with the esp-qtest runner.
 * Test files under tests/qtest/esp/ matching *-test.c or *_test.c are
 * auto-discovered by meson and linked into esp-qtest.
 */
#define ESP_QTEST_SUITE(fn)                                             \
    static void __attribute__((constructor)) esp_qtest_ctor_##fn(void)  \
    {                                                                   \
        esp_qtest_register_suite(#fn, fn);                              \
    }

static inline uint32_t esp_reg_readl(QTestState *qts, uint32_t base,
                                     uint32_t offset)
{
    return qtest_readl(qts, base + offset);
}

static inline void esp_reg_writel(QTestState *qts, uint32_t base,
                                  uint32_t offset, uint32_t val)
{
    qtest_writel(qts, base + offset, val);
}

/* Invoke cb for every esp32* machine available in the current QEMU binary. */
void esp_qtest_for_every_machine(void (*cb)(const char *mname));

/* Trigger a QMP system_reset and wait for the RESET event. */
void esp_qtest_system_reset(QTestState *qts);

/* Drive an input line on /machine/soc/intmatrix. */
void esp_qtest_set_irq(QTestState *qts, int source, int level);

#endif
