#pragma once

#define DR_REG_FRAMEBUF_BASE                    0x21000000

#define DR_REG_DPORT_BASE                       0x3ff00000
#define DR_REG_AES_BASE                         0x3ff01000
#define DR_REG_RSA_BASE                         0x3ff02000
#define DR_REG_SHA_BASE                         0x3ff03000
#define DR_REG_FLASH_MMU_TABLE_PRO              0x3ff10000
#define DR_REG_FLASH_MMU_TABLE_APP              0x3ff12000
#define DR_REG_DPORT_END                        0x3ff13FFC
#define DR_REG_DPORT_APB_BASE                   0x3ff40000
#define DR_REG_UART_BASE                        0x3ff40000
#define DR_REG_SPI1_BASE                        0x3ff42000
#define DR_REG_SPI0_BASE                        0x3ff43000
#define DR_REG_GPIO_BASE                        0x3ff44000
#define DR_REG_GPIO_SD_BASE                     0x3ff44f00
#define DR_REG_FE2_BASE                         0x3ff45000
#define DR_REG_FE_BASE                          0x3ff46000
#define DR_REG_FRC_TIMER_BASE                   0x3ff47000
#define DR_REG_RTCCNTL_BASE                     0x3ff48000
#define DR_REG_RTCIO_BASE                       0x3ff48400
#define DR_REG_SENS_BASE                        0x3ff48800
#define DR_REG_RTC_I2C_BASE                     0x3ff48C00
#define DR_REG_IO_MUX_BASE                      0x3ff49000
#define DR_REG_HINF_BASE                        0x3ff4B000
#define DR_REG_UHCI1_BASE                       0x3ff4C000
#define DR_REG_ANA_BASE                         0x3ff4E000
#define DR_REG_I2S_BASE                         0x3ff4F000
#define DR_REG_UART1_BASE                       0x3ff50000
#define DR_REG_BT_BASE                          0x3ff51000
#define DR_REG_I2C_EXT_BASE                     0x3ff53000
#define DR_REG_UHCI0_BASE                       0x3ff54000
#define DR_REG_SLCHOST_BASE                     0x3ff55000
#define DR_REG_RMT_BASE                         0x3ff56000
#define DR_REG_PCNT_BASE                        0x3ff57000
#define DR_REG_SLC_BASE                         0x3ff58000
#define DR_REG_LEDC_BASE                        0x3ff59000
#define DR_REG_EFUSE_BASE                       0x3ff5A000
#define DR_REG_SPI_ENCRYPT_BASE                 0x3ff5B000
#define DR_REG_NRX_BASE                         0x3ff5CC00
#define DR_REG_BB_BASE                          0x3ff5D000
#define DR_REG_PWM_BASE                         0x3ff5E000
#define DR_REG_TIMERGROUP0_BASE                 0x3ff5F000
#define DR_REG_TIMERGROUP1_BASE                 0x3ff60000
#define DR_REG_RTCMEM0_BASE                     0x3ff61000
#define DR_REG_RTCMEM1_BASE                     0x3ff62000
#define DR_REG_RTCMEM2_BASE                     0x3ff63000
#define DR_REG_SPI2_BASE                        0x3ff64000
#define DR_REG_SPI3_BASE                        0x3ff65000
#define DR_REG_SYSCON_BASE                      0x3ff66000
#define DR_REG_APB_CTRL_BASE                    0x3ff66000    /* Old name for SYSCON, to be removed */
#define DR_REG_I2C1_EXT_BASE                    0x3ff67000
#define DR_REG_SDMMC_BASE                       0x3ff68000
#define DR_REG_EMAC_BASE                        0x3ff69000
#define DR_REG_CAN_BASE                         0x3ff6B000
#define DR_REG_PWM1_BASE                        0x3ff6C000
#define DR_REG_I2S1_BASE                        0x3ff6D000
#define DR_REG_UART2_BASE                       0x3ff6E000
#define DR_REG_PWM2_BASE                        0x3ff6F000
#define DR_REG_PWM3_BASE                        0x3ff70000
#define DR_REG_WDEV_BASE                        0x3ff75000

#define APB_REG_BASE                            0x60000000

