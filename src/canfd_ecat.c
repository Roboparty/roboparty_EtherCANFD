// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 wentywenty

#include <stdio.h>
#include <string.h>
#include "board.h"
#include "hpm_mchtmr_drv.h"
#define ESC_SM_WD_SUPPORTED 0
#include "ecat_def.h"
#include "ecatappl.h"
#include "ecatslv.h"
#include "applInterface.h"
#include "ETHERCANFD.h"
#include "ETHERCANFDObjects.h"
#include "hpm_ecat_hw.h"
#include "hpm_l1c_drv.h"
#include "hpm_mcan_drv.h"
#include "hpm_mcan_regs.h"
#include "hpm_csr_drv.h"

/* Performance and Debug Logging Control */
#define ECANT_DEBUG_LOG 0
#if ECANT_DEBUG_LOG
#define ECANT_LOG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define ECANT_LOG(fmt, ...) do {} while(0)
#endif

#define ECANT_TX_PENDING_TIMEOUT_US (20000ULL)
#define ECANT_RX_QUEUE_DEPTH (16U)
#define ECANT_LED_ON_TICKS (50U)
#define ECANT_CAN2_LOWLEVEL_TIMING_AB_TEST (1)
#define ECANT_CAN2_TX_FORCE_NO_BRS_AB_TEST (1)
#define ECANT_CAN2_TX_FORCE_CLASSIC_AB_TEST (0)

typedef struct {
    uint8_t enable;
    uint8_t fd_enable;
    uint32_t bitrate_arb;
    uint32_t bitrate_data;
    uint8_t clean_dirty_frame;
} ecant_channel_config_shadow_t;

typedef struct {
    uint8_t ignore_tx;
    uint8_t ignore_rx;
} ecant_data_control_shadow_t;

