/*
 * Runner and shared helpers for ESP C qtests.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "esp-qtest.h"

#define ESP_QTEST_MAX_SUITES 256

typedef struct {
    const char *name;
    EspQtestRegisterFn fn;
} EspQtestSuite;

static EspQtestSuite suites[ESP_QTEST_MAX_SUITES];
static size_t num_suites;

void esp_qtest_register_suite(const char *name, EspQtestRegisterFn fn)
{
    g_assert(num_suites < ARRAY_SIZE(suites));
    suites[num_suites].name = name;
    suites[num_suites].fn = fn;
    num_suites++;
}

static void (*machine_cb)(const char *mname);

static void filter_esp_machine(const char *mname)
{
    if (g_str_has_prefix(mname, "esp32")) {
        machine_cb(mname);
    }
}

void esp_qtest_for_every_machine(void (*cb)(const char *mname))
{
    machine_cb = cb;
    qtest_cb_for_every_machine(filter_esp_machine, false);
    machine_cb = NULL;
}

void esp_qtest_system_reset(QTestState *qts)
{
    qtest_qmp_assert_success(qts, "{'execute': 'system_reset'}");
    qtest_qmp_eventwait(qts, "RESET");
}

void esp_qtest_set_irq(QTestState *qts, int source, int level)
{
    qtest_set_irq_in(qts, "/machine/soc/intmatrix", NULL, source, level);
}

static int compare_suites(const void *a, const void *b)
{
    const EspQtestSuite *sa = a;
    const EspQtestSuite *sb = b;
    return strcmp(sa->name, sb->name);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_assert_cmpuint(num_suites, >, 0);

    qsort(suites, num_suites, sizeof(suites[0]), compare_suites);
    for (size_t i = 0; i < num_suites; i++) {
        suites[i].fn();
    }

    return g_test_run();
}
