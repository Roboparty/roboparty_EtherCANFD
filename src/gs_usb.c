// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 wentywenty

/*
 * gs_usb (CAN FD to USB) Firmware for HPMicro
 * Based on CherryUSB
 */

#include <stdio.h>
#include <string.h>
#include "board.h"
#include "hpm_csr_drv.h"
#include "hpm_sysctl_drv.h"
#include "hpm_mcan_drv.h"
#include "version.h"
#include "hpm_clock_drv.h"
#include "hpm_ewdg_drv.h"
#include "hpm_interrupt.h"
#include "hpm_usb_regs.h"
#include "usbd_core.h"
#include "usb_config.h"

extern void hpm_isr_usb0(void);
extern void USBD_IRQHandler(uint8_t busid);

#define GS_USB_VID 0x1209
#define GS_USB_PID 0x2323
#define WBVAL(x) ((x) & 0xFF), (((x) >> 8) & 0xFF)

#define GS_USB_LOG_ENABLE 0
#if GS_USB_LOG_ENABLE
#define GS_USB_LOG(fmt, ...) CONFIG_USB_PRINTF(fmt, ##__VA_ARGS__)
#else
#define GS_USB_LOG(fmt, ...) do { } while (0)
#endif


/* gs_usb vendor requests */
#define GS_USB_LED_ON_TICKS      500U
#define GS_USB_MODE_LED_INTERVAL 500000U

#define GS_USB_BREQ_HOST_FORMAT    0
#define GS_USB_BREQ_BITTIMING      1
#define GS_USB_BREQ_MODE           2
#define GS_USB_BREQ_BT_CONST       3
#define GS_USB_BREQ_DEVICE_CONFIG  4
#define GS_USB_BREQ_TIMESTAMP      5
#define GS_USB_BREQ_IDENTIFY       6
#define GS_USB_BREQ_DATA_BITTIMING 8
#define GS_USB_BREQ_BT_CONST_EXT   9

/* endpoints */
#define GS_USB_EP_OUT 0x02
#define GS_USB_EP_IN  0x81

#ifdef CONFIG_USB_HS
#define GS_USB_MAX_PACKET_SIZE 512
#else
#define GS_USB_MAX_PACKET_SIZE 64
#endif

#if !defined(USB_MAX_EP0_SIZE) || (USB_MAX_EP0_SIZE != 64)
#error "USB_MAX_EP0_SIZE must be 64 for USB HS enumeration"
#endif

/* gs_usb flags and constants */
#define GS_CAN_FLAG_OVERFLOW 1
#define GS_CAN_FLAG_FD       2
#define GS_CAN_FLAG_BRS      4
#define GS_CAN_FLAG_ESI      8

/* gs_usb mode flags (aligned to Linux CAN ctrlmode flags) */
#define GS_CAN_MODE_LISTEN_ONLY    0x0001
#define GS_CAN_MODE_LOOP_BACK      0x0002
#define GS_CAN_MODE_ONE_SHOT       0x0004
#define GS_CAN_MODE_TRIPLE_SAMPLE  0x0008
#define GS_CAN_MODE_FD             0x0100

/* CAN error frame CAN_ID flags */
#define CAN_ERR_FLAG (0x20000000U) 
#define CAN_ERR_CRTL         0x00000004U
#define CAN_ERR_BUSOFF       0x00000040U
#define CAN_ERR_RESTARTED    0x00000100U

/* error status of CAN-controller / data[1] */
#define CAN_ERR_CRTL_RX_WARNING  0x04
#define CAN_ERR_CRTL_TX_WARNING  0x08
#define CAN_ERR_CRTL_RX_PASSIVE  0x10
#define CAN_ERR_CRTL_TX_PASSIVE  0x20

#define GS_CAN_FEATURE_LISTEN_ONLY      (1<<0)
#define GS_CAN_FEATURE_LOOP_BACK        (1<<1)
#define GS_CAN_FEATURE_TRIPLE_SAMPLE    (1<<2)
#define GS_CAN_FEATURE_ONE_SHOT         (1<<3)
#define GS_CAN_FEATURE_HW_TIMESTAMP     (1<<4)
#define GS_CAN_FEATURE_IDENTIFY         (1<<5)
#define GS_CAN_FEATURE_USER_ID          (1<<6)
#define GS_CAN_FEATURE_PAD_FRAME        (1<<7)
#define GS_CAN_FEATURE_FD               (1<<8)
#define GS_CAN_FEATURE_BT_CONST_EXT     (1<<10)

/* Default the controller to one-shot (no auto-retransmission) even when the
 * host does not request it. Auto-retransmission of stale motion commands is
 * undesirable: on an E-stop power cut the bus loses all receivers, the
 * controller becomes error-passive (the single-node ACK error exception keeps
 * TEC from reaching bus-off), and auto-retransmission jams the TX FIFO until
 * the ring overflows and the host hits ENOBUFS. Set to 0 to honor only the
 * host one-shot request. */
#ifndef CONFIG_GS_USB_DEFAULT_ONE_SHOT
#define CONFIG_GS_USB_DEFAULT_ONE_SHOT 1
#endif

/* gs_usb data structures */
struct gs_host_config {
    uint32_t byte_order;
} __attribute__((packed));

struct gs_device_config {
    uint8_t reserved1;
    uint8_t reserved2;
    uint8_t reserved3;
    uint8_t icount;
    uint32_t sw_version;
    uint32_t hw_version;
    uint32_t reserved[7];
} __attribute__((packed));

struct gs_device_bt_const {
    uint32_t feature;
    uint32_t fclk_can;
    uint32_t tseg1_min;
    uint32_t tseg1_max;
    uint32_t tseg2_min;
    uint32_t tseg2_max;
    uint32_t sjw_max;
    uint32_t brp_min;
    uint32_t brp_max;
    uint32_t brp_inc;
} __attribute__((packed));

struct gs_device_bittiming {
    uint32_t prop_seg;
    uint32_t phase_seg1;
    uint32_t phase_seg2;
    uint32_t sjw;
    uint32_t brp;
} __attribute__((packed));

struct gs_device_mode {
    uint32_t mode;
    uint32_t flags;
} __attribute__((packed));

struct gs_device_timestamp {
    uint32_t ts_low;
    uint32_t ts_high;
    uint32_t flags;
} __attribute__((packed));

/* USB EP buffers */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) uint8_t ep_out_buffer[GS_USB_MAX_PACKET_SIZE];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) uint8_t ep_in_buffer[GS_USB_MAX_PACKET_SIZE];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) uint8_t ep_in_dma_buffer[GS_USB_MAX_PACKET_SIZE];

/* Setup Request variables (MUST be in non-cacheable RAM for USB DMA) */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX struct gs_device_config dev_config;
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX struct gs_device_bt_const bt_const;

/* USB descriptors (advanced callback mode, explicit byte layout for EP0 diagnostics) */
#define USB_NOCACHE_RAM_INIT_SECTION __attribute__((section(".rodata")))

#ifndef FW_BCD_DEVICE
#define FW_BCD_DEVICE 0x0100
#endif

USB_NOCACHE_RAM_INIT_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) uint8_t gs_usb_device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_1, 0x00, 0x00, 0x00, GS_USB_VID, GS_USB_PID, FW_BCD_DEVICE, 0x01),
};

USB_NOCACHE_RAM_INIT_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) uint8_t gs_usb_config_descriptor_hs[] = {
    /* Configuration Descriptor */
    0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0xFA,
    /* Interface Descriptor */
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0xFF, 0xFF, 0x00,
    /* Endpoint IN Descriptor */
    0x07, 0x05, GS_USB_EP_IN, 0x02, WBVAL(512), 0x00,
    /* Endpoint OUT Descriptor */
    0x07, 0x05, GS_USB_EP_OUT, 0x02, WBVAL(512), 0x00,
};

USB_NOCACHE_RAM_INIT_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) uint8_t gs_usb_config_descriptor_fs[] = {
    /* Configuration Descriptor */
    0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0xFA,
    /* Interface Descriptor */
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0xFF, 0xFF, 0x00,
    /* Endpoint IN Descriptor */
    0x07, 0x05, GS_USB_EP_IN, 0x02, WBVAL(64), 0x00,
    /* Endpoint OUT Descriptor */
    0x07, 0x05, GS_USB_EP_OUT, 0x02, WBVAL(64), 0x00,
};

USB_NOCACHE_RAM_INIT_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) uint8_t gs_usb_device_quality_descriptor[] = {
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, 0x01),
};

USB_NOCACHE_RAM_INIT_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) uint8_t gs_usb_other_speed_config_descriptor_hs[] = {
    /* Other Speed Configuration Descriptor */
    0x09, 0x07, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0xFA,
    /* Interface Descriptor */
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0xFF, 0xFF, 0x00,
    /* Endpoint IN Descriptor (FS packet size) */
    0x07, 0x05, GS_USB_EP_IN, 0x02, WBVAL(64), 0x00,
    /* Endpoint OUT Descriptor (FS packet size) */
    0x07, 0x05, GS_USB_EP_OUT, 0x02, WBVAL(64), 0x00,
};

USB_NOCACHE_RAM_INIT_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) uint8_t gs_usb_other_speed_config_descriptor_fs[] = {
    /* Other Speed Configuration Descriptor */
    0x09, 0x07, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0xFA,
    /* Interface Descriptor */
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0xFF, 0xFF, 0x00,
    /* Endpoint IN Descriptor (HS packet size) */
    0x07, 0x05, GS_USB_EP_IN, 0x02, WBVAL(512), 0x00,
    /* Endpoint OUT Descriptor (HS packet size) */
    0x07, 0x05, GS_USB_EP_OUT, 0x02, WBVAL(512), 0x00,
};

USB_NOCACHE_RAM_INIT_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) char gs_usb_string_langid[] = { 0x09, 0x04 };
USB_NOCACHE_RAM_INIT_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) char gs_usb_string_manufacturer[] = "wentywenty";
USB_NOCACHE_RAM_INIT_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) char gs_usb_string_product[] = "roboto_usb4can v" FIRMWARE_VERSION;
USB_NOCACHE_RAM_INIT_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) char gs_usb_string_serial[] = "202604070001";

static const char *gs_usb_string_descriptors[] = {
    gs_usb_string_langid,
    gs_usb_string_manufacturer,
    gs_usb_string_product,
    gs_usb_string_serial,
};

static const uint8_t *gs_usb_device_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return gs_usb_device_descriptor;
}

static const uint8_t *gs_usb_config_descriptor_callback(uint8_t speed)
{
    if (speed == USB_SPEED_HIGH) {
        return gs_usb_config_descriptor_hs;
    }
    if (speed == USB_SPEED_FULL) {
        return gs_usb_config_descriptor_fs;
    }
    return NULL;
}

static const uint8_t *gs_usb_device_quality_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return gs_usb_device_quality_descriptor;
}

static const uint8_t *gs_usb_other_speed_descriptor_callback(uint8_t speed)
{
    if (speed == USB_SPEED_HIGH) {
        return gs_usb_other_speed_config_descriptor_hs;
    }
    if (speed == USB_SPEED_FULL) {
        return gs_usb_other_speed_config_descriptor_fs;
    }
    return NULL;
}

static const char *gs_usb_string_descriptor_callback(uint8_t speed, uint8_t index)
{
    (void)speed;
    if (index >= (sizeof(gs_usb_string_descriptors) / sizeof(gs_usb_string_descriptors[0]))) {
        return NULL;
    }
    return (const char *)gs_usb_string_descriptors[index];
}

#define GS_USB_CAN_CHANNEL_COUNT 4U
#define TX_RING_SIZE_PER_CH      128U
#define TX_RING_HIGH_WATERMARK   ((TX_RING_SIZE_PER_CH * 9U) / 10U)
#define TX_RING_LOW_WATERMARK    ((TX_RING_SIZE_PER_CH * 3U) / 10U)
#define GS_USB_TX_TIMEOUT_US     20000U
#define GS_USB_EP_IN_TIMEOUT_US  500000U /* Recover an IN transfer with no completion after 500 ms */
#define GS_USB_EP_IN_FLUSH_TIMEOUT_US 2000U /* Never wait forever for ENDPTFLUSH */
#define GS_USB_EP_IN_RECOVERY_COOLDOWN_US 5000000U
#define GS_USB_EP_IN_RECOVERY_MAX_STREAK 2U
#define GS_USB_RX_DRAIN_LIMIT    32U     /* Bound RX work per channel/main-loop pass */
#define GS_USB_TX_EVT_DRAIN_LIMIT 32U    /* Bound TX event work per channel/pass */
#define GS_USB_BUSOFF_RECOVERY_DELAY_MS 100U  /* Delay before bus-off recovery attempt */
#define GS_USB_BUSOFF_RECOVERY_COOLDOWN_MS 1000U /* Minimum interval between reinit attempts */
#define GS_USB_ERROR_PASSIVE_RECOVERY_MS 5000U /* Recover only if passive persists */

