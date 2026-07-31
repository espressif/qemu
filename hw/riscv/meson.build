riscv_ss = ss.source_set()
riscv_ss.add(files('boot.c'))
riscv_ss.add(when: 'CONFIG_RISCV_NUMA', if_true: files('numa.c'))
riscv_ss.add(files('riscv_hart.c'))
riscv_ss.add(when: 'CONFIG_OPENTITAN', if_true: files('opentitan.c'))
riscv_ss.add(when: 'CONFIG_RISCV_VIRT', if_true: files('virt.c'))
riscv_ss.add(when: 'CONFIG_SHAKTI_C', if_true: files('shakti_c.c'))
riscv_ss.add(when: 'CONFIG_SIFIVE_E', if_true: files('sifive_e.c'))
riscv_ss.add(when: 'CONFIG_SIFIVE_U', if_true: files('sifive_u.c'))
riscv_ss.add(when: 'CONFIG_SPIKE', if_true: files('spike.c'))
riscv_ss.add(when: 'CONFIG_MICROCHIP_PFSOC', if_true: files('microchip_pfsoc.c'))
riscv_ss.add(when: 'CONFIG_ACPI', if_true: files('virt-acpi-build.c'))
riscv_ss.add(when: 'CONFIG_RISCV_ESP32C3', if_true: files(
        'esp32c3_clk.c',
        'esp32c3_intmatrix.c',
        'esp32c3.c',
))
riscv_ss.add(when: 'CONFIG_RISCV_ESP32C5', if_true: files(
        'esp32c3_clk.c',
        'esp32c5_clk.c',
        'esp32c5.c',
        'esp32c5_intmatrix.c',
))
riscv_ss.add(when: 'CONFIG_RISCV_ESP32C6', if_true: files(
        'esp32c3_clk.c',
        'esp32c6_clk.c',
        'esp32c6.c',
        'esp32c6_intmatrix.c',
))
riscv_ss.add(when: 'CONFIG_RISCV_ESP32C61', if_true: files(
        'esp32c3_clk.c',
        'esp32c61_clk.c',
        'esp32c61.c',
        'esp32c61_intmatrix.c',
))

riscv_ss.add(when: 'CONFIG_RISCV_ESP32H4', if_true: files(
        'esp32c3_clk.c',
        'esp32h4_clk.c',
        'esp32h4.c',
        'esp32h4_intmatrix.c',
))
riscv_ss.add(when: 'CONFIG_RISCV_ESP32P4', if_true: files(
        'esp32c3_clk.c',
        'esp32p4_clk.c',
        'esp32p4.c',
        'esp32p4_intmatrix.c',
))
riscv_ss.add(when: 'CONFIG_RISCV_ESP32H2', if_true: files(
        'esp32c3_clk.c',
        'esp32h2_clk.c',
        'esp32h2.c',
        'esp32h2_intmatrix.c',
))
riscv_ss.add(when: 'CONFIG_RISCV_IOMMU', if_true: files('riscv-iommu.c', 'riscv-iommu-pci.c'))

hw_arch += {'riscv': riscv_ss}
