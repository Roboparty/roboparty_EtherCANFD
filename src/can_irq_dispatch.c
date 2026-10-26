// SPDX-License-Identifier: GPL-3.0-only
#include "board.h"
#include "hpm_csr_drv.h"
#include "can_irq_dispatch.h"

static can_irq_handler_t volatile s_can_irq_handler;

void can_irq_set_handler(can_irq_handler_t handler)
{
    uint32_t level = disable_global_irq(CSR_MSTATUS_MIE_MASK);
    s_can_irq_handler = handler;
    if ((level & CSR_MSTATUS_MIE_MASK) != 0U) {
        restore_global_irq(CSR_MSTATUS_MIE_MASK);
    }
}

static void can_irq_dispatch(uint8_t channel)
{
    can_irq_handler_t handler = s_can_irq_handler;
    if (handler != NULL) {
        handler(channel);
    }
}

/* Keep the SDK wrappers in the vector table for both application modes. */
SDK_DECLARE_EXT_ISR_M(BOARD_CAN0_IRQn, can_irq_dispatch0)
void can_irq_dispatch0(void)
{
    can_irq_dispatch(0);
}

SDK_DECLARE_EXT_ISR_M(BOARD_CAN1_IRQn, can_irq_dispatch1)
void can_irq_dispatch1(void)
{
    can_irq_dispatch(1);
}

SDK_DECLARE_EXT_ISR_M(BOARD_CAN2_IRQn, can_irq_dispatch2)
void can_irq_dispatch2(void)
{
    can_irq_dispatch(2);
}

SDK_DECLARE_EXT_ISR_M(BOARD_CAN3_IRQn, can_irq_dispatch3)
void can_irq_dispatch3(void)
{
    can_irq_dispatch(3);
}