struct gs_can_channel {
    MCAN_Type *base;
    mcan_config_t config; // 保存当前的通道波特率设定
    bool is_open;
    uint8_t error_state; // 记录上一次的错误状态 0: active, 1: warning, 2: passive, 3: bus-off
    bool have_can_timing;
    bool have_canfd_timing;
    uint32_t mode_flags;
    uint64_t busoff_detect_us; // bus-off 检测时间戳，0 表示未处于 bus-off
    uint64_t busoff_recovery_us; // 上次恢复尝试时间戳，用于冷却限频
    uint64_t error_passive_detect_us;
};

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
extern uint32_t s_mcan_msg_buf0[MCAN_MSG_BUF_SIZE_IN_WORDS];
extern uint32_t s_mcan_msg_buf1[MCAN_MSG_BUF_SIZE_IN_WORDS];
extern uint32_t s_mcan_msg_buf2[MCAN_MSG_BUF_SIZE_IN_WORDS];
extern uint32_t s_mcan_msg_buf3[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif

static struct gs_can_channel mcan_channels[4] = {
    { .base = HPM_MCAN0, .is_open = false, .error_state = 0, .have_can_timing = false, .have_canfd_timing = false, .mode_flags = 0, .busoff_detect_us = 0, .busoff_recovery_us = 0, .error_passive_detect_us = 0 },
    { .base = HPM_MCAN1, .is_open = false, .error_state = 0, .have_can_timing = false, .have_canfd_timing = false, .mode_flags = 0, .busoff_detect_us = 0, .busoff_recovery_us = 0, .error_passive_detect_us = 0 },
    { .base = HPM_MCAN2, .is_open = false, .error_state = 0, .have_can_timing = false, .have_canfd_timing = false, .mode_flags = 0, .busoff_detect_us = 0, .busoff_recovery_us = 0, .error_passive_detect_us = 0 },
    { .base = HPM_MCAN3, .is_open = false, .error_state = 0, .have_can_timing = false, .have_canfd_timing = false, .mode_flags = 0, .busoff_detect_us = 0, .busoff_recovery_us = 0, .error_passive_detect_us = 0 }
};

static const uint32_t mcan_irqs[GS_USB_CAN_CHANNEL_COUNT] = {
    BOARD_CAN0_IRQn,
    BOARD_CAN1_IRQn,
    BOARD_CAN2_IRQn,
    BOARD_CAN3_IRQn
};

/* 主机用于发给设备或者从设备接收报文的帧格式 */
struct gs_host_frame {
    uint32_t echo_id;
    uint32_t can_id;
    uint8_t can_dlc;
    uint8_t channel;
    uint8_t flags;
    uint8_t reserved;
    uint8_t data[64]; // 高配为支持FD设置64字节宽度
} __attribute__((packed));

struct tx_ring_item {
    struct gs_host_frame host_frame;
    uint32_t frame_len;
    bool loopback;
};

struct tx_ring {
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint16_t count;
    struct tx_ring_item items[TX_RING_SIZE_PER_CH];
};

struct can_tx_state {
    volatile bool busy;
    volatile uint32_t fifo_index;
    volatile uint64_t start_us;
    bool inflight_valid;
    struct tx_ring_item inflight_item;
    uint8_t marker;
};

volatile bool ep_in_busy = false;
volatile uint64_t ep_in_start_us = 0U;
volatile bool ep_in_timeout_reported = false;
volatile uint32_t ep_in_submit_fail_count = 0U;
volatile uint32_t ep_in_recovery_count = 0U;
static uint64_t ep_in_last_recovery_us = 0U;
static uint8_t ep_in_recovery_streak = 0U;
static bool ep_in_persistent_reported = false;
volatile uint32_t out_count = 0;

uint32_t gs_usb_can_clk = 80000000; // Will be runtime detected

volatile uint32_t out_drop = 0;
static uint32_t gs_usb_tx_debug_count = 0;
volatile bool gs_usb_configured = false;
volatile uint32_t gs_usb_reset_count = 0;
volatile uint32_t gs_usb_configured_count = 0;
volatile uint32_t gs_usb_connected_count = 0;
volatile uint32_t gs_usb_disconnected_count = 0;
volatile bool gs_usb_is_connected = false;
volatile uint32_t gs_usb_suspend_count = 0;
volatile uint32_t gs_usb_resume_count = 0;
volatile bool gs_usb_out_paused = false;
volatile bool gs_usb_out_armed = false;
volatile uint32_t gs_usb_main_loop_count = 0U;
volatile uint32_t gs_usb_poll_irq_count = 0U;
volatile uint8_t gs_usb_main_stage = 0U;
volatile uint32_t gs_usb_can_irq_count[GS_USB_CAN_CHANNEL_COUNT] = {0U};

static uint16_t s_gs_can_led_ticks[GS_USB_CAN_CHANNEL_COUNT];
static uint32_t s_gs_mode_led_tick;
static uint32_t s_gs_usb_led_tick;
static bool s_gs_mode_led_state;

static struct gs_host_config gs_host_cfg = { .byte_order = 0 };
static struct gs_device_timestamp gs_timestamp = { 0, 0, 0 };

#define IN_QUEUE_SIZE 64
struct in_msg {
    uint8_t data[76];
    uint32_t len;
};

static struct in_msg in_queue[IN_QUEUE_SIZE];
static volatile uint32_t in_q_head = 0;
static volatile uint32_t in_q_tail = 0;
static struct tx_ring tx_rings[GS_USB_CAN_CHANNEL_COUNT];
static struct can_tx_state can_tx_states[GS_USB_CAN_CHANNEL_COUNT];
static uint8_t tx_marker_seq = 1U;

static void gs_usb_kick_can_tx(uint8_t ch);
static void gs_usb_resume_out_if_possible(void);
static void gs_usb_service_tx_timeouts(void);
static void gs_usb_service_tx_complete_poll(void);
static void gs_usb_recover_channel(uint8_t ch);

static uint32_t gs_usb_irq_save(void)
{
    return disable_global_irq(CSR_MSTATUS_MIE_MASK);
}

static void gs_usb_irq_restore(uint32_t level)
{
    if ((level & CSR_MSTATUS_MIE_MASK) != 0U) {
        restore_global_irq(CSR_MSTATUS_MIE_MASK);
    }
}

static bool enqueue_in_msg(const void *data, uint32_t len)
{
    uint32_t level = gs_usb_irq_save();
    uint32_t next = (in_q_head + 1U) % IN_QUEUE_SIZE;
    if (len == 0U || len > sizeof(in_queue[0].data)) {
        gs_usb_irq_restore(level);
        return false;
    }
    if (next == in_q_tail) {
        gs_usb_irq_restore(level);
        return false;
    }
    memcpy(in_queue[in_q_head].data, data, len);
    in_queue[in_q_head].len = len;
    in_q_head = next;
    gs_usb_irq_restore(level);
    return true;
}

static uint64_t gs_usb_get_time_us(void)
{
    uint64_t cycles;

    if (hpm_core_clock == 0U) {
        return 0U;
    }

    cycles = hpm_csr_get_core_mcycle();
    return (cycles * 1000000ULL) / hpm_core_clock;
}

static bool gs_usb_recover_ep_in(uint64_t now_us)
{
    USB_Type *usb = (USB_Type *)HPM_USB0;
    const uint32_t ep_mask = 1UL << (16U + (GS_USB_EP_IN & 0x0FU));
    uint64_t flush_start_us;
    uint32_t prime_before;
    uint32_t stat_before;
    uint32_t complete_before;
    uint32_t complete_after;
    bool flush_ok = false;

    if (ep_in_last_recovery_us != 0U &&
        (now_us - ep_in_last_recovery_us) < GS_USB_EP_IN_RECOVERY_COOLDOWN_US) {
        return false;
    }

    if (ep_in_recovery_streak >= GS_USB_EP_IN_RECOVERY_MAX_STREAK) {
        if (!ep_in_persistent_reported) {
            ep_in_persistent_reported = true;
            printf("[GS_USB] EP_IN persistent stall; automatic recovery stopped\r\n");
        }
        return false;
    }

    /* Serialize against the normal CherryUSB ISR.  A completion may arrive
     * between the timeout check and masking the IRQ, so recheck it here. */
    intc_m_disable_irq(CONFIG_HPM_USBD_IRQn);
    if (!ep_in_busy || (usb->ENDPTCOMPLETE & ep_mask) != 0U) {
        intc_m_enable_irq(CONFIG_HPM_USBD_IRQn);
        return false;
    }

    prime_before = usb->ENDPTPRIME;
    stat_before = usb->ENDPTSTAT;
    complete_before = usb->ENDPTCOMPLETE;
    ep_in_recovery_count++;
    ep_in_recovery_streak++;
    /* Flush only EP1 IN.  The SDK close path waits forever; use the same
     * hardware sequence with a hard deadline and leave the endpoint enabled
     * so its data toggle remains synchronized with the host. */
    flush_start_us = gs_usb_get_time_us();
    do {
        usb->ENDPTFLUSH = ep_mask;
        while ((usb->ENDPTFLUSH & ep_mask) != 0U &&
               (gs_usb_get_time_us() - flush_start_us) < GS_USB_EP_IN_FLUSH_TIMEOUT_US) {
        }
        if ((usb->ENDPTFLUSH & ep_mask) != 0U) {
            break;
        }
        if ((usb->ENDPTSTAT & ep_mask) == 0U) {
            flush_ok = true;
            break;
        }
    } while ((gs_usb_get_time_us() - flush_start_us) < GS_USB_EP_IN_FLUSH_TIMEOUT_US);

    complete_after = usb->ENDPTCOMPLETE;
    ep_in_last_recovery_us = now_us;
    if (flush_ok && (complete_after & ep_mask) == 0U) {
        ep_in_busy = false;
        ep_in_start_us = 0U;
        ep_in_timeout_reported = false;
    } else if ((complete_after & ep_mask) != 0U) {
        /* The transfer won the race with FLUSH.  Keep the QTD and queue
         * ownership intact; the pending normal USB IRQ will complete it. */
        flush_ok = false;
    }
    intc_m_enable_irq(CONFIG_HPM_USBD_IRQn);

    printf("[GS_USB] EP_IN recover #%lu before: PRIME=0x%08lx STAT=0x%08lx COMPLETE=0x%08lx RXQ=%lu\r\n",
           (unsigned long)ep_in_recovery_count,
           (unsigned long)prime_before,
           (unsigned long)stat_before,
           (unsigned long)complete_before,
           (unsigned long)((in_q_head + IN_QUEUE_SIZE - in_q_tail) % IN_QUEUE_SIZE));
    printf("[GS_USB] EP_IN recover #%lu %s after: FLUSH=0x%08lx STAT=0x%08lx COMPLETE=0x%08lx\r\n",
           (unsigned long)ep_in_recovery_count,
           flush_ok ? "OK" : (((complete_after & ep_mask) != 0U) ? "COMPLETED" : "FAILED"),
           (unsigned long)usb->ENDPTFLUSH,
           (unsigned long)usb->ENDPTSTAT,
           (unsigned long)usb->ENDPTCOMPLETE);
    return flush_ok;
}

static bool tx_ring_push(uint8_t ch, const struct tx_ring_item *item)
{
    struct tx_ring *ring = &tx_rings[ch];
    uint32_t level = gs_usb_irq_save();
    uint16_t head;

    if (ring->count >= TX_RING_SIZE_PER_CH) {
        gs_usb_irq_restore(level);
        return false;
    }

    head = ring->head;
    ring->items[head] = *item;
    ring->head = (uint16_t)((head + 1U) % TX_RING_SIZE_PER_CH);
    ring->count++;
    gs_usb_irq_restore(level);
    return true;
}

static bool tx_ring_pop(uint8_t ch, struct tx_ring_item *item)
{
    struct tx_ring *ring = &tx_rings[ch];
    uint32_t level = gs_usb_irq_save();
    uint16_t tail;

    if (ring->count == 0U) {
        gs_usb_irq_restore(level);
        return false;
    }

    tail = ring->tail;
    *item = ring->items[tail];
    ring->tail = (uint16_t)((tail + 1U) % TX_RING_SIZE_PER_CH);
    ring->count--;
    gs_usb_irq_restore(level);
    return true;
}

static uint16_t tx_ring_count(uint8_t ch)
{
    uint32_t level = gs_usb_irq_save();
    uint16_t count = tx_rings[ch].count;
    gs_usb_irq_restore(level);
    return count;
}

static bool gs_usb_should_pause_out(void)
{
    uint8_t ch;

    for (ch = 0; ch < GS_USB_CAN_CHANNEL_COUNT; ch++) {
        if (tx_ring_count(ch) >= TX_RING_HIGH_WATERMARK) {
            return true;
        }
    }
    return false;
}

static bool gs_usb_can_resume_out(void)
{
    uint8_t ch;

    for (ch = 0; ch < GS_USB_CAN_CHANNEL_COUNT; ch++) {
        if (tx_ring_count(ch) > TX_RING_LOW_WATERMARK) {
            return false;
        }
    }
    return true;
}

static void gs_usb_pause_out(void)
{
    gs_usb_out_paused = true;
    gs_usb_out_armed = false;
}

static void gs_usb_resume_out_if_possible(void)
{
    if (!gs_usb_configured || !gs_usb_out_paused || !gs_usb_can_resume_out()) {
        return;
    }

    gs_usb_out_paused = false;
    gs_usb_out_armed = true;
    usbd_ep_start_read(0, GS_USB_EP_OUT, ep_out_buffer, GS_USB_MAX_PACKET_SIZE);
}

static uint8_t gs_usb_get_channel_error_state(uint8_t ch)
{
    mcan_protocol_status_t psr;

    if (mcan_get_protocol_status(mcan_channels[ch].base, &psr) != status_success) {
        return 0;
    }
    if (psr.in_bus_off_state) {
        return 3;
    }
    if (psr.in_error_passive_state) {
        return 2;
    }
    if (psr.in_warning_state) {
        return 1;
    }
    return 0;
}

static void gs_usb_flush_tx_ring(uint8_t ch)
{
    struct tx_ring_item item;

    while (tx_ring_pop(ch, &item)) {
        (void)enqueue_in_msg(&item.host_frame, item.frame_len);
    }
}

static void gs_usb_finish_tx(uint8_t ch, bool success)
{
    if (ch >= GS_USB_CAN_CHANNEL_COUNT) {
        return;
    }

    if (can_tx_states[ch].fifo_index < 32U) {
        mcan_disable_txbuf_transmission_interrupt(mcan_channels[ch].base,
                                                  (1UL << can_tx_states[ch].fifo_index));
        mcan_disable_txbuf_cancel_finish_interrupt(mcan_channels[ch].base,
                                                   (1UL << can_tx_states[ch].fifo_index));
    }

    if (can_tx_states[ch].inflight_valid) {
        (void)enqueue_in_msg(&can_tx_states[ch].inflight_item.host_frame,
                             can_tx_states[ch].inflight_item.frame_len);
        if (success && can_tx_states[ch].inflight_item.loopback) {
            struct gs_host_frame rx_echo;
            memcpy(&rx_echo, &can_tx_states[ch].inflight_item.host_frame, can_tx_states[ch].inflight_item.frame_len);
            rx_echo.echo_id = 0xFFFFFFFFU;
            (void)enqueue_in_msg(&rx_echo, can_tx_states[ch].inflight_item.frame_len);
        }
    }

    can_tx_states[ch].busy = false;
    can_tx_states[ch].fifo_index = 0;
    can_tx_states[ch].start_us = 0U;
    can_tx_states[ch].marker = 0U;
    can_tx_states[ch].inflight_valid = false;

    gs_usb_kick_can_tx(ch);
    gs_usb_resume_out_if_possible();
}

static void gs_usb_kick_can_tx(uint8_t ch)
{
    struct tx_ring_item item;
    mcan_tx_frame_t tx_frame = {0};
    uint32_t fifo_index = 0;
    hpm_stat_t tx_stat;

    if (ch >= GS_USB_CAN_CHANNEL_COUNT || can_tx_states[ch].busy || !mcan_channels[ch].is_open) {
        return;
    }

    if (!tx_ring_pop(ch, &item)) {
        return;
    }

    if ((item.host_frame.can_id & CAN_ERR_FLAG) == 0U) {
        if (item.host_frame.can_id & 0x80000000U) {
            tx_frame.use_ext_id = 1;
            tx_frame.ext_id = item.host_frame.can_id & 0x1FFFFFFFU;
        } else {
            tx_frame.std_id = item.host_frame.can_id & 0x7FFU;
        }
        if ((item.host_frame.can_id & 0x40000000U) != 0U) {
            tx_frame.rtr = 1;
        }

        tx_frame.dlc = item.host_frame.can_dlc;
        if ((item.host_frame.flags & GS_CAN_FLAG_FD) != 0U) {
            tx_frame.canfd_frame = 1;
        }
        if ((item.host_frame.flags & GS_CAN_FLAG_BRS) != 0U) {
            tx_frame.bitrate_switch = 1;
        }
        tx_frame.event_fifo_control = 1;
        tx_frame.message_marker_l = tx_marker_seq;
        tx_frame.message_marker_h = 0;

        memcpy(tx_frame.data_8, item.host_frame.data, mcan_get_message_size_from_dlc(tx_frame.dlc));

        tx_stat = mcan_transmit_via_txfifo_nonblocking(mcan_channels[ch].base, &tx_frame, &fifo_index);
        if (tx_stat != status_success) {
            GS_USB_LOG("[GS_USB] TX submit failed ch=%u stat=%ld depth=%u\r\n",
                       (unsigned)ch,
                       (long)tx_stat,
                       (unsigned)tx_ring_count(ch));
            (void)tx_ring_push(ch, &item);
            return;
        }

        can_tx_states[ch].busy = true;
        can_tx_states[ch].fifo_index = fifo_index;
        can_tx_states[ch].start_us = gs_usb_get_time_us();
        s_gs_can_led_ticks[ch] = GS_USB_LED_ON_TICKS;
        can_tx_states[ch].marker = tx_marker_seq++;
        can_tx_states[ch].inflight_item = item;
        can_tx_states[ch].inflight_valid = true;
        mcan_enable_txbuf_transmission_interrupt(mcan_channels[ch].base, (1UL << fifo_index));
        mcan_enable_txbuf_cancel_finish_interrupt(mcan_channels[ch].base, (1UL << fifo_index));
        GS_USB_LOG("[GS_USB] TX submit ch=%u fifo=%lu depth=%u\r\n",
                   (unsigned)ch,
                   (unsigned long)fifo_index,
                   (unsigned)tx_ring_count(ch));
    }
}

static bool is_in_noncache_region(const void *p, uint32_t size)
{
    extern uint32_t __noncacheable_start__[];
    extern uint32_t __noncacheable_end__[];
    uintptr_t addr = (uintptr_t)p;
    uintptr_t start = (uintptr_t)__noncacheable_start__;
    uintptr_t end = (uintptr_t)__noncacheable_end__;
    return (addr >= start) && ((addr + size) <= end);
}

static void gs_usb_dump_mem_layout(void)
{
    extern uint32_t __noncacheable_start__[];
    extern uint32_t __noncacheable_end__[];
    GS_USB_LOG("[GS_USB] noncache: [0x%08lx, 0x%08lx)\r\n",
        (unsigned long)(uintptr_t)__noncacheable_start__,
        (unsigned long)(uintptr_t)__noncacheable_end__);
    GS_USB_LOG("[GS_USB] dev desc @0x%08lx (noncache=%d)\r\n",
        (unsigned long)(uintptr_t)gs_usb_device_descriptor,
        is_in_noncache_region(gs_usb_device_descriptor, sizeof(gs_usb_device_descriptor)) ? 1 : 0);
    GS_USB_LOG("[GS_USB] ep_out @0x%08lx (noncache=%d) ep_in @0x%08lx (noncache=%d)\r\n",
        (unsigned long)(uintptr_t)ep_out_buffer,
        is_in_noncache_region(ep_out_buffer, sizeof(ep_out_buffer)) ? 1 : 0,
        (unsigned long)(uintptr_t)ep_in_buffer,
        is_in_noncache_region(ep_in_buffer, sizeof(ep_in_buffer)) ? 1 : 0);
}

static void gs_usb_dump_hw_regs(const char *tag)
{
    USB_Type *usb = (USB_Type *)HPM_USB0;
    GS_USB_LOG("[GS_USB][%s] USBCMD=0x%08lx USBSTS=0x%08lx USBINTR=0x%08lx USBMODE=0x%08lx\r\n",
        tag,
        (unsigned long)usb->USBCMD,
        (unsigned long)usb->USBSTS,
        (unsigned long)usb->USBINTR,
        (unsigned long)usb->USBMODE);
    GS_USB_LOG("[GS_USB][%s] PORTSC1=0x%08lx OTG_CTRL0=0x%08lx PHY_STATUS=0x%08lx\r\n",
        tag,
        (unsigned long)usb->PORTSC1,
        (unsigned long)usb->OTG_CTRL0,
        (unsigned long)usb->PHY_STATUS);
}

/* EP OUT Callback (PC -> MCU) */
void gs_usb_ep_out_handler(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    uint32_t offset = 0;

    (void)busid;
    (void)ep;

    out_count++;
    if (out_count <= 20U) {
        printf("[GS_USB] EP_OUT #%lu nbytes=%u configured=%u paused=%u\r\n",
               (unsigned long)out_count,
               (unsigned)nbytes,
               gs_usb_configured ? 1U : 0U,
               gs_usb_out_paused ? 1U : 0U);
    }
    if (out_count <= 45) {
        GS_USB_LOG("[GS_USB] ep_out %u bytes (count=%lu)\r\n",
                   (unsigned)nbytes,
                   (unsigned long)out_count);
    }

    gs_usb_out_armed = false;

    while (offset + 12U <= nbytes) {
        struct gs_host_frame *hf = (struct gs_host_frame *)(ep_out_buffer + offset);
        struct tx_ring_item item;
        uint32_t frame_len = 12U + (((hf->flags & GS_CAN_FLAG_FD) != 0U) ? 64U : 8U);
        uint8_t ch = hf->channel;
        uint32_t host_stride;

        if (ch < GS_USB_CAN_CHANNEL_COUNT && mcan_channels[ch].is_open) {
            host_stride = mcan_channels[ch].config.enable_canfd ? 76U : 20U;
        } else {
            host_stride = (hf->flags & GS_CAN_FLAG_FD) ? 76U : 20U;
        }

        if (offset + frame_len > nbytes) {
            break;
        }

        if (ch >= GS_USB_CAN_CHANNEL_COUNT || !mcan_channels[ch].is_open) {
            offset += host_stride;
            continue;
        }

        item.frame_len = frame_len;
        item.loopback = (mcan_channels[ch].mode_flags & GS_CAN_MODE_LOOP_BACK) != 0U;
        memcpy(&item.host_frame, hf, frame_len);

        if (gs_usb_tx_debug_count < 32U) {
            printf("[GS_USB] TX request #%lu ch=%u id=0x%08lx dlc=%u flags=0x%02x len=%lu\r\n",
                   (unsigned long)(gs_usb_tx_debug_count + 1U),
                   (unsigned)ch,
                   (unsigned long)hf->can_id,
                   (unsigned)hf->can_dlc,
                   (unsigned)hf->flags,
                   (unsigned long)frame_len);
            gs_usb_tx_debug_count++;
        }

        if (!tx_ring_push(ch, &item)) {
            out_drop++;
            gs_usb_pause_out();
            break;
        }

        gs_usb_kick_can_tx(ch);
        offset += host_stride;
    }

    if (!gs_usb_out_paused && gs_usb_should_pause_out()) {
        gs_usb_pause_out();
    }

    if (!gs_usb_out_paused) {
        gs_usb_out_armed = true;
        usbd_ep_start_read(0, GS_USB_EP_OUT, ep_out_buffer, GS_USB_MAX_PACKET_SIZE);
    }
}

/* EP IN Callback (MCU -> PC) */
void gs_usb_ep_in_handler(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    (void)nbytes;

    /* The queue entry remains owned by USB DMA until this real completion
     * callback.  Never release it merely because a software timer expired. */
    if (ep_in_busy && in_q_head != in_q_tail) {
        in_q_tail = (in_q_tail + 1U) % IN_QUEUE_SIZE;
    }
    ep_in_busy = false;
    ep_in_start_us = 0U;
    ep_in_timeout_reported = false;
    ep_in_recovery_streak = 0U;
    ep_in_persistent_reported = false;
}

static void gs_usb_bus_event_handler(uint8_t busid, uint8_t event)
{
    uint8_t ch;

    switch (event) {
    case USBD_EVENT_CONNECTED:
        gs_usb_connected_count++;
        gs_usb_is_connected = true;
        break;
    case USBD_EVENT_DISCONNECTED:
        gs_usb_disconnected_count++;
        gs_usb_is_connected = false;
        gs_usb_configured = false;
        gs_usb_out_paused = false;
        gs_usb_out_armed = false;
        ep_in_busy = false;
        ep_in_start_us = 0U;
        ep_in_timeout_reported = false;
        ep_in_last_recovery_us = 0U;
        ep_in_recovery_streak = 0U;
        ep_in_persistent_reported = false;
        in_q_head = 0;
        in_q_tail = 0;
        for (ch = 0; ch < GS_USB_CAN_CHANNEL_COUNT; ch++) {
            tx_rings[ch].head = 0;
            tx_rings[ch].tail = 0;
            tx_rings[ch].count = 0;
            can_tx_states[ch].busy = false;
            can_tx_states[ch].fifo_index = 0;
            can_tx_states[ch].start_us = 0U;
            can_tx_states[ch].marker = 0U;
            can_tx_states[ch].inflight_valid = false;
            mcan_channels[ch].busoff_detect_us = 0;
            mcan_channels[ch].busoff_recovery_us = 0;
        }
        break;
    case USBD_EVENT_SUSPEND:
        gs_usb_suspend_count++;
        break;
    case USBD_EVENT_RESUME:
        gs_usb_resume_count++;
        break;
    case USBD_EVENT_CONFIGURED:
        gs_usb_configured_count++;
        gs_usb_configured = true;
        gs_usb_out_paused = false;
        gs_usb_out_armed = true;
        GS_USB_LOG("[GS_USB] start EP_OUT read\r\n");
        usbd_ep_start_read(busid, GS_USB_EP_OUT, ep_out_buffer, GS_USB_MAX_PACKET_SIZE);
        break;
    case USBD_EVENT_RESET:
        gs_usb_reset_count++;
        gs_usb_is_connected = false;
        gs_usb_configured = false;
        gs_usb_out_paused = false;
        gs_usb_out_armed = false;
        ep_in_busy = false;
        ep_in_start_us = 0U;
        ep_in_timeout_reported = false;
        ep_in_last_recovery_us = 0U;
        ep_in_recovery_streak = 0U;
        ep_in_persistent_reported = false;
        in_q_head = 0;
        in_q_tail = 0;
        for (ch = 0; ch < GS_USB_CAN_CHANNEL_COUNT; ch++) {
            tx_rings[ch].head = 0;
            tx_rings[ch].tail = 0;
            tx_rings[ch].count = 0;
            can_tx_states[ch].busy = false;
            can_tx_states[ch].fifo_index = 0;
            can_tx_states[ch].start_us = 0U;
            can_tx_states[ch].marker = 0U;
            can_tx_states[ch].inflight_valid = false;
            mcan_channels[ch].busoff_detect_us = 0;
            mcan_channels[ch].busoff_recovery_us = 0;
        }
        break;
    default:
        break;
    }
}

void gs_usb_notify_handler(uint8_t busid, uint8_t event, void *arg)
{
    (void)busid;
    (void)event;
    (void)arg;
}

static void gs_usb_handle_can_tx_irq(uint8_t ch)
{
    uint32_t flags;

    if (ch >= GS_USB_CAN_CHANNEL_COUNT) {
        return;
    }

    flags = mcan_get_interrupt_flags(mcan_channels[ch].base);
    if ((flags & (MCAN_INT_TX_COMPLETED | MCAN_INT_TX_CANCEL_FINISHED)) == 0U) {
        mcan_clear_interrupt_flags(mcan_channels[ch].base, flags);
        return;
    }

    GS_USB_LOG("[GS_USB] TX IRQ ch=%u flags=0x%08lx fifo=%lu\r\n",
               (unsigned)ch,
               (unsigned long)flags,
               (unsigned long)can_tx_states[ch].fifo_index);

    mcan_clear_interrupt_flags(mcan_channels[ch].base, flags);

    if ((flags & MCAN_INT_TX_CANCEL_FINISHED) != 0U) {
        gs_usb_finish_tx(ch, false);
    } else if ((flags & MCAN_INT_TX_COMPLETED) != 0U) {
        gs_usb_finish_tx(ch, true);
    }
}

static void gs_usb_service_tx_timeouts(void)
{
    uint8_t ch;
    uint64_t now_us = gs_usb_get_time_us();

    for (ch = 0; ch < GS_USB_CAN_CHANNEL_COUNT; ch++) {
        mcan_protocol_status_t psr;
        mcan_error_count_t ecr;
        uint32_t fifo_index;
        uint8_t err_state;

        if (!can_tx_states[ch].busy || can_tx_states[ch].start_us == 0U) {
            continue;
        }

        if ((now_us - can_tx_states[ch].start_us) < GS_USB_TX_TIMEOUT_US) {
            continue;
        }

        fifo_index = can_tx_states[ch].fifo_index;
        if (!mcan_is_transmit_request_pending(mcan_channels[ch].base, fifo_index)) {
            if (can_tx_states[ch].busy) {
                gs_usb_finish_tx(ch, false);
            }
            continue;
        }

        mcan_get_protocol_status(mcan_channels[ch].base, &psr);
        mcan_get_error_counter(mcan_channels[ch].base, &ecr);
        GS_USB_LOG("[GS_USB] TX timeout ch=%u fifo=%lu TXBRP=0x%08lx TXBTO=0x%08lx TXBCF=0x%08lx PSR=0x%08lx TEC=%u REC=%u\r\n",
                   (unsigned)ch,
                   (unsigned long)fifo_index,
                   (unsigned long)mcan_channels[ch].base->TXBRP,
                   (unsigned long)mcan_channels[ch].base->TXBTO,
                   (unsigned long)mcan_channels[ch].base->TXBCF,
                   (unsigned long)mcan_channels[ch].base->PSR,
                   (unsigned)ecr.transmit_error_count,
                   (unsigned)ecr.receive_error_count);

        err_state = gs_usb_get_channel_error_state(ch);
        if (err_state >= 2U) {
            if (fifo_index < 32U) {
                mcan_cancel_tx_buf_send_request(mcan_channels[ch].base, fifo_index);
            }
            gs_usb_flush_tx_ring(ch);
            gs_usb_finish_tx(ch, false);
            continue;
        }

        if ((now_us - can_tx_states[ch].start_us) > (GS_USB_TX_TIMEOUT_US * 2U)) {
            /* 二次超时：先取消硬件 TX buffer 再结束，防止 TX FIFO 槽位泄漏 */
            if (fifo_index < 32U) {
                mcan_cancel_tx_buf_send_request(mcan_channels[ch].base, fifo_index);
            }
            gs_usb_finish_tx(ch, false);
            continue;
        }

        if (fifo_index < 32U) {
            mcan_cancel_tx_buf_send_request(mcan_channels[ch].base, fifo_index);
        }

        can_tx_states[ch].start_us = now_us;
    }
}

/*
 * Bus-off 恢复：重新初始化 MCAN 控制器
 * 当控制器进入 bus-off 状态后，硬件需要看到 128 次 bus idle 才能自动恢复。
 * 如果总线上没有其他节点，自动恢复可能永远不会触发。
 * 此函数通过 deinit + reinit 强制复位控制器状态，清空错误计数器。
 */
static void gs_usb_recover_channel(uint8_t ch)
{
    uint64_t now_us;

    if (ch >= GS_USB_CAN_CHANNEL_COUNT || !mcan_channels[ch].is_open) {
        return;
    }

    now_us = gs_usb_get_time_us();
    if (mcan_channels[ch].busoff_recovery_us != 0U &&
        (now_us - mcan_channels[ch].busoff_recovery_us) <
        (GS_USB_BUSOFF_RECOVERY_COOLDOWN_MS * 1000U)) {
        return;
    }
    mcan_channels[ch].busoff_recovery_us = now_us;

    printf("[GS_USB] ch=%u bus-off recovery: reinit MCAN\r\n", (unsigned)ch);

    /* 保存当前配置 */
    mcan_bit_timing_param_t saved_can_timing = mcan_channels[ch].config.can_timing;
    mcan_bit_timing_param_t saved_canfd_timing = mcan_channels[ch].config.canfd_timing;
    uint32_t saved_mode_flags = mcan_channels[ch].mode_flags;
    bool saved_enable_canfd = mcan_channels[ch].config.enable_canfd;

    /* 取消所有 pending TX */
    if (can_tx_states[ch].busy) {
        uint32_t fifo_index = can_tx_states[ch].fifo_index;
        if (fifo_index < 32U) {
            mcan_cancel_tx_buf_send_request(mcan_channels[ch].base, fifo_index);
        }
        gs_usb_flush_tx_ring(ch);
        /* 手动清理 TX 状态（不调用 gs_usb_finish_tx，因为即将 deinit） */
        can_tx_states[ch].busy = false;
        can_tx_states[ch].fifo_index = 0;
        can_tx_states[ch].start_us = 0U;
        can_tx_states[ch].marker = 0U;
        can_tx_states[ch].inflight_valid = false;
    }

    /* 禁用中断 */
    intc_m_disable_irq(mcan_irqs[ch]);

    /* deinit + reinit */
    printf("[GS_USB] ch=%u recovery: before mcan_deinit\r\n", (unsigned)ch);
    mcan_deinit(mcan_channels[ch].base);
    printf("[GS_USB] ch=%u recovery: after mcan_deinit\r\n", (unsigned)ch);

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    {
        mcan_msg_buf_attr_t attr;
        if (ch == 0) { mcan_msg_buf_attr_t tmp = { (uint32_t)&s_mcan_msg_buf0, sizeof(s_mcan_msg_buf0) }; attr = tmp; }
        else if (ch == 1) { mcan_msg_buf_attr_t tmp = { (uint32_t)&s_mcan_msg_buf1, sizeof(s_mcan_msg_buf1) }; attr = tmp; }
        else if (ch == 2) { mcan_msg_buf_attr_t tmp = { (uint32_t)&s_mcan_msg_buf2, sizeof(s_mcan_msg_buf2) }; attr = tmp; }
        else { mcan_msg_buf_attr_t tmp = { (uint32_t)&s_mcan_msg_buf3, sizeof(s_mcan_msg_buf3) }; attr = tmp; }
        mcan_set_msg_buf_attr(mcan_channels[ch].base, &attr);
    }
#endif

    /* 恢复配置 */
    mcan_get_default_config(mcan_channels[ch].base, &mcan_channels[ch].config);
    mcan_channels[ch].config.use_lowlevel_timing_setting = true;
    mcan_channels[ch].config.can_timing = saved_can_timing;
    mcan_channels[ch].config.canfd_timing = saved_canfd_timing;
    mcan_channels[ch].config.enable_canfd = saved_enable_canfd;

    mcan_get_default_ram_config(mcan_channels[ch].base, &mcan_channels[ch].config.ram_config, mcan_channels[ch].config.enable_canfd);
    mcan_channels[ch].config.ram_config.enable_tx_evt_fifo = true;
    mcan_channels[ch].config.ram_config.tx_evt_fifo_watermark = 1;

    mcan_channels[ch].config.enable_tdc = mcan_channels[ch].config.enable_canfd;
    if (mcan_channels[ch].config.enable_tdc) {
        mcan_channels[ch].config.tdc_config.ssp_offset = 6;
        mcan_channels[ch].config.tdc_config.filter_window_length = 0;
    }
    mcan_channels[ch].config.enable_non_iso_mode = false;
    mcan_channels[ch].config.disable_auto_retransmission = CONFIG_GS_USB_DEFAULT_ONE_SHOT || ((saved_mode_flags & GS_CAN_MODE_ONE_SHOT) != 0);

    if (saved_mode_flags & GS_CAN_MODE_LOOP_BACK) {
        mcan_channels[ch].config.mode = mcan_mode_loopback_internal;
    } else if (saved_mode_flags & GS_CAN_MODE_LISTEN_ONLY) {
        mcan_channels[ch].config.mode = mcan_mode_listen_only;
        mcan_channels[ch].config.enable_restricted_operation_mode = true;
    } else {
        mcan_channels[ch].config.mode = mcan_mode_normal;
    }

    printf("[GS_USB] ch=%u recovery: before mcan_init\r\n", (unsigned)ch);
    hpm_stat_t stat = mcan_init(mcan_channels[ch].base, &mcan_channels[ch].config, gs_usb_can_clk);
    printf("[GS_USB] ch=%u recovery: after mcan_init stat=%ld\r\n",
           (unsigned)ch, (long)stat);
    if (stat != status_success) {
        printf("[GS_USB] ch=%u bus-off recovery FAILED: stat=%ld\r\n", (unsigned)ch, (long)stat);
        return;
    }

    /* 配置全局滤波器 */
    mcan_global_filter_config_t filter = {0};
    filter.accept_non_matching_std_frame_option = 0;
    filter.accept_non_matching_ext_frame_option = 0;
    mcan_set_global_filter_config(mcan_channels[ch].base, &filter);

    /* 清空错误计数器 */
    ((volatile uint32_t *)(mcan_channels[ch].base))[0x40/4] = 0;

    /* 重置状态 */
    mcan_channels[ch].error_state = 0;
    mcan_channels[ch].busoff_detect_us = 0;
    tx_rings[ch].head = 0;
    tx_rings[ch].tail = 0;
    tx_rings[ch].count = 0;
    /* Re-arm USB OUT after MCAN recovery so the host can resume TX. */
    if (gs_usb_configured && !gs_usb_out_armed) {
        gs_usb_out_paused = false;
        gs_usb_out_armed = true;
        usbd_ep_start_read(0, GS_USB_EP_OUT, ep_out_buffer, GS_USB_MAX_PACKET_SIZE);
    }

    /* 重新使能中断 */
    mcan_enable_interrupts(mcan_channels[ch].base,
                           MCAN_INT_TX_COMPLETED | MCAN_INT_TX_CANCEL_FINISHED);
    intc_m_enable_irq_with_priority(mcan_irqs[ch], 2);

    printf("[GS_USB] ch=%u bus-off recovery OK\r\n", (unsigned)ch);
    printf("[GS_USB] ch=%u timing after recovery: DBTP=0x%08lx TDCR=0x%08lx\r\n",
           (unsigned)ch,
           (unsigned long)mcan_channels[ch].base->DBTP,
           (unsigned long)mcan_channels[ch].base->TDCR);
}

static void gs_usb_service_tx_complete_poll(void)
{
    uint8_t ch;

    for (ch = 0; ch < GS_USB_CAN_CHANNEL_COUNT; ch++) {
        mcan_tx_event_fifo_elem_t tx_evt;

        if (!mcan_channels[ch].is_open) {
            continue;
        }

        /* Drain all pending TX Event FIFO entries in one pass to prevent overflow.
         * Do not break after completing the in-flight frame: older/stale events
         * may still be pending and must also be consumed. */
        uint32_t tx_evt_drained = 0U;
        while (tx_evt_drained < GS_USB_TX_EVT_DRAIN_LIMIT &&
               mcan_read_tx_evt_fifo(mcan_channels[ch].base, &tx_evt) == status_success) {
            tx_evt_drained++;
            GS_USB_LOG("[GS_USB] TX event ch=%u marker=%u evt=%u fifo=%lu\r\n",
                       (unsigned)ch,
                       (unsigned)tx_evt.message_marker,
                       (unsigned)tx_evt.event_type,
                       (unsigned long)can_tx_states[ch].fifo_index);

            if (can_tx_states[ch].busy && can_tx_states[ch].inflight_valid &&
                tx_evt.message_marker == can_tx_states[ch].marker) {
                gs_usb_finish_tx(ch, true);
            }
        }
    }
}

SDK_DECLARE_EXT_ISR_M(BOARD_CAN0_IRQn, gs_usb_can0_isr)
void gs_usb_can0_isr(void)
{
    gs_usb_can_irq_count[0]++;
    gs_usb_handle_can_tx_irq(0);
}

SDK_DECLARE_EXT_ISR_M(BOARD_CAN1_IRQn, gs_usb_can1_isr)
void gs_usb_can1_isr(void)
{
    gs_usb_can_irq_count[1]++;
    gs_usb_handle_can_tx_irq(1);
}

SDK_DECLARE_EXT_ISR_M(BOARD_CAN2_IRQn, gs_usb_can2_isr)
void gs_usb_can2_isr(void)
{
    gs_usb_can_irq_count[2]++;
    gs_usb_handle_can_tx_irq(2);
}

SDK_DECLARE_EXT_ISR_M(BOARD_CAN3_IRQn, gs_usb_can3_isr)
void gs_usb_can3_isr(void)
{
    gs_usb_can_irq_count[3]++;
    gs_usb_handle_can_tx_irq(3);
}

/* Vendor Control Request Handler */
int gs_usb_vendor_request_handler(uint8_t busid, struct usb_setup_packet *setup, uint8_t **data, uint32_t *len)
{
    uint8_t req_type = setup->bmRequestType & USB_REQUEST_TYPE_MASK;
    if ((req_type != USB_REQUEST_VENDOR) && (req_type != USB_REQUEST_CLASS)) {
        return -1; // Let standard handler or other layers deal with it
    }

    // Handle gs_usb_breq requests (BitTiming, Mode, DeviceConfig, etc.)
    uint16_t req = setup->bRequest;
    uint16_t val = setup->wValue; // Channel index usually passed in wValue

    GS_USB_LOG("[GS_USB] vendor req bm=0x%02x bReq=0x%02x wValue=0x%04x wIndex=0x%04x wLen=%u\r\n",
               setup->bmRequestType,
               setup->bRequest,
               setup->wValue,
               setup->wIndex,
               (unsigned)setup->wLength);

    /* Alternate host drivers may use request 0x0a for data bittiming (20-byte OUT). */
    if (((setup->bmRequestType & USB_REQUEST_DIR_MASK) == 0) && (setup->wLength == sizeof(struct gs_device_bittiming)) && (req == 0x0a)) {
        if (val < 4) {
            struct gs_device_bittiming *bt = (struct gs_device_bittiming *)(*data);
            mcan_bit_timing_param_t *t = &mcan_channels[val].config.canfd_timing;
            mcan_channels[val].have_canfd_timing = true;
            t->prescaler = bt->brp;
            t->num_seg1 = bt->prop_seg + bt->phase_seg1;
            t->num_seg2 = bt->phase_seg2;
            t->num_sjw = bt->sjw;
        }
        *len = 0;
        return 0;
    }

    /* Host-side variants sometimes swap device_config (12) and bt_const (40). */
    if ((setup->bmRequestType & USB_REQUEST_DIR_MASK) && (setup->wLength == 40) && (req == GS_USB_BREQ_DEVICE_CONFIG)) {
        bt_const.feature = GS_CAN_FEATURE_LOOP_BACK | GS_CAN_FEATURE_LISTEN_ONLY | GS_CAN_FEATURE_ONE_SHOT | GS_CAN_FEATURE_FD;
        bt_const.fclk_can = gs_usb_can_clk;
        bt_const.tseg1_min = 1;
        bt_const.tseg1_max = 256;
        bt_const.tseg2_min = 1;
        bt_const.tseg2_max = 128;
        bt_const.sjw_max = 128;
        bt_const.brp_min = 1;
        bt_const.brp_max = 512;
        bt_const.brp_inc = 1;
        *data = (uint8_t *)&bt_const;
        *len = (setup->wLength < sizeof(bt_const)) ? setup->wLength : sizeof(bt_const);
        return 0;
    }
    if ((setup->bmRequestType & USB_REQUEST_DIR_MASK) && (setup->wLength == 12) && (req == GS_USB_BREQ_TIMESTAMP)) {
        dev_config.reserved1 = 0;
        dev_config.reserved2 = 0;
        dev_config.reserved3 = 0;
        dev_config.icount = 3; // 4 CAN channels (icount = nchannels - 1)
        dev_config.sw_version = 2;
        dev_config.hw_version = 1;
        memset(dev_config.reserved, 0, sizeof(dev_config.reserved));
        *data = (uint8_t *)&dev_config;
        *len = (setup->wLength < sizeof(dev_config)) ? setup->wLength : sizeof(dev_config);
        return 0;
    }
    if ((setup->bmRequestType & USB_REQUEST_DIR_MASK) && (setup->wLength == 4) && (req == GS_USB_BREQ_IDENTIFY)) {
        static uint32_t ts = 0;
        *data = (uint8_t *)&ts;
        *len = sizeof(ts);
        return 0;
    }

    switch (req) {
        case GS_USB_BREQ_HOST_FORMAT:
            GS_USB_LOG("[GS_USB] HOST_FORMAT bm=0x%02x wLen=%u\r\n",
                       setup->bmRequestType,
                       (unsigned)setup->wLength);
            if (setup->bmRequestType & USB_REQUEST_DIR_MASK) {
                *data = (uint8_t *)&gs_host_cfg;
                *len = sizeof(gs_host_cfg);
            } else {
                if (setup->wLength >= sizeof(struct gs_host_config)) {
                    gs_host_cfg = *((struct gs_host_config *)(*data));
                }
                *len = 0;
            }
            break;
            
        case GS_USB_BREQ_DEVICE_CONFIG:
            // Return number of channels, feature flags (CAN FD support)
            dev_config.reserved1 = 0;
            dev_config.reserved2 = 0;
            dev_config.reserved3 = 0;
            dev_config.icount = 3; // 4 CAN channels (icount = nchannels - 1)
            dev_config.sw_version = 2; // gs_usb driver v2
            dev_config.hw_version = 1; // custom hardware version
            memset(dev_config.reserved, 0, sizeof(dev_config.reserved));
            *data = (uint8_t *)&dev_config;
            *len = (setup->wLength < sizeof(dev_config)) ? setup->wLength : sizeof(dev_config);
            GS_USB_LOG("[GS_USB] DEVICE_CONFIG len=%u bytes: %02x %02x %02x %02x %02x %02x %02x %02x\r\n",
                       (unsigned)*len,
                       ((uint8_t *)&dev_config)[0],
                       ((uint8_t *)&dev_config)[1],
                       ((uint8_t *)&dev_config)[2],
                       ((uint8_t *)&dev_config)[3],
                       ((uint8_t *)&dev_config)[4],
                       ((uint8_t *)&dev_config)[5],
                       ((uint8_t *)&dev_config)[6],
                       ((uint8_t *)&dev_config)[7]);
            break;
            
        case GS_USB_BREQ_BT_CONST:
        case GS_USB_BREQ_BT_CONST_EXT:
            // Return hardware bit timing constraints (assumes 40MHz CAN clock for calculation)
bt_const.feature = GS_CAN_FEATURE_LOOP_BACK | GS_CAN_FEATURE_LISTEN_ONLY | GS_CAN_FEATURE_ONE_SHOT | GS_CAN_FEATURE_FD | GS_CAN_FEATURE_BT_CONST_EXT;
            bt_const.fclk_can = gs_usb_can_clk; 
            
            if (req == GS_USB_BREQ_BT_CONST_EXT) {
                /* Data Phase Constraints (Tighter for 5Mbps) */
                bt_const.tseg1_min = 1;
                bt_const.tseg1_max = 32;  /* DTSEG1 is 5-bit in MCAN */
                bt_const.tseg2_min = 1;
                bt_const.tseg2_max = 16;  /* DTSEG2 is 4-bit in MCAN */
                bt_const.sjw_max = 16;
                bt_const.brp_min = 1;
                bt_const.brp_max = 32;
                bt_const.brp_inc = 1;
            } else {
                /* Nominal Phase Constraints */
                bt_const.tseg1_min = 1;
                bt_const.tseg1_max = 256;
                bt_const.tseg2_min = 1;
                bt_const.tseg2_max = 128;
                bt_const.sjw_max = 128;
                bt_const.brp_min = 1;
                bt_const.brp_max = 512;
                bt_const.brp_inc = 1;
            }
            bt_const.fclk_can = gs_usb_can_clk;
            *data = (uint8_t *)&bt_const;
            *len = sizeof(bt_const);
            break;
            
        case GS_USB_BREQ_MODE:
            // Start / Stop specific MCAN channel
            if (val < 4) { // channel < 4
                struct gs_device_mode *mode_req = (struct gs_device_mode *)(*data);
                mcan_channels[val].mode_flags = mode_req->flags;
                if (mode_req->mode == 0) { // reset
                    mcan_deinit(mcan_channels[val].base);
                    intc_m_disable_irq(mcan_irqs[val]);
                    mcan_channels[val].is_open = false;
                    mcan_channels[val].error_state = 0;
                    mcan_channels[val].busoff_detect_us = 0;
                    tx_rings[val].head = 0;
                    tx_rings[val].tail = 0;
                    tx_rings[val].count = 0;
                    can_tx_states[val].busy = false;
                    can_tx_states[val].fifo_index = 0;
                    can_tx_states[val].start_us = 0U;
                    can_tx_states[val].marker = 0U;
                    can_tx_states[val].inflight_valid = false;
                } else { // start
                    if (mcan_channels[val].is_open) {
                        *len = 0;
                        return 0;
                    }
                    if (!mcan_channels[val].have_can_timing) {
                        return -1;
                    }
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
                    mcan_msg_buf_attr_t attr;
                    if (val == 0) { mcan_msg_buf_attr_t tmp = { (uint32_t)&s_mcan_msg_buf0, sizeof(s_mcan_msg_buf0) }; attr = tmp; }
                    else if (val == 1) { mcan_msg_buf_attr_t tmp = { (uint32_t)&s_mcan_msg_buf1, sizeof(s_mcan_msg_buf1) }; attr = tmp; }
                    else if (val == 2) { mcan_msg_buf_attr_t tmp = { (uint32_t)&s_mcan_msg_buf2, sizeof(s_mcan_msg_buf2) }; attr = tmp; }
                    else { mcan_msg_buf_attr_t tmp = { (uint32_t)&s_mcan_msg_buf3, sizeof(s_mcan_msg_buf3) }; attr = tmp; }

                    if (mcan_set_msg_buf_attr(mcan_channels[val].base, &attr) != status_success) {
                        mcan_channels[val].is_open = false;
                        return -1;
                    }
#endif
                    // Save the assigned bit timings
                    mcan_bit_timing_param_t saved_can_timing = mcan_channels[val].config.can_timing;
                    mcan_bit_timing_param_t saved_canfd_timing = mcan_channels[val].config.canfd_timing;
                    
                    mcan_get_default_config(mcan_channels[val].base, &mcan_channels[val].config);
                    mcan_channels[val].config.use_lowlevel_timing_setting = true;
                    mcan_channels[val].config.can_timing = saved_can_timing;
                    mcan_channels[val].config.canfd_timing = saved_canfd_timing;
                    
                    // Explicitly enable FD if host requested or we have timing
                    mcan_channels[val].config.enable_canfd = ((mcan_channels[val].mode_flags & GS_CAN_MODE_FD) != 0) || mcan_channels[val].have_canfd_timing;
                    
                    /* 关键修复：自动配置 RAM 缓冲区，支持 64 字节 FD 数据长度 */
                    mcan_get_default_ram_config(mcan_channels[val].base, &mcan_channels[val].config.ram_config, mcan_channels[val].config.enable_canfd);
                    mcan_channels[val].config.ram_config.enable_tx_evt_fifo = true;
                    mcan_channels[val].config.ram_config.tx_evt_fifo_watermark = 1;

                    mcan_channels[val].config.enable_tdc = mcan_channels[val].config.enable_canfd;
                    if (mcan_channels[val].config.enable_tdc) {
                        mcan_channels[val].config.tdc_config.ssp_offset = 6;
                        mcan_channels[val].config.tdc_config.filter_window_length = 0;
                    }
                    /* 确保启用 ISO 模式以匹配周立功的默认设置 */
                    mcan_channels[val].config.enable_non_iso_mode = false;
                    
                    mcan_channels[val].config.disable_auto_retransmission = CONFIG_GS_USB_DEFAULT_ONE_SHOT || ((mcan_channels[val].mode_flags & GS_CAN_MODE_ONE_SHOT) != 0);
                    if (mcan_channels[val].mode_flags & GS_CAN_MODE_LOOP_BACK) {
                        mcan_channels[val].config.mode = mcan_mode_loopback_internal;
                    } else if (mcan_channels[val].mode_flags & GS_CAN_MODE_LISTEN_ONLY) {
                        mcan_channels[val].config.mode = mcan_mode_listen_only;
                        mcan_channels[val].config.enable_restricted_operation_mode = true;
                    } else {
                        mcan_channels[val].config.mode = mcan_mode_normal;
                    }
                    
                    printf("[GS_USB] mcan_init ch %d with clk=%lu\r\n", val, gs_usb_can_clk);
                    printf("[GS_USB] mode: %d, CANFD: %d, brs: %d\r\n", 
                           mcan_channels[val].config.mode, 
                           mcan_channels[val].config.enable_canfd, 
                           mcan_channels[val].config.enable_tdc);
                    printf("[GS_USB] Nominal: prescaler=%u, ts1=%u, ts2=%u, sjw=%u\r\n",
                           mcan_channels[val].config.can_timing.prescaler,
                           mcan_channels[val].config.can_timing.num_seg1,
                           mcan_channels[val].config.can_timing.num_seg2,
                           mcan_channels[val].config.can_timing.num_sjw);
                    printf("[GS_USB] Data: prescaler=%u, ts1=%u, ts2=%u, sjw=%u\r\n",
                           mcan_channels[val].config.canfd_timing.prescaler,
                           mcan_channels[val].config.canfd_timing.num_seg1,
                           mcan_channels[val].config.canfd_timing.num_seg2,
                           mcan_channels[val].config.canfd_timing.num_sjw);

                    hpm_stat_t stat = mcan_init(mcan_channels[val].base, &mcan_channels[val].config, gs_usb_can_clk);
                    if (stat == status_success) {
                        printf("[GS_USB] ch=%d TDCR=0x%08lx DBTP=0x%08lx DBTP_TDC=%lu\r\n",
                               val,
                               (unsigned long)((volatile uint32_t *)(mcan_channels[val].base))[0x48/4],
                               (unsigned long)((volatile uint32_t *)(mcan_channels[val].base))[0x0C/4],
                               (unsigned long)(((volatile uint32_t *)(mcan_channels[val].base))[0x0C/4] >> 23) & 1);
                        mcan_global_filter_config_t filter = {0};
                        filter.accept_non_matching_std_frame_option = 0; // Receive all std ID into RX FIFO 0
                        filter.accept_non_matching_ext_frame_option = 0; // Receive all ext ID into RX FIFO 0
                        mcan_set_global_filter_config(mcan_channels[val].base, &filter);
                        ((volatile uint32_t *)(mcan_channels[val].base))[0x40/4] = 0; // Clear ECR (error counters)
                        mcan_channels[val].error_state = 0;
                        mcan_channels[val].busoff_detect_us = 0;
                        mcan_enable_interrupts(mcan_channels[val].base,
                                               MCAN_INT_TX_COMPLETED | MCAN_INT_TX_CANCEL_FINISHED);
                        intc_m_enable_irq_with_priority(mcan_irqs[val], 2);
                        tx_rings[val].head = 0;
                        tx_rings[val].tail = 0;
                        tx_rings[val].count = 0;
                        can_tx_states[val].busy = false;
                        can_tx_states[val].fifo_index = 0;
                        can_tx_states[val].start_us = 0U;
                        can_tx_states[val].marker = 0U;
                        can_tx_states[val].inflight_valid = false;
                        mcan_channels[val].is_open = true;
                    }
                }
            }
            *len = 0;
            break;
            
        case GS_USB_BREQ_BITTIMING:
        case GS_USB_BREQ_DATA_BITTIMING:
            // Receive and apply baud rate to MCAN
            if (val < 4) { // channel < 4
                struct gs_device_bittiming *bt = (struct gs_device_bittiming *)(*data);
                mcan_bit_timing_param_t *t;
                if (req == GS_USB_BREQ_BITTIMING) {
                    t = &mcan_channels[val].config.can_timing;
                    mcan_channels[val].have_can_timing = true;
                } else {
                    t = &mcan_channels[val].config.canfd_timing;
                    mcan_channels[val].have_canfd_timing = true;
                }
                // gs_usb的位配置和hpm_mcan_drv精确对应
                t->prescaler = bt->brp;
                t->num_seg1 = bt->prop_seg + bt->phase_seg1;
                t->num_seg2 = bt->phase_seg2;
                t->num_sjw = bt->sjw;
            }
            *len = 0;
            break;
            
        case GS_USB_BREQ_IDENTIFY:
        case GS_USB_BREQ_TIMESTAMP:
            if (req == GS_USB_BREQ_TIMESTAMP) {
                gs_timestamp.ts_low = 0;
                gs_timestamp.ts_high = 0;
                gs_timestamp.flags = 0;
                *data = (uint8_t *)&gs_timestamp;
                *len = (setup->wLength < sizeof(gs_timestamp)) ? setup->wLength : sizeof(gs_timestamp);
            } else {
                *len = 0;
            }
            break;

        default:
            return -1; // Stall
    }
    return 0; // Success
}

#define USBD_WINUSB_VENDOR_CODE 0x17

/* WinUSB Microsoft OS 2.0 descriptor sizes */
#define DEVICE_INTERFACE_GUID_FEATURE_LEN  128
#define USBD_WINUSB_DESC_SET_LEN (WINUSB_DESCRIPTOR_SET_HEADER_SIZE + WINUSB_FEATURE_COMPATIBLE_ID_SIZE + DEVICE_INTERFACE_GUID_FEATURE_LEN)

USB_NOCACHE_RAM_INIT_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) uint8_t gs_usb_msosv2_descriptor[] = {
    /* WCID20 descriptor set descriptor */
    WBVAL(WINUSB_DESCRIPTOR_SET_HEADER_SIZE), /* wLength */
    WBVAL(WINUSB_SET_HEADER_DESCRIPTOR_TYPE), /* wDescriptorType */
    0x00, 0x00, 0x03, 0x06,                   /* >= Win 8.1 */  /* dwWindowsVersion*/
    WBVAL(USBD_WINUSB_DESC_SET_LEN),          /* wDescriptorSetTotalLength */

    /* WCID20 compatible ID descriptor */
    WBVAL(WINUSB_FEATURE_COMPATIBLE_ID_SIZE),  /* wLength */
    WBVAL(WINUSB_FEATURE_COMPATIBLE_ID_TYPE),  /* wDescriptorType */
    'W', 'I', 'N', 'U', 'S', 'B', 0, 0,        /* cCID_8 */
    0, 0, 0, 0, 0, 0, 0, 0,                    /* cSubCID_8 */

    /* WCID20 registry property descriptor */
    WBVAL(DEVICE_INTERFACE_GUID_FEATURE_LEN),  /* wLength */
    WBVAL(WINUSB_FEATURE_REG_PROPERTY_TYPE),   /* wDescriptorType */
    WBVAL(WINUSB_PROP_DATA_TYPE_REG_SZ),       /* wPropertyDataType */
    WBVAL(40),                                 /* wPropertyNameLength */
    /* DeviceInterfaceGUID */
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0,
    'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0,
    'G', 0, 'U', 0, 'I', 0, 'D', 0, 0, 0,
    WBVAL(78),                                 /* wPropertyDataLength*/
    '{', 0,
    'C', 0, 'D', 0, 'B', 0, '3', 0, 'B', 0, '5', 0, 'A', 0, 'D', 0, '-', 0,
    '2', 0, '9', 0, '3', 0, 'B', 0, '-', 0,
    '4', 0, '6', 0, '6', 0, '3', 0, '-', 0,
    'A', 0, 'A', 0, '3', 0, '6', 0, '-', 0,
    '1', 0, 'A', 0, 'A', 0, 'E', 0, '4', 0, '6', 0, '4', 0, '6', 0, '3', 0, '7', 0, '7', 0, '6', 0,
    '}', 0, 0, 0,
};

