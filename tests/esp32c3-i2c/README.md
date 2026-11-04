# ESP32-C3 I2C tests

Register-level tests for `hw/i2c/esp32c3_i2c.c`, `hw/misc/oro_i2c_chip.c` and
the bit-banged bridge in `hw/riscv/esp32c3.c`.

```sh
tests/esp32c3-i2c/run.sh
```

Exit status: 0 pass, 1 fail, 77 skip (no cross compiler). Every check prints
its own line, so a failure names itself:

```
--- controller (esp32c3) ---
I2CTEST START
probe 0x58 INT_RAW=0x0080 ACK  ok
probe 0x08 INT_RAW=0x0400 NACK ok
read 0x58[10] INT_RAW=0x0080 val=0x23 expect=0x23 ok
end-split 0x58[10] INT_RAW=0x0008 busy=0 then INT_RAW=0x0080 val=0x23 ok
clr-bus SCL_SP_CONF=0x000a ok
I2CTEST ALL OK
--- bitbang (esp32c3,i2c-pads=7/6) ---
I2CTEST START
bitbang pads 07/06 probe 0x58 ACK  ok
bitbang pads 01/02 probe 0x58 NACK ok
bitbang read 0x58[10] val=0x23 expect=0x23 ok
I2CTEST ALL OK
```

Two guests are built from the one `i2ctest.c` and both must pass. Which one is
built is a compile flag (`-DTEST_BITBANG`), because the two need different
machine arguments.

## Controller guest

Driven through the I2C registers, with **no `i2c-pads`** — deliberately, since
that is what covers the board pad check's fail-open path, and no bit-bang
bridge is instantiated either. 15 checks, in four groups:

- **`probe`** — the exact command list ESP-IDF's `i2c_master_probe()` emits
  (`RESTART(6), WRITE(1) ack_en, STOP(2)`). A configured address must raise
  TRANS_COMPLETE, an unconfigured one must raise NACK, and a NACK must not
  wedge the controller for the next probe.
- **`read`** / write-readback — register reads and writes against two chips at
  different addresses, including pointer auto-increment and the pointer not
  leaking between transfers.
- **`end-split`** — a register read split across an END command, the way the
  driver splits transfers that do not fit the FIFO. `SR.BUS_BUSY` must read 0
  in the gap, because the driver spins on it with no timeout in interrupt
  context (`s_i2c_send_command_async`), while the transfer itself must stay
  open so the resumed command list does not re-read its payload as an address.
  Those two requirements pull in opposite directions on a single flag; this is
  the only coverage for the split.
- **`clr-bus`** — `SCL_SP_CONF.SCL_RST_SLV_EN` is `R/W/SC` and must read back
  as 0 with `SCL_RST_SLV_NUM` preserved. `s_i2c_master_clear_bus()` polls it
  for 50ms and then fails every later transaction.

## Bit-bang guest

Run with `-machine esp32c3,i2c-pads=7/6`. No I2C registers at all: it toggles
two pads with plain GPIO writes and samples SDA out of `GPIO_IN_REG`, the way
a software I2C master does — which is how the firmware's board autodetection
probes the bus, with MicroPython's `SoftI2C`, before it knows which board it
is running on. 9 checks:

- **on the board's pads (7/6)** — probe ACK and NACK, a NACK not wedging the
  bus, and a two-phase register read, so data is proven to flow in both
  directions through `bitbang_i2c` onto the same `I2CBus` the
  `oro-i2c-chip` slaves sit on.
- **on pads the board's bus is not wired to (1/2)** — must find nothing, even
  though the chips are right there on the controller's bus. That is a Zero
  being probed on the ZeroC/One pad pair, and it is the dead-bus fallback the
  firmware's autodetection relies on; getting an ACK here would silently
  mis-detect the board.

The ACK is what needs the open-drain input path in `hw/gpio/esp32c3_gpio.c`
(`ESP32C3_GPIO_EXT_OD_IRQ`): without it a pad reads back only what the guest
drives, so a released SDA always reads high and every address NACKs.

## Why this is not a qtest

`-accel qtest` segfaults during `-machine esp32c3` init, with an empty script
and no I2C in the picture, so the usual `tests/qtest` approach is unavailable.
QEMU's gdbstub is not an alternative either: `M` packets go through
`address_space_write_rom`, which skips MMIO side effects, so register writes
silently no-op. Hence a real guest, loaded with `-kernel`.

The test writes its output straight to the UART0 FIFO rather than linking
anything from ESP-IDF, so it needs only a bare-metal riscv32 compiler. It is
not wired into meson because that compiler is not a QEMU build dependency.
