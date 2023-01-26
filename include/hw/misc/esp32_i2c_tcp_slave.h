/*
 */
#ifndef QEMU_ESP32_I2C_TCP_SLAVE_H
#define QEMU_ESP32_I2C_TCP_SLAVE_H

#include "hw/i2c/i2c.h"
#include "qom/object.h"

#define TYPE_ESP32_I2C_TCP "esp32_i2c_tcp"
OBJECT_DECLARE_SIMPLE_TYPE(ESP32_I2C_TCP_State, ESP32_I2C_TCP)

/**
 */
struct ESP32_I2C_TCP_State {
    /*< private >*/
    I2CSlave i2c;
};

#endif
