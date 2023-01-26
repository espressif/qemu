#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"
#include "hw/misc/esp32_i2c_tcp_slave.h"
#include "qemu/module.h"

#include "qapi/qmp/json-writer.h"
#include "chardev/char-fe.h"
#include "io/channel-socket.h"
#include "chardev/char-io.h"
#include "chardev/char-socket.h"
#include "qapi/error.h"

/*
Description:
    To allow an end-user to emulate a I2C slave device  which is not supported by QEMU,
    a new QEMU I2C slave device was created that passes I2C operations over TCP to the host
    where a TCP client resides which implements a logic of I2C emulated device.


Architecture: 
     ---------------------------
     | Qemu                    |
     |                         |         -----------------------
     |  ESP32 Firmware write   |         |                     |
     |  to I2C Slave           |         | I2C Slave Emulation |
     |                         |         |                     |
     |  -----------------------&---------&----                 |
     |  | I2C Slave at 0x7F    &   tcp   &     recv msg        |
     |  -----------------------&---------&---- process msg     | 
     |                         |         |     send respone    |
     |                         |         |                     |
     |                         |         |                     |
     ---------------------------         |----------------------


Syntax & protocol:
     Qemu I2C Slave sends a msg in following format: BBB\r\n
     where each 'B' represents a single byte 0-255
     QEMU I2C Slave expects a respone message in the same format as fast as possible
     Example:
        req: 0x45 0x01 0x00 \r\n
       resp: 0x45 0x01 0x00 \r\n

     The format BBB\r\n
       first 'B' is a message type
       second 'B' is a data value
       third 'B' is an error value (not used at the moment)

     There are three types of message
       'E' or 0x45 - Event: 
       'S' or 0x53 - Send: byte sent to emulated I2C Slave
       'R' or 0x52 - Recv: byte to be received by I2C Master


     'E' message
       second byte is an event type:
        0x0: I2C_START_RECV
        0x1: I2C_START_SEND
        0x2: I2C_START_SEND_ASYNC
        0x3: I2C_FINISH
        0x4: I2C_NACK    

       Example:
           0x45 0x01 0x00  - start send
           0x45 0x03 0x00  - finish

       In case of 'E' message, a response is the same as a request message

     'S' message
       second byte is a byte transmitted from I2C Master to I2C slave device
       the byte to by processed by I2C Slave Device

       Example:
           0x53 0x20 0x00

       In case of 'S' message, a response is the same as a request message

     'R' message
       the I2C Master expect a byte from the emulated i2c slave device
       An app that emulates I2C slave device has to modify the second byte of the request message and send it back as a response.
       Example:
            req: 0x52 0x00 0x00
           resp: 0x52 0x11 0x00


Examples of Transmission:
    1) i2cset -c 0x7F -r 0x20 0x11 0x22 0x33 0x44 0x55
         req: 45 01 00
        resp: 45 01 00

         req: 53 20 00
        resp: 53 20 00

         req: 53 11 00
        resp: 53 11 00

         req: 53 22 00
        resp: 53 22 00

         req: 53 33 00
        resp: 53 33 00

         req: 53 44 00
        resp: 53 44 00

         req: 53 55 00
        resp: 53 55 00

         req: 45 03 00
        resp: 45 03 00

    2) i2cget -c 0x7F -r 0x20 -l 0x03
         req: 45 01 00
        resp: 45 01 00

         req: 53 20 00
        resp: 53 20 00

         req: 45 03 00
        resp: 45 03 00

         req: 45 00 00
        resp: 45 00 00

         req: 52 00 00
        resp: 52 11 00

         req: 52 00 00
        resp: 52 22 00

         req: 52 00 00
        resp: 52 33 00

         req: 45 03 00
        resp: 45 03 00



To start i2c.socket server, set QEMU param:
    -chardev socket,port=16001,wait=no,host=localhost,server=on,ipv4=on,id=i2c.socket


Simple demo I2C Slave Emulation in Python:
    tests/i2c-tcp-demo/i2c-tcp-demo.py

Limitations:
    - there is no recv timeout which may lead to qemu hang
*/


#define CHARDEV_NAME "i2c.socket"

static Chardev *chardev;
static CharBackend char_backend;
static bool chardev_open;

typedef struct
{
    uint8_t id;
    uint8_t byte;
    uint8_t err;
} packet;

static int chr_can_receive(void *opaque)
{
    return CHR_READ_BUF_LEN;
}

static void chr_event(void *opaque, QEMUChrEvent event)
{
    switch (event)
    {
    case CHR_EVENT_OPENED:
        qemu_log("connected\n");
        chardev_open = true;
        break;
    case CHR_EVENT_CLOSED:
        qemu_log("disconnected\n");
        chardev_open = false;
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }
}

static void send_packet(packet *p)
{
    static const char *PACKET_FMT = "%c%c%c\r\n";
    static char buff[32];

    // encode
    int len = snprintf(buff, sizeof(buff), PACKET_FMT, p->id, p->byte, p->err);

    // send
    qemu_chr_fe_write_all(&char_backend, (uint8_t *)buff, len);

    // receive
    qemu_chr_fe_read_all(&char_backend, (uint8_t *)buff, len);

    // decode
    sscanf(buff, PACKET_FMT, &p->id, &p->byte, &p->err);
}

static uint8_t slave_rx(I2CSlave *i2c)
{
    packet p = {.id = 'R',
                .byte = 0,
                .err = 0};

    send_packet(&p);

    return p.byte;
}

static int slave_tx(I2CSlave *i2c, uint8_t data)
{
    packet p = {.id = 'S',
                .byte = data,
                .err = 0};

    send_packet(&p);

    return 0;
}

static int slave_event(I2CSlave *i2c, enum i2c_event event)
{
    packet p = {.id = 'E',
                .byte = event,
                .err = 0};

    send_packet(&p);

    return 0;
}

static void slave_realize(DeviceState *dev, Error **errp)
{
}

static void slave_init(Object *obj)
{
    Error *err = NULL;
    chardev = qemu_chr_find(CHARDEV_NAME);
    if (!chardev)
    {
        error_report("chardev '%s' not found", CHARDEV_NAME);
        return;
    }

    if (!qemu_chr_fe_init(&char_backend, chardev, &err))
    {
        error_report_err(err);
        return;
    }

    qemu_chr_fe_set_handlers(&char_backend, chr_can_receive, NULL, chr_event, NULL, NULL, NULL, true);
}

static void slave_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->realize = slave_realize;
    k->event = slave_event;
    k->recv = slave_rx;
    k->send = slave_tx;
}

static const TypeInfo esp32_i2c_tcp_info = {
    .name = TYPE_ESP32_I2C_TCP,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(ESP32_I2C_TCP_State),
    .instance_init = slave_init,
    .class_init = slave_class_init,
};

static void esp32_i2c_tcp_type_init(void)
{
    type_register_static(&esp32_i2c_tcp_info);
}

type_init(esp32_i2c_tcp_type_init);