#define USBD_NUM_DEV_CAPABILITIES (1)
#define USBD_WINUSB_DESC_LEN 28
#define USBD_BOS_WTOTALLENGTH (0x05 + USBD_WINUSB_DESC_LEN)

USB_NOCACHE_RAM_INIT_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) uint8_t gs_usb_bos_descriptor[] = {
    /* WCID20 BOS descriptor */
    0x05,                         /* bLength */
    0x0f,                         /* bDescriptorType */
    WBVAL(USBD_BOS_WTOTALLENGTH), /* wTotalLength */
    USBD_NUM_DEV_CAPABILITIES,    /* bNumDeviceCaps */

    /* WCID20 device capability descriptor */
    USBD_WINUSB_DESC_LEN,           /* bLength */
    0x10,                           /* bDescriptorType */
    USB_DEVICE_CAPABILITY_PLATFORM, /* bDevCapabilityType */
    0x00,                           /* bReserved */
    0xDF, 0x60, 0xDD, 0xD8,         /* PlatformCapabilityUUID */
    0x89, 0x45, 0xC7, 0x4C,
    0x9C, 0xD2, 0x65, 0x9D,
    0x9E, 0x64, 0x8A, 0x9F,
    0x00, 0x00, 0x03, 0x06,                  /* >= Win 8.1 */ /* dwWindowsVersion*/
    WBVAL(USBD_WINUSB_DESC_SET_LEN),         /* wDescriptorSetTotalLength */
    USBD_WINUSB_VENDOR_CODE,                 /* bVendorCode */
    0,                                       /* bAltEnumCode */
};