#define ETS_WIFI_MAC_INTR_SOURCE                0/**< interrupt of WiFi MAC, level*/
#define ETS_WIFI_MAC_NMI_SOURCE                 1/**< interrupt of WiFi MAC, NMI, use if MAC have bug to fix in NMI*/
#define ETS_WIFI_BB_INTR_SOURCE                 2/**< interrupt of WiFi BB, level, we can do some calibartion*/
#define ETS_BT_MAC_INTR_SOURCE                  3/**< will be cancelled*/
#define ETS_BT_BB_INTR_SOURCE                   4/**< interrupt of BT BB, level*/
#define ETS_BT_BB_NMI_SOURCE                    5/**< interrupt of BT BB, NMI, use if BB have bug to fix in NMI*/
#define ETS_RWBT_INTR_SOURCE                    6/**< interrupt of RWBT, level*/
#define ETS_RWBLE_INTR_SOURCE                   7/**< interrupt of RWBLE, level*/
#define ETS_RWBT_NMI_SOURCE                     8/**< interrupt of RWBT, NMI, use if RWBT have bug to fix in NMI*/
#define ETS_RWBLE_NMI_SOURCE                    9/**< interrupt of RWBLE, NMI, use if RWBT have bug to fix in NMI*/
#define ETS_SLC0_INTR_SOURCE                    10/**< interrupt of SLC0, level*/
#define ETS_SLC1_INTR_SOURCE                    11/**< interrupt of SLC1, level*/
#define ETS_UHCI0_INTR_SOURCE                   12/**< interrupt of UHCI0, level*/
#define ETS_UHCI1_INTR_SOURCE                   13/**< interrupt of UHCI1, level*/
#define ETS_TG0_T0_LEVEL_INTR_SOURCE            14/**< interrupt of TIMER_GROUP0, TIMER0, level, we would like use EDGE for timer if permission*/
#define ETS_TG0_T1_LEVEL_INTR_SOURCE            15/**< interrupt of TIMER_GROUP0, TIMER1, level, we would like use EDGE for timer if permission*/
#define ETS_TG0_WDT_LEVEL_INTR_SOURCE           16/**< interrupt of TIMER_GROUP0, WATCHDOG, level*/
#define ETS_TG0_LACT_LEVEL_INTR_SOURCE          17/**< interrupt of TIMER_GROUP0, LACT, level*/
#define ETS_TG1_T0_LEVEL_INTR_SOURCE            18/**< interrupt of TIMER_GROUP1, TIMER0, level, we would like use EDGE for timer if permission*/
#define ETS_TG1_T1_LEVEL_INTR_SOURCE            19/**< interrupt of TIMER_GROUP1, TIMER1, level, we would like use EDGE for timer if permission*/
#define ETS_TG1_WDT_LEVEL_INTR_SOURCE           20/**< interrupt of TIMER_GROUP1, WATCHDOG, level*/
#define ETS_TG1_LACT_LEVEL_INTR_SOURCE          21/**< interrupt of TIMER_GROUP1, LACT, level*/
#define ETS_GPIO_INTR_SOURCE                    22/**< interrupt of GPIO, level*/
#define ETS_GPIO_NMI_SOURCE                     23/**< interrupt of GPIO, NMI*/
#define ETS_FROM_CPU_INTR0_SOURCE               24/**< interrupt0 generated from a CPU, level*/ /* Used for FreeRTOS */
#define ETS_FROM_CPU_INTR1_SOURCE               25/**< interrupt1 generated from a CPU, level*/ /* Used for FreeRTOS */
#define ETS_FROM_CPU_INTR2_SOURCE               26/**< interrupt2 generated from a CPU, level*/ /* Used for DPORT Access */
#define ETS_FROM_CPU_INTR3_SOURCE               27/**< interrupt3 generated from a CPU, level*/ /* Used for DPORT Access */
#define ETS_SPI0_INTR_SOURCE                    28/**< interrupt of SPI0, level, SPI0 is for Cache Access, do not use this*/
#define ETS_SPI1_INTR_SOURCE                    29/**< interrupt of SPI1, level, SPI1 is for flash read/write, do not use this*/
#define ETS_SPI2_INTR_SOURCE                    30/**< interrupt of SPI2, level*/
#define ETS_SPI3_INTR_SOURCE                    31/**< interrupt of SPI3, level*/
#define ETS_I2S0_INTR_SOURCE                    32/**< interrupt of I2S0, level*/
#define ETS_I2S1_INTR_SOURCE                    33/**< interrupt of I2S1, level*/
#define ETS_UART0_INTR_SOURCE                   34/**< interrupt of UART0, level*/
#define ETS_UART1_INTR_SOURCE                   35/**< interrupt of UART1, level*/
#define ETS_UART2_INTR_SOURCE                   36/**< interrupt of UART2, level*/
#define ETS_SDIO_HOST_INTR_SOURCE               37/**< interrupt of SD/SDIO/MMC HOST, level*/
#define ETS_ETH_MAC_INTR_SOURCE                 38/**< interrupt of ethernet mac, level*/
#define ETS_PWM0_INTR_SOURCE                    39/**< interrupt of PWM0, level, Reserved*/
#define ETS_PWM1_INTR_SOURCE                    40/**< interrupt of PWM1, level, Reserved*/
#define ETS_PWM2_INTR_SOURCE                    41/**< interrupt of PWM2, level*/
#define ETS_PWM3_INTR_SOURCE                    42/**< interruot of PWM3, level*/
#define ETS_LEDC_INTR_SOURCE                    43/**< interrupt of LED PWM, level*/
#define ETS_EFUSE_INTR_SOURCE                   44/**< interrupt of efuse, level, not likely to use*/
#define ETS_CAN_INTR_SOURCE                     45/**< interrupt of can, level*/
#define ETS_RTC_CORE_INTR_SOURCE                46/**< interrupt of rtc core, level, include rtc watchdog*/
#define ETS_RMT_INTR_SOURCE                     47/**< interrupt of remote controller, level*/
#define ETS_PCNT_INTR_SOURCE                    48/**< interrupt of pluse count, level*/
#define ETS_I2C_EXT0_INTR_SOURCE                49/**< interrupt of I2C controller1, level*/
#define ETS_I2C_EXT1_INTR_SOURCE                50/**< interrupt of I2C controller0, level*/
#define ETS_RSA_INTR_SOURCE                     51/**< interrupt of RSA accelerator, level*/
#define ETS_SPI1_DMA_INTR_SOURCE                52/**< interrupt of SPI1 DMA, SPI1 is for flash read/write, do not use this*/
#define ETS_SPI2_DMA_INTR_SOURCE                53/**< interrupt of SPI2 DMA, level*/
#define ETS_SPI3_DMA_INTR_SOURCE                54/**< interrupt of SPI3 DMA, level*/
#define ETS_WDT_INTR_SOURCE                     55/**< will be cancelled*/
#define ETS_TIMER1_INTR_SOURCE                  56/**< will be cancelled*/
#define ETS_TIMER2_INTR_SOURCE                  57/**< will be cancelled*/
#define ETS_TG0_T0_EDGE_INTR_SOURCE             58/**< interrupt of TIMER_GROUP0, TIMER0, EDGE*/
#define ETS_TG0_T1_EDGE_INTR_SOURCE             59/**< interrupt of TIMER_GROUP0, TIMER1, EDGE*/
#define ETS_TG0_WDT_EDGE_INTR_SOURCE            60/**< interrupt of TIMER_GROUP0, WATCH DOG, EDGE*/
#define ETS_TG0_LACT_EDGE_INTR_SOURCE           61/**< interrupt of TIMER_GROUP0, LACT, EDGE*/
#define ETS_TG1_T0_EDGE_INTR_SOURCE             62/**< interrupt of TIMER_GROUP1, TIMER0, EDGE*/
#define ETS_TG1_T1_EDGE_INTR_SOURCE             63/**< interrupt of TIMER_GROUP1, TIMER1, EDGE*/
#define ETS_TG1_WDT_EDGE_INTR_SOURCE            64/**< interrupt of TIMER_GROUP1, WATCHDOG, EDGE*/
#define ETS_TG1_LACT_EDGE_INTR_SOURCE           65/**< interrupt of TIMER_GROUP0, LACT, EDGE*/
#define ETS_MMU_IA_INTR_SOURCE                  66/**< interrupt of MMU Invalid Access, LEVEL*/
#define ETS_MPU_IA_INTR_SOURCE                  67/**< interrupt of MPU Invalid Access, LEVEL*/
#define ETS_CACHE_IA_INTR_SOURCE                68/**< interrupt of Cache Invalied Access, LEVEL*/


#define ESP32_DPORT_CROSSCORE_INT_COUNT     4
#define ESP32_INT_MATRIX_OUTPUTS    32
#define ESP32_INT_MATRIX_INPUTS     69
#define ESP32_CPU_COUNT             2
#define ESP32_UART_COUNT            3
#define ESP32_FRC_COUNT             2
#define ESP32_TIMG_COUNT            2
#define ESP32_SPI_COUNT             4
#define ESP32_I2C_COUNT             2
#define ESP32_RTC_CNTL_SCRATCH_REG_COUNT     8

