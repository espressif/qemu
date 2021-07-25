/*
 * Designware SD/MMC controller emulation
 *
 * Copyright (c) 2021 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DWC_SDMMC_H
#define DWC_SDMMC_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/sd/sd.h"


typedef struct DWCSDMMCState {
    /*< private >*/
    SysBusDevice busdev;

    /*< public >*/
    SDBus sdbus;
    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t ctrl;
    uint32_t cmd;
    uint32_t cmdarg;
    uint32_t resp[4];
    uint32_t intmask;
    uint32_t rintsts;
    uint32_t dbaddr;
    uint32_t dscaddr;
    uint32_t blksiz;
    uint32_t bytcnt;
    uint32_t pldmnd;
    uint32_t idinten;
    uint32_t idsts;

    size_t bytes_left;


} DWCSDMMCState;

#define TYPE_DWC_SDMMC  "dwc_sdmmc"
#define DWC_SDMMC(obj)  OBJECT_CHECK(DWCSDMMCState, (obj), \
                                         TYPE_DWC_SDMMC)

#endif /* DWC_SDMMC_H */