static const struct usb_msosv2_descriptor msosv2_desc = {
    .vendor_code = USBD_WINUSB_VENDOR_CODE,
    .compat_id = gs_usb_msosv2_descriptor,
    .compat_id_len = USBD_WINUSB_DESC_SET_LEN,
};

static const struct usb_bos_descriptor bos_desc = {
    .string = gs_usb_bos_descriptor,
    .string_len = USBD_BOS_WTOTALLENGTH
};

static const struct usb_descriptor gs_usb_descriptors = {
    .device_descriptor_callback = gs_usb_device_descriptor_callback,
    .config_descriptor_callback = gs_usb_config_descriptor_callback,
    .device_quality_descriptor_callback = gs_usb_device_quality_descriptor_callback,
    .other_speed_descriptor_callback = gs_usb_other_speed_descriptor_callback,
    .string_descriptor_callback = gs_usb_string_descriptor_callback,
    .msosv2_descriptor = &msosv2_desc,
    .bos_descriptor = &bos_desc,
};

static struct usbd_endpoint gs_usb_endpoints[] = {
    { .ep_addr = GS_USB_EP_OUT, .ep_cb = gs_usb_ep_out_handler },
    { .ep_addr = GS_USB_EP_IN,  .ep_cb = gs_usb_ep_in_handler  }
};

