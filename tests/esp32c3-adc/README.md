# ESP32-C3 SAR ADC test

Register-level test for `hw/adc/esp32c3_saradc.c` and the `adc-pad<n>` machine
properties in `hw/riscv/esp32c3.c`.

```sh
tests/esp32c3-adc/run.sh
```

Exit status: 0 pass, 1 fail, 77 skip (no cross compiler). Every check prints
its own line, so a failure names itself:

```
--- registers (esp32c3,adc-pad0=0,adc-pad4=4095,adc-pad5=1234) ---
ADCTEST START
readback ONETIME_SAMPLE=0x8b000000 ok
cal ADC1 INT_RAW=0x80000000 data=0x0001e000 ok
convert ADC2 ch0 polls=1 raw=1234 expect=1234 ok
convert ADC1 ch2 polls=1 raw=2048 expect=2048 ok
no-retrigger INT_RAW=0x00000000 ok
ADCTEST ALL OK
--- out-of-range code (esp32c3,adc-pad5=4096) ---
rejected: qemu-system-riscv32: adc-pad5: expected a raw ADC code in 0..4095, got 4096 ok
```

## What the model is for

ESP-IDF waits for a oneshot conversion with
`while (!adc_oneshot_ll_get_event(event));` and no timeout. It does so in
`adc_oneshot_hal_convert()`, and in `read_cal_channel()`, which the
self-calibration in `adc_hal_self_calibration()` calls about 140 times per
unit and attenuation. `esp_adc` runs that calibration from a constructor
whenever the eFuses hold no calibration data, which under QEMU is every boot
of any firmware that links the ADC driver. With nothing behind `APB_SARADC`
the DONE bit never appears and the guest never reaches `app_main()`.

So the model's job is to finish a conversion, at once, with a believable
result. What a pad reads is a fact about the board and comes from the machine:

```
-machine esp32c3,adc-pad5=3000
```

`adc-pad<n>` is the raw 12-bit code, 0..4095, exactly as `adc_oneshot_read()`
returns it; attenuation and the calibration curve are not modelled. Pads 0..4
are ADC1 channels 0..4 and pad 5 is ADC2 channel 0. A pad with no property
reads 2048.

## Checks

The guest writes `APB_SARADC` the way the HAL does: one bitfield per
read-modify-write, in the HAL's order.

- **`readback`** — the registers are storage. Before the model existed, reads
  returned 0, so each bitfield assignment erased the ones before it.
- **`cal`** — `cal_setup()` + `read_cal_channel()`: the channel "disabled"
  (`0xf` on ADC1, `0x9` on ADC2), which selects the internal ground. DONE must
  be up on the first read of `INT_RAW`, for that unit only, and the code must
  be 0: the calibration binary-searches for the smallest init code that reads
  0, so a constant 0 converges, on a non-zero code that IDF then caches.
- **`convert`** — `adc_oneshot_hal_convert()` on pads with and without an
  `adc-pad<n>`, at both ends of the range, before and after a calibration
  pass, and on an ADC1 channel that has no pad. `polls=1` is the point of the
  whole model. For ADC2 the word must also pass
  `adc_oneshot_ll_raw_check_valid()`, which reads the channel back out of bits
  16:13 and rejects anything above 9.
- **`int_clr`**, **`no-retrigger`**, **`no-unit`** — `INT_CLR` is
  write-1-to-clear, and only `ONETIME_START` going 0 → 1 converts. The HAL
  leaves START set between conversions and begins the next one by rewriting
  the unit-enable fields; a model that converted on the level would raise
  DONE again right after the driver cleared it, on the wrong channel.
- **`int_st`**, **`data-ro`** — `INT_ST` is `INT_RAW & INT_ENA`, and the
  result registers ignore writes.

`run.sh` then starts QEMU with a code that does not fit in 12 bits, which must
be refused rather than truncated into a plausible reading.

## Not modelled

Continuous (DMA) mode, the threshold monitors, the IIR filters and the
temperature sensor: their registers are storage and nothing happens.
`adc_cali_*` cannot work either, since it needs calibration eFuses.

The startup stub and linker script are shared with `../esp32c3-i2c/`, whose
README also explains why these are guests rather than qtests.
