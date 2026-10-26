// SPDX-License-Identifier: GPL-3.0-only
#ifndef CAN_IRQ_DISPATCH_H
#define CAN_IRQ_DISPATCH_H

#include <stdint.h>

typedef void (*can_irq_handler_t)(uint8_t channel);

/* Register the selected mode before enabling any CAN interrupt. */
void can_irq_set_handler(can_irq_handler_t handler);

#endif
