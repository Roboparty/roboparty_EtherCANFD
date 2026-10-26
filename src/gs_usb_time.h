// SPDX-License-Identifier: GPL-3.0-only
#ifndef GS_USB_TIME_H
#define GS_USB_TIME_H

#include <stdint.h>

/* Split before multiplying: cycles * 1000000 overflows after several hours. */
static inline uint64_t gs_usb_cycles_to_us(uint64_t cycles, uint32_t clock_hz)
{
    if (clock_hz == 0U) {
        return 0U;
    }
    return (cycles / clock_hz) * 1000000ULL +
           ((cycles % clock_hz) * 1000000ULL) / clock_hz;
}

#endif