struct usbd_interface gs_usb_interface;

void gs_usb_init(void)
{
    usbd_desc_register(0, &gs_usb_descriptors);

    gs_usb_interface.class_interface_handler = NULL;
    gs_usb_interface.class_endpoint_handler = NULL;
    gs_usb_interface.vendor_handler = gs_usb_vendor_request_handler;
    gs_usb_interface.notify_handler = NULL;
    usbd_add_interface(0, &gs_usb_interface);

    usbd_add_endpoint(0, &gs_usb_endpoints[0]);
    usbd_add_endpoint(0, &gs_usb_endpoints[1]);

    printf("[GS_USB] init: before usbd_initialize\r\n");
    usbd_initialize(0, CONFIG_HPM_USBD_BASE, gs_usb_bus_event_handler);
    printf("[GS_USB] init: after usbd_initialize\r\n");
    ((USB_Type *)HPM_USB0)->USBMODE |= USB_USBMODE_SDIS_MASK;
    gs_usb_dump_hw_regs("post_init");
}

static void gs_usb_watchdog_init(void)
{
    clock_add_to_group(clock_watchdog0, BOARD_RUNNING_CORE);
    ewdg_config_t config;
    ewdg_get_default_config(HPM_EWDG0, &config);
    config.enable_watchdog = true;
    config.int_rst_config.enable_timeout_reset = true;      /* timeout reset */
    config.ctrl_config.use_lowlevel_timeout = false;
    config.ctrl_config.keep_running_in_debug_mode = false;  /* pause in debug, no reset */
    config.ctrl_config.cnt_clk_sel = ewdg_cnt_clk_src_bus_clk; /* AHB0, no 32K xtal */
    config.cnt_src_freq = clock_get_frequency(clock_watchdog0);
    config.ctrl_config.timeout_reset_us = 1000UL * 1000UL;  /* 1s */
    ewdg_init(HPM_EWDG0, &config);
}

