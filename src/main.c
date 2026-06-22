// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 wentywenty

#include "board.h"
#include "hpm_pdgo_drv.h"
#include "version.h"

/* 1 = WKUP pressed runs GS_USB;  0 = WKUP pressed runs EtherCAT */
#define WKUP_GS_USB_MODE 1

extern void gs_usb_run(void);
extern void ecat_run(void);

static bool is_wkup_pin_pressed(void)
{
    return (HPM_PDGO->PIN_STS & PDGO_PIN_STS_WKUP_MASK) == 0U;
}

int main(void)
{
    board_init();
    // board_detect_led_pinout();  // skip ADC: always use default PF02-07
    board_init_status_leds();

    printf("[TEST] All LEDs ON for 0.5s...\n");
    for (int ch = 0; ch < 4; ch++) board_led_can_on(ch);
    board_led_usb_on();
    board_led_mode_on();
    for (volatile uint32_t d = 0; d < 200000000U; d++) { __asm__("nop"); }
    for (int ch = 0; ch < 4; ch++) board_led_can_off(ch);
    board_led_usb_off();
    board_led_mode_off();
    printf("[TEST] All LEDs OFF\n");

    printf("\n==========================================\n");
    printf("=== ethercanfd v" FIRMWARE_VERSION " (" BUILD_DATE ") ===\n");

#if WKUP_GS_USB_MODE
    if (is_wkup_pin_pressed()) {
        printf("[BOOT] WKUP released  -> GS_USB mode\n");
        printf("==========================================\n\n");
        gs_usb_run();
    } else {
        printf("[BOOT] WKUP pressed -> EtherCAT mode\n");
        printf("==========================================\n\n");
        ecat_run();
    }
#else
    if (is_wkup_pin_pressed()) {
        printf("[BOOT] WKUP released  -> EtherCAT mode\n");
        printf("==========================================\n\n");
        ecat_run();
    } else {
        printf("[BOOT] WKUP pressed -> GS_USB mode\n");
        printf("==========================================\n\n");
        gs_usb_run();
    }
#endif

    while (1) { }
    return 0;
}