static const uint32_t s_supported_arb_bitrates[] = {125000U, 250000U, 500000U, 1000000U};
static const uint32_t s_supported_data_bitrates[] = {1000000U, 2000000U, 4000000U, 5000000U, 8000000U, 10000000U};
static const uint8_t s_config_subindex_count = 5U;
static const uint8_t s_data_control_subindex_count = 2U;

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") uint32_t s_mcan_msg_buf0[MCAN_MSG_BUF_SIZE_IN_WORDS];
ATTR_PLACE_AT(".ahb_sram") uint32_t s_mcan_msg_buf1[MCAN_MSG_BUF_SIZE_IN_WORDS];
ATTR_PLACE_AT(".ahb_sram") uint32_t s_mcan_msg_buf2[MCAN_MSG_BUF_SIZE_IN_WORDS];
ATTR_PLACE_AT(".ahb_sram") uint32_t s_mcan_msg_buf3[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif


extern TOBJ7000 Can0TxFrame0x7000;
extern TOBJ7010 Can1TxFrame0x7010;
extern TOBJ7020 Can2TxFrame0x7020;
extern TOBJ7030 Can3TxFrame0x7030;

extern TOBJ6000 Can0RxFrame0x6000;
extern TOBJ6010 Can1RxFrame0x6010;
extern TOBJ6020 Can2RxFrame0x6020;
extern TOBJ6030 Can3RxFrame0x6030;

extern TOBJ8000 Can0Config0x8000;
extern TOBJ8010 Can1Config0x8010;
extern TOBJ8020 Can2Config0x8020;
extern TOBJ8030 Can3Config0x8030;

extern TOBJ9000 Can0Status0x9000;
extern TOBJ9010 Can1Status0x9010;
extern TOBJ9020 Can2Status0x9020;
extern TOBJ9030 Can3Status0x9030;

extern TOBJA000 DataControl0xA000;

static TOBJ7000* s_tx_frames[4];
static TOBJ6000* s_rx_frames[4];
static TOBJ8000* s_config_objs[4];
static TOBJ9000* s_status_objs[4];
static ecant_channel_config_shadow_t s_config_shadow[4];
static ecant_data_control_shadow_t s_data_control_shadow = {0, 0};

static volatile uint8_t s_canfd_rx_head[4] = {0, 0, 0, 0};
static volatile uint8_t s_canfd_rx_tail[4] = {0, 0, 0, 0};
static volatile uint32_t s_canfd_rx_drop_count[4] = {0, 0, 0, 0};
static volatile uint32_t s_canfd_rx_isr_count[4] = {0, 0, 0, 0};
static volatile uint32_t s_canfd_rx_isr_disc[4] = {0, 0, 0, 0};
static mcan_rx_message_t s_canfd_rx_queue[4][ECANT_RX_QUEUE_DEPTH];
static bool s_canfd_tx_pending[4] = {false, false, false, false};
static uint32_t s_canfd_tx_fifo_idx[4] = {0, 0, 0, 0};
static uint32_t s_canfd_tx_id[4] = {0, 0, 0, 0};
static uint8_t s_canfd_tx_dlc[4] = {0, 0, 0, 0};
static bool s_canfd_tx_left_op_reported[4] = {false, false, false, false};
static bool s_canfd_tx_cancel_requested[4] = {false, false, false, false};
static uint64_t s_canfd_tx_start_us[4] = {0, 0, 0, 0};
static uint16_t s_canfd_tx_trigger_seen[4] = {0, 0, 0, 0};
static uint8_t s_can_led_ticks[4] = {0, 0, 0, 0};
static uint16_t s_ecat_usb_led_tick;
static bool s_can2_tx_ab_override_reported = false;

static MCAN_Type *s_can_bases[4] = {BOARD_CAN0_BASE, BOARD_CAN1_BASE, BOARD_CAN2_BASE, BOARD_CAN3_BASE};
static uint32_t s_can_irqs[4] = {BOARD_CAN0_IRQn, BOARD_CAN1_IRQn, BOARD_CAN2_IRQn, BOARD_CAN3_IRQn};
static uint32_t s_can_clk_hz[4];

static bool sanitize_channel_config(uint8_t ch, bool *normalized);
static void sanitize_data_control(void);
static void dump_can_config_regs(uint8_t ch, const char *tag);

static bool should_use_can2_lowlevel_timing_ab(uint8_t ch)
{
#if ECANT_CAN2_LOWLEVEL_TIMING_AB_TEST
    const ecant_channel_config_shadow_t *cfg = &s_config_shadow[ch];

    return (cfg->enable != 0U) &&
           (cfg->fd_enable != 0U) &&
           (cfg->bitrate_arb == 1000000U) &&
           (cfg->bitrate_data == 5000000U);
#else
    (void)ch;
    return false;
#endif
}

static void apply_can2_lowlevel_timing_ab(mcan_config_t *config, uint32_t can_clk_hz)
{
    config->use_lowlevel_timing_setting = true;

    if (can_clk_hz >= 80000000U) {
        config->can_timing.prescaler = 4U;
        config->can_timing.num_seg1 = 15U;
        config->can_timing.num_seg2 = 4U;
        config->can_timing.num_sjw = 4U;

        config->canfd_timing.prescaler = 1U;
        config->canfd_timing.num_seg1 = 11U;
        config->canfd_timing.num_seg2 = 4U;
        config->canfd_timing.num_sjw = 4U;
    } else {
        config->can_timing.prescaler = 2U;
        config->can_timing.num_seg1 = 15U;
        config->can_timing.num_seg2 = 4U;
        config->can_timing.num_sjw = 4U;

        config->canfd_timing.prescaler = 1U;
        config->canfd_timing.num_seg1 = 5U;
        config->canfd_timing.num_seg2 = 2U;
        config->canfd_timing.num_sjw = 2U;
    }

    config->can_timing.enable_tdc = false;
    config->canfd_timing.enable_tdc = true;
}

static void dump_can_config_summary(uint8_t ch, const mcan_config_t *config, const char *tag)
{
    if (config->use_lowlevel_timing_setting) {
        ECANT_LOG("CAN%d %s CFG: clk=%lu, ll=%u, mode=%u, fd=%u, tdc=%u, rom=%u, dar=%u, arb=%lu, data=%lu, npre=%u nseg1=%u nseg2=%u nsjw=%u, dpre=%u dseg1=%u dseg2=%u dsjw=%u\n",
               ch,
               tag,
               (unsigned long)s_can_clk_hz[ch],
               (unsigned)config->use_lowlevel_timing_setting,
               (unsigned)config->mode,
               (unsigned)config->enable_canfd,
               (unsigned)config->enable_tdc,
               (unsigned)config->enable_restricted_operation_mode,
               (unsigned)config->disable_auto_retransmission,
               (unsigned long)s_config_shadow[ch].bitrate_arb,
               (unsigned long)s_config_shadow[ch].bitrate_data,
               (unsigned)config->can_timing.prescaler,
               (unsigned)config->can_timing.num_seg1,
               (unsigned)config->can_timing.num_seg2,
               (unsigned)config->can_timing.num_sjw,
               (unsigned)config->canfd_timing.prescaler,
               (unsigned)config->canfd_timing.num_seg1,
               (unsigned)config->canfd_timing.num_seg2,
               (unsigned)config->canfd_timing.num_sjw);
    } else {
        ECANT_LOG("CAN%d %s CFG: clk=%lu, ll=%u, mode=%u, fd=%u, tdc=%u, rom=%u, dar=%u, arb=%lu, data=%lu\n",
               ch,
               tag,
               (unsigned long)s_can_clk_hz[ch],
               (unsigned)config->use_lowlevel_timing_setting,
               (unsigned)config->mode,
               (unsigned)config->enable_canfd,
               (unsigned)config->enable_tdc,
               (unsigned)config->enable_restricted_operation_mode,
               (unsigned)config->disable_auto_retransmission,
               (unsigned long)config->baudrate,
               (unsigned long)config->baudrate_fd);
    }
}

static void dump_can_config_regs(uint8_t ch, const char *tag)
{
    MCAN_Type *base = s_can_bases[ch];

    ECANT_LOG("CAN%d %s REG: CCCR=0x%08lx NBTP=0x%08lx DBTP=0x%08lx TDCR=0x%08lx TEST=0x%08lx PSR=0x%08lx ECR=0x%08lx TXBC=0x%08lx TXFQS=0x%08lx RXF0S=0x%08lx\n",
           ch,
           tag,
           (unsigned long)base->CCCR,
           (unsigned long)base->NBTP,
           (unsigned long)base->DBTP,
           (unsigned long)base->TDCR,
           (unsigned long)base->TEST,
           (unsigned long)base->PSR,
           (unsigned long)base->ECR,
           (unsigned long)base->TXBC,
           (unsigned long)base->TXFQS,
           (unsigned long)base->RXF0S);
}

static void apply_can2_tx_frame_ab_override(uint8_t ch, mcan_tx_frame_t *tx)
{
    if (ch != 2U || tx == NULL) {
        return;
    }

#if ECANT_CAN2_TX_FORCE_CLASSIC_AB_TEST
    tx->canfd_frame = 0;
    tx->bitrate_switch = 0;
    if (!s_can2_tx_ab_override_reported) {
        ECANT_LOG("CAN2 TX A/B override active: force classic CAN framing\n");
        s_can2_tx_ab_override_reported = true;
    }
#elif ECANT_CAN2_TX_FORCE_NO_BRS_AB_TEST
    if (tx->canfd_frame != 0U) {
        tx->bitrate_switch = 0;
        if (!s_can2_tx_ab_override_reported) {
            ECANT_LOG("CAN2 TX A/B override active: force FD without BRS\n");
            s_can2_tx_ab_override_reported = true;
        }
    }
#endif
}

static void reset_tx_runtime_state(uint8_t ch, bool clear_pdo, bool clear_trigger_seen)
{
    s_canfd_tx_pending[ch] = false;
    s_canfd_tx_fifo_idx[ch] = 0;
    s_canfd_tx_id[ch] = 0;
    s_canfd_tx_dlc[ch] = 0;
    s_canfd_tx_left_op_reported[ch] = false;
    s_canfd_tx_cancel_requested[ch] = false;
    s_canfd_tx_start_us[ch] = 0;
    if (clear_trigger_seen) {
        s_canfd_tx_trigger_seen[ch] = 0;
    }
    (void)clear_pdo;
}

static void reset_all_tx_runtime_state(bool clear_pdo, bool clear_trigger_seen)
{
    for (uint8_t ch = 0; ch < 4; ch++) {
        reset_tx_runtime_state(ch, clear_pdo, clear_trigger_seen);
    }
}

static void cancel_all_hw_tx_buffers(uint8_t ch)
{
    uint32_t txbc = s_can_bases[ch]->TXBC;
    uint32_t num_bufs = MCAN_TXBC_NDTB_GET(txbc);
    for (uint32_t i = 0; i < num_bufs; i++) {
        s_can_bases[ch]->TXBCR = (1U << i);
    }
}

static uint64_t get_time_us(void)
{
    uint64_t cycles = hpm_csr_get_core_mcycle();
    uint32_t cpu_hz = clock_get_frequency(clock_cpu0);
    if (cpu_hz == 0U) {
        return 0;
    }
    return (cycles * 1000000ULL) / cpu_hz;
}

static void print_tx_pending_diag(uint8_t ch, const char *tag)
{
    MCAN_Type *base = s_can_bases[ch];
    uint32_t fifo_index = s_canfd_tx_fifo_idx[ch];
    mcan_error_count_t err_count;
    mcan_protocol_status_t psr;
    mcan_get_error_counter(base, &err_count);
    mcan_get_protocol_status(base, &psr);
    ECANT_LOG("CAN%d %s: ID=0x%lx, DLC=%u, FIFO=%lu, TXBRP=%d, TXBTO=%d, TXBCF=%d, ACT=%u, LEC=%u, DLEC=%u, TEC=%u, REC=%u, BO=%u, EP=%u, EW=%u\n",
           ch,
           tag,
           s_canfd_tx_id[ch],
           s_canfd_tx_dlc[ch],
           fifo_index,
           mcan_is_transmit_request_pending(base, fifo_index) ? 1 : 0,
           mcan_is_transmit_occurred(base, fifo_index) ? 1 : 0,
           mcan_is_tx_buf_cancellation_finished(base, fifo_index) ? 1 : 0,
           (unsigned)psr.activity,
           (unsigned)psr.last_error_code,
           (unsigned)mcan_get_data_phase_last_error_code(base),
           (unsigned)err_count.transmit_error_count,
           (unsigned)err_count.receive_error_count,
           (unsigned)psr.in_bus_off_state,
           (unsigned)psr.in_error_passive_state,
           (unsigned)psr.in_warning_state);
    dump_can_config_regs(ch, tag);
}

static bool enqueue_can_rx(uint8_t ch, const mcan_rx_message_t *msg)
{
    uint8_t head = s_canfd_rx_head[ch];
    uint8_t next_head = (uint8_t)((head + 1U) % ECANT_RX_QUEUE_DEPTH);

    if (next_head == s_canfd_rx_tail[ch]) {
        s_canfd_rx_drop_count[ch] += 1U;
        return false;
    }

    s_canfd_rx_queue[ch][head] = *msg;
    s_canfd_rx_head[ch] = next_head;
    return true;
}

static bool dequeue_can_rx(uint8_t ch, mcan_rx_message_t *msg)
{
    uint8_t tail = s_canfd_rx_tail[ch];

    if (tail == s_canfd_rx_head[ch]) {
        return false;
    }

    *msg = s_canfd_rx_queue[ch][tail];
    s_canfd_rx_tail[ch] = (uint8_t)((tail + 1U) % ECANT_RX_QUEUE_DEPTH);
    return true;
}

static void handle_can_isr(uint8_t ch)
{
    uint32_t flags = mcan_get_interrupt_flags(s_can_bases[ch]);
    mcan_clear_interrupt_flags(s_can_bases[ch], flags);
}

static void ecat_can0_isr(void) { handle_can_isr(0); }
static void ecat_can1_isr(void) { handle_can_isr(1); }
static void ecat_can2_isr(void) { handle_can_isr(2); }
static void ecat_can3_isr(void) { handle_can_isr(3); }

static volatile uint32_t s_canfd_rx_polled[4] = {0, 0, 0, 0};

static mcan_config_t s_can_config;

static int get_config_channel_from_index(uint16_t index)
{
    if (index < 0x8000U || index > 0x8030U || ((index - 0x8000U) % 0x10U) != 0U) {
        return -1;
    }

    return (int)((index - 0x8000U) / 0x10U);
}

static void build_config_snapshot(uint8_t ch, TOBJ8000 *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->u16SubIndex0 = s_config_subindex_count;
    snapshot->Enable = s_config_shadow[ch].enable;
    snapshot->FdEnable = s_config_shadow[ch].fd_enable;
    snapshot->BitrateArb = s_config_shadow[ch].bitrate_arb;
    snapshot->BitrateData = s_config_shadow[ch].bitrate_data;
    snapshot->CleanDirtyFrame = s_config_shadow[ch].clean_dirty_frame;
}

static void build_data_control_snapshot(TOBJA000 *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->u16SubIndex0 = s_data_control_subindex_count;
    snapshot->IgnoreTx = s_data_control_shadow.ignore_tx;
    snapshot->IgnoreRx = s_data_control_shadow.ignore_rx;
}

static UINT8 read_config_subindex(const TOBJ8000 *snapshot,
                                  UINT8 subindex,
                                  UINT32 size,
                                  UINT16 MBXMEM *p_data)
{
    if (p_data == NULL || snapshot == NULL) {
        return ABORTIDX_GENERAL_ERROR;
    }

    switch (subindex) {
    case 0:
        if (size != sizeof(snapshot->u16SubIndex0)) {
            return ABORTIDX_PARAM_LENGTH_ERROR;
        }
        memcpy((uint8_t *)p_data, &snapshot->u16SubIndex0, sizeof(snapshot->u16SubIndex0));
        return 0U;
    case 1:
        if (size != sizeof(snapshot->Enable)) {
            return ABORTIDX_PARAM_LENGTH_ERROR;
        }
        memcpy((uint8_t *)p_data, &snapshot->Enable, sizeof(snapshot->Enable));
        return 0U;
    case 2:
        if (size != sizeof(snapshot->FdEnable)) {
            return ABORTIDX_PARAM_LENGTH_ERROR;
        }
        memcpy((uint8_t *)p_data, &snapshot->FdEnable, sizeof(snapshot->FdEnable));
        return 0U;
    case 3:
        if (size != sizeof(snapshot->BitrateArb)) {
            return ABORTIDX_PARAM_LENGTH_ERROR;
        }
        memcpy((uint8_t *)p_data, &snapshot->BitrateArb, sizeof(snapshot->BitrateArb));
        return 0U;
    case 4:
        if (size != sizeof(snapshot->BitrateData)) {
            return ABORTIDX_PARAM_LENGTH_ERROR;
        }
        memcpy((uint8_t *)p_data, &snapshot->BitrateData, sizeof(snapshot->BitrateData));
        return 0U;
    case 5:
        if (size != sizeof(snapshot->CleanDirtyFrame)) {
            return ABORTIDX_PARAM_LENGTH_ERROR;
        }
        memcpy((uint8_t *)p_data, &snapshot->CleanDirtyFrame, sizeof(snapshot->CleanDirtyFrame));
        return 0U;
    default:
        return ABORTIDX_SUBINDEX_NOT_EXISTING;
    }
}

static UINT8 write_config_subindex(TOBJ8000 *snapshot,
                                   UINT8 subindex,
                                   UINT32 size,
                                   const UINT16 MBXMEM *p_data)
{
    if (p_data == NULL || snapshot == NULL) {
        return ABORTIDX_GENERAL_ERROR;
    }

    switch (subindex) {
    case 0:
        return ABORTIDX_READ_ONLY_ENTRY;
    case 1:
        if (size != sizeof(snapshot->Enable)) {
            return ABORTIDX_PARAM_LENGTH_ERROR;
        }
        memcpy(&snapshot->Enable, (const uint8_t *)p_data, sizeof(snapshot->Enable));
        return 0U;
    case 2:
        if (size != sizeof(snapshot->FdEnable)) {
            return ABORTIDX_PARAM_LENGTH_ERROR;
        }
        memcpy(&snapshot->FdEnable, (const uint8_t *)p_data, sizeof(snapshot->FdEnable));
        return 0U;
    case 3:
        if (size != sizeof(snapshot->BitrateArb)) {
            return ABORTIDX_PARAM_LENGTH_ERROR;
        }
        memcpy(&snapshot->BitrateArb, (const uint8_t *)p_data, sizeof(snapshot->BitrateArb));
        return 0U;
    case 4:
        if (size != sizeof(snapshot->BitrateData)) {
            return ABORTIDX_PARAM_LENGTH_ERROR;
        }
        memcpy(&snapshot->BitrateData, (const uint8_t *)p_data, sizeof(snapshot->BitrateData));
        return 0U;
    case 5:
        if (size != sizeof(snapshot->CleanDirtyFrame)) {
            return ABORTIDX_PARAM_LENGTH_ERROR;
        }
        memcpy(&snapshot->CleanDirtyFrame, (const uint8_t *)p_data, sizeof(snapshot->CleanDirtyFrame));
        return 0U;
    default:
        return ABORTIDX_SUBINDEX_NOT_EXISTING;
    }
}

static UINT8 read_data_control_subindex(const TOBJA000 *snapshot,
                                        UINT8 subindex,
                                        UINT32 size,
                                        UINT16 MBXMEM *p_data)
{
    if (p_data == NULL || snapshot == NULL) {
        return ABORTIDX_GENERAL_ERROR;
    }

    switch (subindex) {
    case 0:
        if (size != sizeof(snapshot->u16SubIndex0)) {
            return ABORTIDX_PARAM_LENGTH_ERROR;
        }
        memcpy((uint8_t *)p_data, &snapshot->u16SubIndex0, sizeof(snapshot->u16SubIndex0));
        return 0U;
    case 1:
        if (size != sizeof(snapshot->IgnoreTx)) {
            return ABORTIDX_PARAM_LENGTH_ERROR;
        }
        memcpy((uint8_t *)p_data, &snapshot->IgnoreTx, sizeof(snapshot->IgnoreTx));
        return 0U;
    case 2:
        if (size != sizeof(snapshot->IgnoreRx)) {
            return ABORTIDX_PARAM_LENGTH_ERROR;
        }
        memcpy((uint8_t *)p_data, &snapshot->IgnoreRx, sizeof(snapshot->IgnoreRx));
        return 0U;
    default:
        return ABORTIDX_SUBINDEX_NOT_EXISTING;
    }
}

static UINT8 write_data_control_subindex(TOBJA000 *snapshot,
                                         UINT8 subindex,
                                         UINT32 size,
                                         const UINT16 MBXMEM *p_data)
{
    if (p_data == NULL || snapshot == NULL) {
        return ABORTIDX_GENERAL_ERROR;
    }

    switch (subindex) {
    case 0:
        return ABORTIDX_READ_ONLY_ENTRY;
    case 1:
        if (size != sizeof(snapshot->IgnoreTx)) {
            return ABORTIDX_PARAM_LENGTH_ERROR;
        }
        memcpy(&snapshot->IgnoreTx, (const uint8_t *)p_data, sizeof(snapshot->IgnoreTx));
        return 0U;
    case 2:
        if (size != sizeof(snapshot->IgnoreRx)) {
            return ABORTIDX_PARAM_LENGTH_ERROR;
        }
        memcpy(&snapshot->IgnoreRx, (const uint8_t *)p_data, sizeof(snapshot->IgnoreRx));
        return 0U;
    default:
        return ABORTIDX_SUBINDEX_NOT_EXISTING;
    }
}

static UINT8 ecant_config_object_read(UINT16 index,
                                      UINT8 subindex,
                                      UINT32 size,
                                      UINT16 MBXMEM *p_data,
                                      UINT8 b_complete_access)
{
    TOBJ8000 snapshot;
    int ch = get_config_channel_from_index(index);
    (void)b_complete_access;

    if (ch < 0) {
        return ABORTIDX_OBJECT_NOT_EXISTING;
    }

    build_config_snapshot((uint8_t)ch, &snapshot);
    return read_config_subindex(&snapshot, subindex, size, p_data);
}

static UINT8 ecant_data_control_read(UINT16 index,
                                     UINT8 subindex,
                                     UINT32 size,
                                     UINT16 MBXMEM *p_data,
                                     UINT8 b_complete_access)
{
    TOBJA000 snapshot;
    (void)b_complete_access;

    if (index != 0xA000U) {
        return ABORTIDX_OBJECT_NOT_EXISTING;
    }

    build_data_control_snapshot(&snapshot);
    return read_data_control_subindex(&snapshot, subindex, size, p_data);
}

static UINT8 ecant_config_object_write(UINT16 index,
                                       UINT8 subindex,
                                       UINT32 size,
                                       UINT16 MBXMEM *p_data,
                                       UINT8 b_complete_access)
{
    TOBJ8000 candidate;
    int ch = get_config_channel_from_index(index);
    UINT8 result;
    (void)b_complete_access;

    if (ch < 0) {
        return ABORTIDX_OBJECT_NOT_EXISTING;
    }

    build_config_snapshot((uint8_t)ch, &candidate);
    result = write_config_subindex(&candidate, subindex, size, p_data);
    if (result != 0U) {
        return result;
    }

    if (candidate.u16SubIndex0 != s_config_subindex_count) {
        return ABORTIDX_VALUE_EXCEEDED;
    }

    *s_config_objs[ch] = candidate;
    if (!sanitize_channel_config((uint8_t)ch, NULL)) {
        return ABORTIDX_VALUE_EXCEEDED;
    }

    return 0U;
}

static UINT8 ecant_data_control_write(UINT16 index,
                                      UINT8 subindex,
                                      UINT32 size,
                                      UINT16 MBXMEM *p_data,
                                      UINT8 b_complete_access)
{
    TOBJA000 candidate;
    UINT8 result;
    (void)b_complete_access;

    if (index != 0xA000U) {
        return ABORTIDX_OBJECT_NOT_EXISTING;
    }

    build_data_control_snapshot(&candidate);
    result = write_data_control_subindex(&candidate, subindex, size, p_data);
    if (result != 0U) {
        return result;
    }

    if (candidate.u16SubIndex0 != s_data_control_subindex_count) {
        return ABORTIDX_VALUE_EXCEEDED;
    }
    if (candidate.IgnoreTx > 1U || candidate.IgnoreRx > 1U) {
        return ABORTIDX_VALUE_EXCEEDED;
    }

    DataControl0xA000 = candidate;
    sanitize_data_control();
    return 0U;
}

static void register_object_callbacks(void)
{
    static const uint16_t config_indexes[4] = {0x8000U, 0x8010U, 0x8020U, 0x8030U};

    for (uint8_t ch = 0; ch < 4; ch++) {
        TOBJECT OBJMEM *p_obj = (TOBJECT OBJMEM *)OBJ_GetObjectHandle(config_indexes[ch]);
        if (p_obj != NULL) {
            p_obj->Read = ecant_config_object_read;
            p_obj->Write = ecant_config_object_write;
        }
    }

    {
        TOBJECT OBJMEM *p_obj = (TOBJECT OBJMEM *)OBJ_GetObjectHandle(0xA000U);
        if (p_obj != NULL) {
            p_obj->Read = ecant_data_control_read;
            p_obj->Write = ecant_data_control_write;
        }
    }
}

static uint32_t normalize_supported_bitrate(uint32_t raw,
                                            const uint32_t *supported,
                                            size_t count,
                                            uint32_t tolerance_percent)
{
    uint32_t best = 0;
    uint32_t best_delta = UINT32_MAX;

    for (size_t i = 0; i < count; i++) {
        uint32_t candidate = supported[i];
        uint32_t delta = (raw > candidate) ? (raw - candidate) : (candidate - raw);
        if (delta < best_delta) {
            best = candidate;
            best_delta = delta;
        }
        if (delta == 0U) {
            return candidate;
        }
    }

    if (best != 0U && (uint64_t)best_delta * 100ULL <= (uint64_t)best * tolerance_percent) {
        return best;
    }

    return 0U;
}

static void sync_shadow_to_config_object(uint8_t ch)
{
    TOBJ8000 *cfg = s_config_objs[ch];
    const ecant_channel_config_shadow_t *shadow = &s_config_shadow[ch];
    cfg->Enable = shadow->enable;
    cfg->FdEnable = shadow->fd_enable;
    cfg->BitrateArb = shadow->bitrate_arb;
    cfg->BitrateData = shadow->bitrate_data;
    cfg->CleanDirtyFrame = shadow->clean_dirty_frame;
}

static void sync_shadow_to_data_control_object(void)
{
    DataControl0xA000.IgnoreTx = s_data_control_shadow.ignore_tx;
    DataControl0xA000.IgnoreRx = s_data_control_shadow.ignore_rx;
}

static bool sanitize_channel_config(uint8_t ch, bool *normalized)
{
    TOBJ8000 *cfg = s_config_objs[ch];
    ecant_channel_config_shadow_t candidate;
    uint32_t normalized_arb = normalize_supported_bitrate(cfg->BitrateArb,
                                                          s_supported_arb_bitrates,
                                                          sizeof(s_supported_arb_bitrates) / sizeof(s_supported_arb_bitrates[0]),
                                                          5U);
    uint32_t normalized_data = normalize_supported_bitrate(cfg->BitrateData,
                                                           s_supported_data_bitrates,
                                                           sizeof(s_supported_data_bitrates) / sizeof(s_supported_data_bitrates[0]),
                                                           5U);

    if (cfg->Enable > 1U || cfg->FdEnable > 1U || cfg->CleanDirtyFrame > 1U ||
        normalized_arb == 0U || normalized_data == 0U) {
        ECANT_LOG("Rejecting CAN%u Config: raw enable=%u fd=%u arb=%lu data=%lu clean=%u; restoring last good values\n",
               ch,
               (unsigned)cfg->Enable,
               (unsigned)cfg->FdEnable,
               (unsigned long)cfg->BitrateArb,
               (unsigned long)cfg->BitrateData,
               (unsigned)cfg->CleanDirtyFrame);
        sync_shadow_to_config_object(ch);
        if (normalized != NULL) {
            *normalized = false;
        }
        return false;
    }

    candidate.enable = cfg->Enable;
    candidate.fd_enable = cfg->FdEnable;
    candidate.bitrate_arb = normalized_arb;
    candidate.bitrate_data = normalized_data;
    candidate.clean_dirty_frame = cfg->CleanDirtyFrame;

    if (normalized != NULL) {
        *normalized = (candidate.bitrate_arb != cfg->BitrateArb) ||
                      (candidate.bitrate_data != cfg->BitrateData);
    }

    if (candidate.bitrate_arb != cfg->BitrateArb || candidate.bitrate_data != cfg->BitrateData) {
        ECANT_LOG("Normalizing CAN%u Config: raw arb=%lu data=%lu -> arb=%lu data=%lu\n",
               ch,
               (unsigned long)cfg->BitrateArb,
               (unsigned long)cfg->BitrateData,
               (unsigned long)candidate.bitrate_arb,
               (unsigned long)candidate.bitrate_data);
    }

    s_config_shadow[ch] = candidate;
    sync_shadow_to_config_object(ch);
    return true;
}

static void sanitize_data_control(void)
{
    if (DataControl0xA000.IgnoreTx > 1U || DataControl0xA000.IgnoreRx > 1U) {
        ECANT_LOG("Rejecting DataControl: raw ignore_tx=%u ignore_rx=%u; restoring last good values\n",
               (unsigned)DataControl0xA000.IgnoreTx,
               (unsigned)DataControl0xA000.IgnoreRx);
        sync_shadow_to_data_control_object();
        return;
    }

    s_data_control_shadow.ignore_tx = DataControl0xA000.IgnoreTx;
    s_data_control_shadow.ignore_rx = DataControl0xA000.IgnoreRx;
    sync_shadow_to_data_control_object();
}

static void apply_can_config(uint8_t ch)
{
    mcan_get_default_config(s_can_bases[ch], &s_can_config);
    s_can_config.baudrate = 1000000;
    s_can_config.baudrate_fd = 5000000;
    s_can_config.enable_canfd = true;
    s_can_config.enable_tdc = true;
    s_can_config.tdc_config.ssp_offset = 8;
    s_can_config.tdc_config.filter_window_length = 4;
    s_can_config.mode = mcan_mode_normal;
    s_can_config.disable_auto_retransmission = false;
    if (s_can_clk_hz[ch] >= 80000000U) {
        apply_can2_lowlevel_timing_ab(&s_can_config, s_can_clk_hz[ch]);
    } else {
        s_can_config.use_lowlevel_timing_setting = true;
        s_can_config.can_timing.prescaler = 2U;
        s_can_config.can_timing.num_seg1 = 15U;
        s_can_config.can_timing.num_seg2 = 4U;
        s_can_config.can_timing.num_sjw = 4U;
        s_can_config.can_timing.enable_tdc = false;
        s_can_config.canfd_timing.prescaler = 1U;
        s_can_config.canfd_timing.num_seg1 = 5U;
        s_can_config.canfd_timing.num_seg2 = 2U;
        s_can_config.canfd_timing.num_sjw = 2U;
        s_can_config.canfd_timing.enable_tdc = true;
    }

    ECANT_LOG("CAN%d Init: Arb=1000000, Data=5000000, FD=1, TDC=1, clk=%lu\n",
           ch, (unsigned long)s_can_clk_hz[ch]);
    dump_can_config_summary(ch, &s_can_config, "PRE-INIT");

    mcan_get_default_ram_config(s_can_bases[ch], &s_can_config.ram_config, true);
    {
        hpm_stat_t init_stat = mcan_init(s_can_bases[ch], &s_can_config, s_can_clk_hz[ch]);
        ECANT_LOG("CAN%d mcan_init stat=%ld\n", ch, (long)init_stat);
    }
    dump_can_config_regs(ch, "POST-INIT");

    s_can_bases[ch]->CCCR &= ~(uint32_t)(MCAN_CCCR_DAR_MASK);

    reset_tx_runtime_state(ch, false, false);

    mcan_enable_interrupts(s_can_bases[ch], MCAN_EVENT_RECEIVE);
    intc_m_enable_irq_with_priority(s_can_irqs[ch], 1);
    ECANT_LOG("CAN%d Enabled.\n", ch);
}

static void check_and_reconfig_can(void)
{
    sanitize_data_control();
}

static void ecant_canfd_init(void)
{
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    mcan_msg_buf_attr_t attr0 = { (uint32_t)s_mcan_msg_buf0, sizeof(s_mcan_msg_buf0) };
    mcan_set_msg_buf_attr(s_can_bases[0], &attr0);
    mcan_msg_buf_attr_t attr1 = { (uint32_t)s_mcan_msg_buf1, sizeof(s_mcan_msg_buf1) };
    mcan_set_msg_buf_attr(s_can_bases[1], &attr1);
    mcan_msg_buf_attr_t attr2 = { (uint32_t)s_mcan_msg_buf2, sizeof(s_mcan_msg_buf2) };
    mcan_set_msg_buf_attr(s_can_bases[2], &attr2);
    mcan_msg_buf_attr_t attr3 = { (uint32_t)s_mcan_msg_buf3, sizeof(s_mcan_msg_buf3) };
    mcan_set_msg_buf_attr(s_can_bases[3], &attr3);
#endif

    s_config_objs[0] = &Can0Config0x8000;
    s_config_objs[1] = (TOBJ8000*)&Can1Config0x8010;
    s_config_objs[2] = (TOBJ8000*)&Can2Config0x8020;
    s_config_objs[3] = (TOBJ8000*)&Can3Config0x8030;
    
    s_status_objs[0] = &Can0Status0x9000;
    s_status_objs[1] = (TOBJ9000*)&Can1Status0x9010;
    s_status_objs[2] = (TOBJ9000*)&Can2Status0x9020;
    s_status_objs[3] = (TOBJ9000*)&Can3Status0x9030;

    register_object_callbacks();

    for (uint8_t ch = 0; ch < 4; ch++) {
        board_init_can(s_can_bases[ch]);
        s_can_clk_hz[ch] = board_init_can_clock(s_can_bases[ch]);
        
        TOBJ8000 *cfg = s_config_objs[ch];
        if (cfg->BitrateArb == 0) {
            cfg->BitrateArb = 1000000;
            cfg->BitrateData = 5000000;
            cfg->FdEnable = 1;
            cfg->Enable = 1;
        }
        cfg->CleanDirtyFrame = (cfg->CleanDirtyFrame != 0U) ? 1U : 0U;
        sanitize_channel_config(ch, NULL);
        apply_can_config(ch);
    }

    s_data_control_shadow.ignore_tx = (DataControl0xA000.IgnoreTx != 0U) ? 1U : 0U;
    s_data_control_shadow.ignore_rx = (DataControl0xA000.IgnoreRx != 0U) ? 1U : 0U;
    sync_shadow_to_data_control_object();

    ECANT_LOG("ECANT CANFD Init OK\n");
}

static void poll_can_tx_status(void)
{
    for (uint8_t ch = 0; ch < 4; ch++) {
        if (!s_canfd_tx_pending[ch]) {
            continue;
        }

        uint32_t fifo_index = s_canfd_tx_fifo_idx[ch];
        if (mcan_is_transmit_occurred(s_can_bases[ch], fifo_index)) {
            ECANT_LOG("CAN%d TX-DONE: ID=0x%lx\n", ch, s_canfd_tx_id[ch]);
            reset_tx_runtime_state(ch, false, false);
        } else if (mcan_is_tx_buf_cancellation_finished(s_can_bases[ch], fifo_index)) {
            ECANT_LOG("CAN%d TX-CANCEL: ID=0x%lx\n", ch, s_canfd_tx_id[ch]);
            reset_tx_runtime_state(ch, false, false);
        } else if (nAlStatus != STATE_OP && !s_canfd_tx_left_op_reported[ch]) {
            print_tx_pending_diag(ch, "TX-PENDING");
            ECANT_LOG("CAN%d TX-PENDING while AL=0x%02x\n", ch, nAlStatus);
            s_canfd_tx_left_op_reported[ch] = true;
        } else {
            uint64_t now_us = get_time_us();
            if (!s_canfd_tx_cancel_requested[ch] && s_canfd_tx_start_us[ch] != 0 &&
                now_us > s_canfd_tx_start_us[ch] && (now_us - s_canfd_tx_start_us[ch]) >= ECANT_TX_PENDING_TIMEOUT_US) {
                print_tx_pending_diag(ch, "TX-TIMEOUT");
                mcan_cancel_tx_buf_send_request(s_can_bases[ch], fifo_index);
                s_canfd_tx_cancel_requested[ch] = true;
                /* Note: trigger seen is not cleared here to maintain toggle state */
            }
        }
    }
}


static void forward_ecat_to_can(void)
{
    TOBJ7000 tx_snapshot[4];

    if (s_data_control_shadow.ignore_tx == 1) return;

    s_tx_frames[0] = &Can0TxFrame0x7000;
    s_tx_frames[1] = (TOBJ7000*)&Can1TxFrame0x7010;
    s_tx_frames[2] = (TOBJ7000*)&Can2TxFrame0x7020;
    s_tx_frames[3] = (TOBJ7000*)&Can3TxFrame0x7030;

    for (uint8_t ch = 0; ch < 4; ch++) {
        memcpy(&tx_snapshot[ch], s_tx_frames[ch], sizeof(TOBJ7000));
    }

    for (uint8_t ch = 0; ch < 4; ch++) {
        TOBJ7000 *tx_obj = &tx_snapshot[ch];
        uint16_t current_trig = tx_obj->SubIndex005;

        if (current_trig == s_canfd_tx_trigger_seen[ch] || current_trig == 0) {
            continue;
        }

        s_canfd_tx_trigger_seen[ch] = current_trig;

        mcan_tx_frame_t tx;
        uint32_t fifo_index = 0;
        memset(&tx, 0, sizeof(tx));
        uint32_t dlc = tx_obj->Dlc;
        if (dlc == 0 || dlc > 15) continue;
        tx.use_ext_id = (tx_obj->CanIdHi & 0x2000) ? 1 : 0;
        if (tx.use_ext_id) {
            tx.ext_id = ((uint32_t)(tx_obj->CanIdHi & 0x1FFF) << 16) | tx_obj->CanIdLo;
        } else {
            tx.std_id = tx_obj->CanIdLo & 0x7FF;
        }
        tx.dlc = tx_obj->Dlc;
        tx.canfd_frame = (tx_obj->Flags & 0x01) ? 1 : 0;
        tx.bitrate_switch = (tx_obj->Flags & 0x02) ? 1 : 0;
        tx.error_state_indicator = 0;
        apply_can2_tx_frame_ab_override(ch, &tx);
        
        uint32_t len = mcan_get_message_size_from_dlc(tx.dlc);
        memcpy(tx.data_8, (const void *)tx_obj->Payload1, (len > 64) ? 64 : len);
        
        ECANT_LOG("CAN%d TX-SUBMIT: CCCR=0x%08lx PSR=0x%08lx ECR=0x%08lx TXFQS=0x%08lx ID=0x%lx DLC=%u FD=%u BRS=%u\n",
                  ch,
                  (unsigned long)s_can_bases[ch]->CCCR,
                  (unsigned long)s_can_bases[ch]->PSR,
                  (unsigned long)s_can_bases[ch]->ECR,
                  (unsigned long)s_can_bases[ch]->TXFQS,
                  (uint32_t)(tx.use_ext_id ? tx.ext_id : tx.std_id),
                  tx.dlc,
                  tx.canfd_frame,
                  tx.bitrate_switch);
        
        hpm_stat_t stat = mcan_transmit_via_txfifo_nonblocking(s_can_bases[ch], &tx, &fifo_index);
        
        ECANT_LOG("ECAT -> CAN%d: ID=0x%lx, Stat=%ld\n", ch, (uint32_t)(tx.use_ext_id ? tx.ext_id : tx.std_id), (long)stat);

        if (stat == status_success) {
            s_canfd_tx_pending[ch] = true;
            s_canfd_tx_fifo_idx[ch] = fifo_index;
            s_canfd_tx_id[ch] = (uint32_t)(tx.use_ext_id ? tx.ext_id : tx.std_id);
            s_canfd_tx_dlc[ch] = tx.dlc;
            s_canfd_tx_left_op_reported[ch] = false;
            s_canfd_tx_cancel_requested[ch] = false;
            s_canfd_tx_start_us[ch] = get_time_us();
            s_can_led_ticks[ch] = ECANT_LED_ON_TICKS;
        } else {
            ECANT_LOG("CAN%d TX-SUBMIT-FAIL: ID=0x%lx, DLC=%u, Stat=%ld\n",
                   ch,
                   (uint32_t)(tx.use_ext_id ? tx.ext_id : tx.std_id),
                   tx.dlc,
                   (long)stat);
        }
    }
}

static void forward_can_to_ecat(void)
{
    static uint16_t s_rx_seq[4] = {0, 0, 0, 0};

    if (s_data_control_shadow.ignore_rx == 1) return;

    s_rx_frames[0] = &Can0RxFrame0x6000;
    s_rx_frames[1] = (TOBJ6000*)&Can1RxFrame0x6010;
    s_rx_frames[2] = (TOBJ6000*)&Can2RxFrame0x6020;
    s_rx_frames[3] = (TOBJ6000*)&Can3RxFrame0x6030;

    bool alive = (nAlStatus == STATE_SAFEOP || nAlStatus == STATE_OP);

    for (uint8_t ch = 0; ch < 4; ch++) {
        mcan_rx_message_t rx_msg;
        if (status_success != mcan_read_rxfifo(s_can_bases[ch], 0, &rx_msg)) continue;

        if (!alive) continue;
        uint32_t id = rx_msg.use_ext_id ? rx_msg.ext_id : rx_msg.std_id;
        uint32_t len = mcan_get_message_size_from_dlc(rx_msg.dlc);

        TOBJ6000 *rx_obj = s_rx_frames[ch];

        s_rx_seq[ch]++;
        if (s_rx_seq[ch] == 0) s_rx_seq[ch] = 1;
        rx_obj->SubIndex005 = s_rx_seq[ch];

        if (rx_msg.use_ext_id) {
            rx_obj->CanIdLo = rx_msg.ext_id & 0xFFFF;
            rx_obj->CanIdHi = (rx_msg.ext_id >> 16) | 0x2000;
        } else {
            rx_obj->CanIdLo = rx_msg.std_id;
            rx_obj->CanIdHi = 0;
        }
        rx_obj->Dlc = rx_msg.dlc;
        rx_obj->Flags = (rx_msg.canfd_frame ? 0x01 : 0) |
                                 (rx_msg.bitrate_switch ? 0x02 : 0);
        
        memcpy(rx_obj->Payload1, (const void *)rx_msg.data_8, (len > 64) ? 64 : len);
        s_canfd_rx_polled[ch]++;
        s_can_led_ticks[ch] = ECANT_LED_ON_TICKS;
        ECANT_LOG("rx=%lu\n", (unsigned long)s_canfd_rx_polled[ch]);
    }
}

static void sync_can_status_to_sdo(void)
{
    for (uint8_t ch = 0; ch < 4; ch++) {
        TOBJ9000 *status = s_status_objs[ch];
        mcan_error_count_t err_count;
        mcan_get_error_counter(s_can_bases[ch], &err_count);
        
        status->Tec = err_count.transmit_error_count;
        status->Rec = err_count.receive_error_count;
        
        mcan_protocol_status_t psr;
        if (mcan_get_protocol_status(s_can_bases[ch], &psr) == status_success) {
            if (psr.in_bus_off_state) status->BusState = 2;
            else if (psr.in_error_passive_state) status->BusState = 1;
            else status->BusState = 0;
        }
    }
}

void ecat_run(void)
{
    board_init_ethercat(HPM_ESC);
    board_init_switch_led();
    ECANT_LOG("\n--- ECANT Gateway ---\n");

    if (ecat_hardware_init(HPM_ESC) != status_success) return;

    MainInit();
    
    install_isr(BOARD_CAN0_IRQn, (uint32_t)ecat_can0_isr);
    install_isr(BOARD_CAN1_IRQn, (uint32_t)ecat_can1_isr);
    install_isr(BOARD_CAN2_IRQn, (uint32_t)ecat_can2_isr);
    install_isr(BOARD_CAN3_IRQn, (uint32_t)ecat_can3_isr);
    ecant_canfd_init();
    
    APPL_GenerateMapping(&nPdInputSize, &nPdOutputSize);
    bRunApplication = TRUE;

    EcatWdValue = 0;

    uint16_t last_status = 0;
    while (bRunApplication) {
        check_and_reconfig_can();
        poll_can_tx_status();
        forward_can_to_ecat();
        if (nAlStatus == STATE_SAFEOP || nAlStatus == STATE_OP) {
            PDO_OutputMapping();
            PDO_InputMapping();
        }
        MainLoop();
        EcatWdValue = 0;
        forward_ecat_to_can();
        sync_can_status_to_sdo();

        for (uint8_t lch = 0; lch < 4; lch++) {
            if (s_can_led_ticks[lch] > 0) {
                s_can_led_ticks[lch]--;
                board_led_can_on(lch);
            } else {
                board_led_can_off(lch);
            }
        }
        if (nAlStatus == STATE_OP) {
            board_led_mode_on();
        } else {
            board_led_mode_off();
        }

        s_ecat_usb_led_tick++;
        if (nAlStatus == STATE_OP) {
            board_led_usb_on();
        } else if (nAlStatus == STATE_PREOP) {
            if (s_ecat_usb_led_tick >= 500U) {
                s_ecat_usb_led_tick = 0;
                board_led_usb_on();
            } else if (s_ecat_usb_led_tick >= 250U) {
                board_led_usb_off();
            }
        } else if (nAlStatus == STATE_SAFEOP) {
            if (s_ecat_usb_led_tick >= 200U) {
                s_ecat_usb_led_tick = 0;
                board_led_usb_on();
            } else if (s_ecat_usb_led_tick >= 100U) {
                board_led_usb_off();
            }
        } else {
            board_led_usb_off();
            s_ecat_usb_led_tick = 0;
        }

        if (nAlStatus != last_status) {
            if (last_status == STATE_OP && nAlStatus != STATE_OP) {
                for (uint8_t ch = 0; ch < 4; ch++) cancel_all_hw_tx_buffers(ch);
                reset_all_tx_runtime_state(false, true);
            } else if (nAlStatus == STATE_OP && last_status != STATE_OP) {
                for (uint8_t ch = 0; ch < 4; ch++) {
                    cancel_all_hw_tx_buffers(ch);
                    s_canfd_rx_head[ch] = s_canfd_rx_tail[ch];
                }
                HW_EscWriteDWord(0, ESC_PD_WD_TIME);
            }
            ECANT_LOG("AL Status: 0x%02x rx=%lu\n", nAlStatus, (unsigned long)s_canfd_rx_polled[0]);
            last_status = nAlStatus;
        }
    }
}