void gs_usb_run(void)
{
    // 初始化 USB PHY和时钟，配置运行在高速模式
    board_init_usb((USB_Type *)HPM_USB0);

    // 对齐官方可用样例：仅设置优先级，IRQ 由 usbd_initialize 内部流程使能
    intc_set_irq_priority(CONFIG_HPM_USBD_IRQn, 3);

    // TODO: 考虑是否要有 CAN 的收发器使能引脚配置
    // 初始化 MCAN0 ~ MCAN3 的 RX/TX 引脚
    init_mcan0_pins();
    init_mcan1_pins();
    init_mcan2_pins();
    init_mcan3_pins();

    /* Set up global variables to print */
    gs_usb_can_clk = board_init_can_clock(HPM_MCAN0);
    board_init_can_clock(HPM_MCAN1);
    board_init_can_clock(HPM_MCAN2);
    board_init_can_clock(HPM_MCAN3);
    
    printf("\r\n==========================================\r\n");
    printf("[GS_USB] gs_usb.c build: %s %s\r\n", __DATE__, __TIME__);
    printf("[GS_USB] MCAN Clock: %lu Hz\r\n", gs_usb_can_clk);
    printf("[GS_USB] mcan_channels[0] base: 0x%08lX\r\n", (uint32_t)HPM_MCAN0);

    printf("[GS_USB] Firmware Started! Initializing USB...\r\n");
    GS_USB_LOG("[GS_USB] USB0 clock: %lu Hz\r\n", clock_get_frequency(clock_usb0));
    GS_USB_LOG("[GS_USB] mchtmr0 clock (from osc24m path): %lu Hz\r\n", clock_get_frequency(clock_mchtmr0));
    GS_USB_LOG("[GS_USB] IRQ: %d, EP0 MPS: %d, HS bulk MPS: %d\r\n",
           CONFIG_HPM_USBD_IRQn, 64, GS_USB_MAX_PACKET_SIZE);
    gs_usb_dump_mem_layout();
    gs_usb_dump_hw_regs("boot");
    printf("==========================================\r\n");

    // /*
    //  * 冷启动修复：board_init_usb 配置了 60MHz USB 时钟和内部 VBUS 后，
    //  * USB 控制器状态机处于 power-on 不定态。必须通过 USBCMD.RST=1 将其
    //  * 复位到确定初始态，否则 usbd_initialize 设 RS=1 时 D+ 上拉可能不干净，
    //  * 主机收不到合法连接事件 → 无法枚举。
    //  * 手动复位 MCU 可以工作是因为全局复位间接复位了 USB 控制器。
    //  *
    //  * EHCI 规范要求 RST 之前必须确保 RS=0（冷启时 RS 默认为 0，满足条件）。
    //  */
    // {
    //     USB_Type *usb = (USB_Type *)HPM_USB0;

    //     /* 等待 PHY / 内部 VBUS 通路稳定（模拟电路需要建立时间） */
    //     board_delay_ms(50);

    //     /* USBCMD.RST = 1：复位管道、定时器、计数器、状态机 */
    //     usb->USBCMD |= (1UL << 1);
    //     board_delay_ms(1);
    //     /* 等待硬件自动清零 RST（EHCI 规定最多 ~1ms） */
    //     {
    //         volatile uint32_t timeout = 50000U;
    //         while ((usb->USBCMD & (1UL << 1)) && --timeout) {
    //             __asm__("nop");
    //         }
    //         if (timeout == 0U) {
    //             printf("[GS_USB] WARNING: USBCMD.RST did not clear!\r\n");
    //         }
    //     }

    //     gs_usb_dump_hw_regs("after_rst");
    // }

    gs_usb_init();
    printf("[GS_USB] init: gs_usb_init() returned\r\n");

    /* 禁用 Suspend 中断：D+上拉后总线空闲>3ms会反复触发SLI形成死循环 */
    ((USB_Type *)HPM_USB0)->USBINTR &= ~USB_USBINTR_SLE_MASK;

    /* 强制绑定 USB0 IRQ 到 CherryUSB ISR 入口，排除向量未正确链接的问题 */
    install_isr(CONFIG_HPM_USBD_IRQn, (uint32_t)hpm_isr_usb0);
    intc_m_enable_irq_with_priority(CONFIG_HPM_USBD_IRQn, 3);
    printf("[GS_USB] IRQ bound: %d -> hpm_isr_usb0\r\n", CONFIG_HPM_USBD_IRQn);
    install_isr(BOARD_CAN0_IRQn, (uint32_t)gs_usb_can0_isr);
    install_isr(BOARD_CAN1_IRQn, (uint32_t)gs_usb_can1_isr);
    install_isr(BOARD_CAN2_IRQn, (uint32_t)gs_usb_can2_isr);
    install_isr(BOARD_CAN3_IRQn, (uint32_t)gs_usb_can3_isr);
    intc_set_irq_priority(BOARD_CAN0_IRQn, 2);
    intc_set_irq_priority(BOARD_CAN1_IRQn, 2);
    intc_set_irq_priority(BOARD_CAN2_IRQn, 2);
    intc_set_irq_priority(BOARD_CAN3_IRQn, 2);

    uint32_t idle_log_tick = 0;
    uint32_t alive_tick = 0;
    uint32_t last_reset_count = 0;
    uint32_t last_configured_count = 0;
    uint32_t last_connected_count = 0;
    uint32_t last_disconnected_count = 0;
    uint32_t last_suspend_count = 0;
    uint32_t last_resume_count = 0;

    gs_usb_watchdog_init();

    while (1) {
        uint64_t loop_now_us;
        static uint64_t heartbeat_last_us = 0U;
        uint8_t previous_stage = gs_usb_main_stage;
        gs_usb_main_loop_count++;
        /* Emit the heartbeat before entering USB service.  This project also
         * polls a pending UI status here because the IRQ path alone can leave
         * a control transfer pending during enumeration on this platform. */
        loop_now_us = gs_usb_get_time_us();
        if (heartbeat_last_us == 0U ||
            (loop_now_us - heartbeat_last_us) >= 1000000U) {
            heartbeat_last_us = loop_now_us;
            USB_Type *heartbeat_usb = (USB_Type *)HPM_USB0;
            printf("[GS_USB] heartbeat pre-usb loops=%lu prev_stage=%u cfg=%u conn=%u USBSTS=0x%08lx poll=%lu OUT=%lu paused=%u armed=%u ring=%u,%u,%u,%u busy=%u,%u,%u,%u RXQ=%lu IN=%u fail=%lu\r\n",
                   (unsigned long)gs_usb_main_loop_count,
                   (unsigned)previous_stage,
                   gs_usb_configured ? 1U : 0U,
                   gs_usb_is_connected ? 1U : 0U,
                   (unsigned long)heartbeat_usb->USBSTS,
                   (unsigned long)gs_usb_poll_irq_count,
                   (unsigned long)out_count,
                   gs_usb_out_paused ? 1U : 0U,
                   gs_usb_out_armed ? 1U : 0U,
                   (unsigned)tx_ring_count(0),
                   (unsigned)tx_ring_count(1),
                   (unsigned)tx_ring_count(2),
                   (unsigned)tx_ring_count(3),
                   can_tx_states[0].busy ? 1U : 0U,
                   can_tx_states[1].busy ? 1U : 0U,
                   can_tx_states[2].busy ? 1U : 0U,
                   can_tx_states[3].busy ? 1U : 0U,
                   (unsigned long)((in_q_head + IN_QUEUE_SIZE - in_q_tail) % IN_QUEUE_SIZE),
                   ep_in_busy ? 1U : 0U,
                   (unsigned long)ep_in_submit_fail_count);
        }
        gs_usb_main_stage = 1U; /* USB service */
        ewdg_refresh(HPM_EWDG0);
        USB_Type *usb = (USB_Type *)HPM_USB0;

        /* The same CherryUSB handler is also entered from hpm_isr_usb0().
         * Serialize this polling fallback with the hardware IRQ so QHD/QTD
         * completion processing can never be re-entered. */
        intc_m_disable_irq(CONFIG_HPM_USBD_IRQn);
        if (usb->USBSTS & USB_USBSTS_UI_MASK) {
            gs_usb_poll_irq_count++;
            USBD_IRQHandler(0);
        }
        intc_m_enable_irq(CONFIG_HPM_USBD_IRQn);
        if (last_connected_count != gs_usb_connected_count) {
            last_connected_count = gs_usb_connected_count;
            printf("[GS_USB] USB connected (count=%lu)\r\n", (unsigned long)last_connected_count);
        }
        if (last_disconnected_count != gs_usb_disconnected_count) {
            last_disconnected_count = gs_usb_disconnected_count;
            printf("[GS_USB] USB disconnected (count=%lu)\r\n", (unsigned long)last_disconnected_count);
        }
        if (last_suspend_count != gs_usb_suspend_count) {
            last_suspend_count = gs_usb_suspend_count;
            printf("[GS_USB] USB suspend (count=%lu)\r\n", (unsigned long)last_suspend_count);
        }
        if (last_resume_count != gs_usb_resume_count) {
            last_resume_count = gs_usb_resume_count;
            printf("[GS_USB] USB resume (count=%lu)\r\n", (unsigned long)last_resume_count);
        }
        if (last_reset_count != gs_usb_reset_count) {
            last_reset_count = gs_usb_reset_count;
            printf("[GS_USB] USB reset (count=%lu)\r\n", (unsigned long)last_reset_count);
        }
        if (last_configured_count != gs_usb_configured_count) {
            last_configured_count = gs_usb_configured_count;
            printf("[GS_USB] USB configured (count=%lu)\r\n", (unsigned long)last_configured_count);
        }

        if (!gs_usb_configured) {
            if (++alive_tick >= 30000000U) {
                alive_tick = 0;
                /* printf("[GS_USB] alive (not configured)\r\n"); */
            }
            if (++idle_log_tick >= 5000000U) {
                idle_log_tick = 0;
                /* gs_usb_dump_hw_regs("idle"); */
            }

            if (++s_gs_mode_led_tick >= GS_USB_MODE_LED_INTERVAL) {
                s_gs_mode_led_tick = 0;
                s_gs_mode_led_state = !s_gs_mode_led_state;
                if (s_gs_mode_led_state) board_led_mode_on(); else board_led_mode_off();
            }

            if (gs_usb_is_connected) {
                if (++s_gs_usb_led_tick >= 300000U) {
                    s_gs_usb_led_tick = 0;
                    board_led_usb_toggle();
                }
            } else {
                board_led_usb_off();
                board_led_can_off(0); board_led_can_off(1);
                board_led_can_off(2); board_led_can_off(3);
                s_gs_can_led_ticks[0] = 0; s_gs_can_led_ticks[1] = 0;
                s_gs_can_led_ticks[2] = 0; s_gs_can_led_ticks[3] = 0;
                s_gs_usb_led_tick = 0;
            }

            continue; // 在主机配置好 USB (分配地址+枚举结束) 之前，跳过疯狂扫描外设防止总线阻塞导致 DMA 或中断受影响
        }

        gs_usb_main_stage = 2U; /* TX event FIFO */
        gs_usb_service_tx_complete_poll();
        gs_usb_main_stage = 3U; /* TX timeout */
        gs_usb_service_tx_timeouts();

        /* Never clear the software busy flag without first aborting the QTD.
         * A bounded EP1-IN-only flush preserves the queue entry for retry. */
        if (ep_in_busy && ep_in_start_us != 0U &&
            (gs_usb_get_time_us() - ep_in_start_us) >= GS_USB_EP_IN_TIMEOUT_US) {
            uint64_t timeout_now_us = gs_usb_get_time_us();
            if (!ep_in_timeout_reported) {
                USB_Type *timeout_usb = (USB_Type *)HPM_USB0;
                ep_in_timeout_reported = true;
                printf("[GS_USB] EP_IN timeout: PRIME=0x%08lx STAT=0x%08lx COMPLETE=0x%08lx RXQ=%lu\r\n",
                       (unsigned long)timeout_usb->ENDPTPRIME,
                       (unsigned long)timeout_usb->ENDPTSTAT,
                       (unsigned long)timeout_usb->ENDPTCOMPLETE,
                       (unsigned long)((in_q_head + IN_QUEUE_SIZE - in_q_tail) % IN_QUEUE_SIZE));
            }
            (void)gs_usb_recover_ep_in(timeout_now_us);
        }

        gs_usb_main_stage = 4U; /* TX kick */
        {
            uint8_t kick_ch;
            for (kick_ch = 0; kick_ch < GS_USB_CAN_CHANNEL_COUNT; kick_ch++) {
                if (tx_ring_count(kick_ch) > 0U && !can_tx_states[kick_ch].busy && mcan_channels[kick_ch].is_open) {
                    gs_usb_kick_can_tx(kick_ch);
                }
            }
        }

        for (uint8_t lch = 0; lch < GS_USB_CAN_CHANNEL_COUNT; lch++) {
            if (s_gs_can_led_ticks[lch] > 0) {
                s_gs_can_led_ticks[lch]--;
                if (s_gs_can_led_ticks[lch] & 0x80U) {
                    board_led_can_on(lch);
                } else {
                    board_led_can_off(lch);
                }
            } else {
                board_led_can_off(lch);
            }
        }
        if (++s_gs_mode_led_tick >= GS_USB_MODE_LED_INTERVAL) {
            s_gs_mode_led_tick = 0;
            s_gs_mode_led_state = !s_gs_mode_led_state;
            if (s_gs_mode_led_state) board_led_mode_on(); else board_led_mode_off();
        }

        if (++s_gs_usb_led_tick >= 100000U) {
            s_gs_usb_led_tick = 0;
            board_led_usb_toggle();
        }

        gs_usb_main_stage = 5U; /* CAN RX */
        // Process CAN RX to USB IN buffer
        if (1) {
            for (int i = 0; i < 4; i++) {
                if (!mcan_channels[i].is_open) continue;
                
                // Read and report hardware error state changes
                mcan_protocol_status_t psr;
                mcan_get_protocol_status(mcan_channels[i].base, &psr);
                
                uint8_t current_err_state = 0;
                if (psr.in_bus_off_state) {
                    current_err_state = 3;
                } else if (psr.in_error_passive_state) {
                    current_err_state = 2;
                } else if (psr.in_warning_state) {
                    current_err_state = 1;
                }

                if (current_err_state != mcan_channels[i].error_state) {
                    if (current_err_state >= 2U || mcan_channels[i].error_state >= 2U) {
                        mcan_error_count_t log_ecr;
                        mcan_get_error_counter(mcan_channels[i].base, &log_ecr);
                        printf("[GS_USB] ch=%u ERR state: %u -> %u\r\n",
                               (unsigned)i,
                               (unsigned)mcan_channels[i].error_state,
                               (unsigned)current_err_state);
                        if (current_err_state >= 2U) {
                            printf("[GS_USB] ch=%u counters: TEC=%u REC=%u PSR=0x%08lx DBTP=0x%08lx TDCR=0x%08lx\r\n",
                                   (unsigned)i,
                                   (unsigned)log_ecr.transmit_error_count,
                                   (unsigned)log_ecr.receive_error_count,
                                   (unsigned long)mcan_channels[i].base->PSR,
                                   (unsigned long)mcan_channels[i].base->DBTP,
                                   (unsigned long)mcan_channels[i].base->TDCR);
                        }
                    }
                    mcan_channels[i].error_state = current_err_state;
                    struct gs_host_frame err_hf = {0};
                    err_hf.channel = (uint8_t)i;
                    err_hf.echo_id = 0xFFFFFFFFU;
                    err_hf.can_id = CAN_ERR_FLAG | CAN_ERR_CRTL;
                    err_hf.can_dlc = 8;

                    if (current_err_state == 3) { // Bus Off
                        err_hf.can_id |= CAN_ERR_BUSOFF;
                        {
                            mcan_error_count_t ecr;
                            mcan_get_error_counter(mcan_channels[i].base, &ecr);
                            printf("[GS_USB] ch=%u BUS-OFF: TEC=%u REC=%u PSR=0x%08lx\r\n",
                                   (unsigned)i,
                                   (unsigned)ecr.transmit_error_count,
                                   (unsigned)ecr.receive_error_count,
                                   (unsigned long)mcan_channels[i].base->PSR);
                        }
                        (void)enqueue_in_msg(&err_hf, 20U);
                        /* 记录 bus-off 时间戳，稍后自动恢复 */
                        if (mcan_channels[i].busoff_detect_us == 0) {
                            mcan_channels[i].busoff_detect_us = gs_usb_get_time_us();
                        }
                        continue;
                    } else if (current_err_state == 2) { // Error Passive
                        mcan_error_count_t ecr;
                        mcan_get_error_counter(mcan_channels[i].base, &ecr);
                        if (ecr.receive_error_count >= 128) err_hf.data[1] |= CAN_ERR_CRTL_RX_PASSIVE;
                        if (ecr.transmit_error_count >= 128) err_hf.data[1] |= CAN_ERR_CRTL_TX_PASSIVE;
                    } else if (current_err_state == 1) { // Warning
                        mcan_error_count_t ecr;
                        mcan_get_error_counter(mcan_channels[i].base, &ecr);
                        if (ecr.receive_error_count >= 96) err_hf.data[1] |= CAN_ERR_CRTL_RX_WARNING;
                        if (ecr.transmit_error_count >= 96) err_hf.data[1] |= CAN_ERR_CRTL_TX_WARNING;
                    } else { // Back to Active
                        err_hf.data[1] = 0; // Clear all error flags
                        mcan_channels[i].busoff_detect_us = 0; // 清除 bus-off 时间戳
                    }

                    (void)enqueue_in_msg(&err_hf, 20U);
                }

                /* Allow transient error-passive states, but recover a channel
                 * that remains passive for too long (cooldown still applies). */
                if (current_err_state == 2U) {
                    if (mcan_channels[i].error_passive_detect_us == 0U) {
                        mcan_channels[i].error_passive_detect_us = gs_usb_get_time_us();
                    } else if ((gs_usb_get_time_us() - mcan_channels[i].error_passive_detect_us) >=
                               (GS_USB_ERROR_PASSIVE_RECOVERY_MS * 1000U)) {
                        /* Keep error-passive state; host down/up performs recovery. */
                        mcan_channels[i].error_passive_detect_us = gs_usb_get_time_us();
                    }
                } else if (current_err_state < 2U) {
                    mcan_channels[i].error_passive_detect_us = 0U;
                }
                
                /* Bus-off 后保持当前状态，不自动 deinit/reinit；由主机 down/up 恢复。 */
                if (mcan_channels[i].busoff_detect_us != 0) {
                    uint64_t now_us = gs_usb_get_time_us();
                    if ((now_us - mcan_channels[i].busoff_detect_us) >= (GS_USB_BUSOFF_RECOVERY_DELAY_MS * 1000U)) {
                        /* Bus-off recovery intentionally disabled; host down/up required. */
                    }
                }
                
                mcan_rx_message_t rx_msg;
                uint32_t rx_drained = 0U;
                // Attempt to read from RX FIFO 0
                while (rx_drained < GS_USB_RX_DRAIN_LIMIT &&
                       status_success == mcan_read_rxfifo(mcan_channels[i].base, 0, &rx_msg)) {
                    rx_drained++;
                    struct gs_host_frame hf = {0};
                    uint32_t frame_len;

                    hf.channel = i;
                    hf.echo_id = 0xFFFFFFFFU; // RX frame

                    if (rx_msg.use_ext_id) {
                        hf.can_id = (rx_msg.ext_id & 0x1FFFFFFF) | 0x80000000U;
                    } else {
                        hf.can_id = rx_msg.std_id;
                    }
                    if (rx_msg.rtr) hf.can_id |= 0x40000000;

                    hf.can_dlc = rx_msg.dlc;
                    hf.flags = 0;
                    if (rx_msg.canfd_frame) hf.flags |= GS_CAN_FLAG_FD;
                    if (rx_msg.bitrate_switch) hf.flags |= GS_CAN_FLAG_BRS;
                    hf.reserved = 0;

                    uint8_t rx_len = mcan_get_message_size_from_dlc(rx_msg.dlc);
                    memcpy(hf.data, rx_msg.data_8, rx_len);
                    frame_len = 12U + ((hf.flags & GS_CAN_FLAG_FD) ? 64U : 8U);

                    // Disabled Printf to avoid hanging UART
                    // printf("[GS_USB] RX ch=%u ...

                    (void)enqueue_in_msg(&hf, frame_len);
                    s_gs_can_led_ticks[i] = GS_USB_LED_ON_TICKS;
                }
            }
        }
        
        gs_usb_main_stage = 6U; /* USB IN */
        // Flush IN buffer one frame at a time
        if (in_q_head != in_q_tail && !ep_in_busy) {
            uint32_t len = in_queue[in_q_tail].len;
            int ep_status;
            memcpy(ep_in_dma_buffer, in_queue[in_q_tail].data, len);

            ep_in_busy = true;
            ep_in_start_us = gs_usb_get_time_us();
            ep_in_timeout_reported = false;
            ep_status = usbd_ep_start_write(0, GS_USB_EP_IN, ep_in_dma_buffer, len);
            if (ep_status != 0) {
                ep_in_busy = false;
                ep_in_start_us = 0U;
                ep_in_submit_fail_count++;
            }
        }

        gs_usb_main_stage = 7U; /* heartbeat */
        loop_now_us = gs_usb_get_time_us();
        if (heartbeat_last_us == 0U ||
            (loop_now_us - heartbeat_last_us) >= 1000000U) {
            heartbeat_last_us = loop_now_us;
            printf("[GS_USB] heartbeat loops=%lu stage=%u poll=%lu OUT=%lu paused=%u armed=%u ring=%u,%u,%u,%u busy=%u,%u,%u,%u RXQ=%lu IN=%u fail=%lu\r\n",
                   (unsigned long)gs_usb_main_loop_count,
                   (unsigned)gs_usb_main_stage,
                   (unsigned long)gs_usb_poll_irq_count,
                   (unsigned long)out_count,
                   gs_usb_out_paused ? 1U : 0U,
                   gs_usb_out_armed ? 1U : 0U,
                   (unsigned)tx_ring_count(0),
                   (unsigned)tx_ring_count(1),
                   (unsigned)tx_ring_count(2),
                   (unsigned)tx_ring_count(3),
                   can_tx_states[0].busy ? 1U : 0U,
                   can_tx_states[1].busy ? 1U : 0U,
                   can_tx_states[2].busy ? 1U : 0U,
                   can_tx_states[3].busy ? 1U : 0U,
                   (unsigned long)((in_q_head + IN_QUEUE_SIZE - in_q_tail) % IN_QUEUE_SIZE),
                   ep_in_busy ? 1U : 0U,
                   (unsigned long)ep_in_submit_fail_count);
        }
    }
}
