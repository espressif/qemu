/*
 * QTest verifying that every ESP machine initializes cleanly under
 * -accel qtest and advances virtual time with qtest_clock_step().
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "esp-qtest.h"

static void test_machine_init(const void *data)
{
    const char *machine = data;
    QTestState *qts = qtest_initf("-machine %s", machine);

    g_assert_cmpint(qtest_clock_step(qts, 10 * SCALE_MS), ==, 10 * SCALE_MS);
    qtest_quit(qts);
}

static void add_esp_machine_test(const char *mname)
{
    g_autofree char *path =
        g_strdup_printf("/esp/machine/%s/qtest-init", mname);
    qtest_add_data_func_full(path, g_strdup(mname), test_machine_init, g_free);
}

static void register_machine_tests(void)
{
    esp_qtest_for_every_machine(add_esp_machine_test);
}

ESP_QTEST_SUITE(register_machine_tests)
