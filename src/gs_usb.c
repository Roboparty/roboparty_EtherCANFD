// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 wentywenty

/*
 * gs_usb (CAN FD to USB) Firmware for HPMicro
 * Based on CherryUSB
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "board.h"
#include "hpm_csr_drv.h"
#include "hpm_sysctl_drv.h"
#include "hpm_mcan_drv.h"
#include "hpm_otp_drv.h"
#include "hpm_otp_table.h"
#include "version.h"
#include "hpm_clock_drv.h"
#include "hpm_ewdg_drv.h"
#include "hpm_interrupt.h"
#include "hpm_usb_regs.h"
#include "usbd_core.h"
#include "usb_config.h"
#include "gs_usb_time.h"
#include "can_irq_dispatch.h"



#define GS_USB_VID 0x1209
#define GS_USB_PID 0x2323
#define WBVAL(x) ((x) & 0xFF), (((x) >> 8) & 0xFF)

#define GS_USB_LOG_ENABLE 1
#ifndef CONFIG_GS_USB_TX_TRACE
#define CONFIG_GS_USB_TX_TRACE 0
#endif
#ifndef CONFIG_GS_USB_PERIODIC_DIAGNOSTICS
#define CONFIG_GS_USB_PERIODIC_DIAGNOSTICS 0
#endif
#if GS_USB_LOG_ENABLE
static void gs_usb_log(const char *fmt, ...);
#define GS_USB_LOG(fmt, ...) gs_usb_log(fmt, ##__VA_ARGS__)
#else
#define GS_USB_LOG(fmt, ...) do { } while (0)
#endif
#if CONFIG_GS_USB_TX_TRACE
#define GS_USB_TX_TRACE(fmt, ...) GS_USB_LOG(fmt, ##__VA_ARGS__)
#else
#define GS_USB_TX_TRACE(fmt, ...) do { } while (0)
#endif


/* gs_usb vendor requests */
#define GS_USB_LED_ON_TICKS      500U
#define GS_USB_MODE_LED_INTERVAL 500000U

#define GS_USB_BREQ_HOST_FORMAT    0
#define GS_USB_BREQ_BITTIMING      1
#define GS_USB_BREQ_MODE           2
#define GS_USB_BREQ_BT_CONST       4
#define GS_USB_BREQ_DEVICE_CONFIG  5
#define GS_USB_BREQ_TIMESTAMP      6
#define GS_USB_BREQ_IDENTIFY       7
#define GS_USB_BREQ_DATA_BITTIMING 10
#define GS_USB_BREQ_BT_CONST_EXT   11

/* Non-upstream diagnostics extension (host queries it explicitly; the stock
 * gs_usb kernel driver never sends it, so it is inert unless a diagnostic tool
 * issues the vendor control transfer). */
#define GS_USB_BREQ_COUNTERS       0x10
#define GS_USB_COUNTERS_VERSION    2U

#define GS_USB_CAN_CHANNEL_COUNT 4U

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

/* gs_usb USB protocol mode flags */
#define GS_CAN_MODE_LISTEN_ONLY    0x0001
#define GS_CAN_MODE_LOOP_BACK      0x0002
#define GS_CAN_MODE_ONE_SHOT       0x0008
#define GS_CAN_MODE_TRIPLE_SAMPLE  0x0004
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

/* ONE_SHOT is controlled by the host. Motor deployments must explicitly
 * request one-shot if retransmission of stale commands is undesirable. */

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

struct gs_device_bt_const_extended {
    struct gs_device_bt_const nominal;
    uint32_t dtseg1_min, dtseg1_max;
    uint32_t dtseg2_min, dtseg2_max;
    uint32_t dsjw_max, dbrp_min, dbrp_max, dbrp_inc;
} __attribute__((packed));

_Static_assert(sizeof(struct gs_device_bt_const_extended) == 72, "gs_usb extended timing ABI");

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

/* Host-queryable firmware counters (GS_USB_BREQ_COUNTERS, vendor IN). */
struct gs_device_counters {
    uint32_t version;
    uint32_t rx_dropped_total;                        /* 软件 in_queue 满（桥→主机丢弃） */
    uint32_t rx_dropped_ch[GS_USB_CAN_CHANNEL_COUNT];
    uint32_t rxfifo0_lost[GS_USB_CAN_CHANNEL_COUNT];  /* MCAN 硬件 RX FIFO0 丢失 */
    uint32_t out_drop_total;                          /* 主机下发帧被丢弃（两类合计） */
    uint32_t out_drop_ring;                           /* TX ring 满 */
    uint32_t out_drop_invalid;                        /* 主机下发非法 error frame */
    uint32_t tx_failed;                               /* finish_tx(false) 合计 */
    uint32_t tx_arb_lost;                             /* 发送失败且非本固件超时取消 */
    uint32_t tx_timeout_cancel;                       /* 20ms 超时取消导致的失败 */
    uint32_t tx_halted;                               /* TX 超时 2x 后进入 INIT 的次数 */
    uint32_t echo_backpressure;                       /* echo 队列满 */
    uint32_t ep_in_recovery;                          /* Reserved; always zero */
    uint32_t ep_in_submit_fail;                       /* usbd_ep_start_write 提交失败 */
    uint32_t busoff_total;                            /* 进入 bus-off 次数 */
    uint32_t tx_failed_ch[GS_USB_CAN_CHANNEL_COUNT];  /* 分通道 TX 发送失败 */
} __attribute__((packed));

/* USB EP buffers */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) uint8_t ep_out_buffer[GS_USB_MAX_PACKET_SIZE];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) uint8_t ep_in_buffer[GS_USB_MAX_PACKET_SIZE];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX __attribute__((aligned(32))) uint8_t ep_in_dma_buffer[GS_USB_MAX_PACKET_SIZE];

/* Setup Request variables (MUST be in non-cacheable RAM for USB DMA) */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX struct gs_device_config dev_config;
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX struct gs_device_bt_const bt_const;
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static struct gs_device_bt_const_extended bt_const_ext;

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
static char gs_usb_string_serial[33];

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

static void gs_usb_build_serial(void)
{
    uint32_t uid[4];
    const uint8_t *bytes = (const uint8_t *)uid;
    bool valid = false;
    int i;

    for (i = 0; i < 4; i++) {
        uid[i] = otp_read_from_shadow(OTP_TABLE_DIE_TRACE_OFFSET + i);
        valid = valid || (uid[i] != 0U);
    }
    if (!valid) {
        for (i = 0; i < 4; i++) {
            uid[i] = otp_read_from_shadow(OTP_TABLE_UUID_OFFSET + i);
            valid = valid || (uid[i] != 0U);
        }
    }
    if (!valid) {
        snprintf(gs_usb_string_serial, sizeof(gs_usb_string_serial), "%s", "202604070001");
        return;
    }
    for (i = 0; i < 16; i++) {
        snprintf(&gs_usb_string_serial[i * 2], 3, "%02x", bytes[i]);
    }
}

#define TX_RING_SIZE_PER_CH      128U
#define GS_USB_TX_TIMEOUT_US     20000U
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

/* Legacy .noncacheable is NOLOAD; initialize the pool before enabling USB. */
USB_NOCACHE_RAM_SECTION static struct gs_can_channel mcan_channels[4];

static const uint32_t mcan_irqs[GS_USB_CAN_CHANNEL_COUNT] = {
    BOARD_CAN0_IRQn,
    BOARD_CAN1_IRQn,
    BOARD_CAN2_IRQn,
    BOARD_CAN3_IRQn
};
USB_NOCACHE_RAM_SECTION static volatile uint32_t channel_generation[GS_USB_CAN_CHANNEL_COUNT];

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
    volatile bool cancel_requested;
    volatile bool halted;
    bool inflight_valid;
    struct tx_ring_item inflight_item;
    uint8_t marker;
};

/* All state shared by the USB IRQ and the CAN/main-loop paths lives in the
 * non-cacheable pool.  This keeps flag publication independent of cache
 * maintenance and makes the IRQ ownership explicit. */
struct gs_usb_flag_pool {
    bool ep_in_busy;
    uint64_t ep_in_start_us;
    uint32_t ep_in_submit_fail_count;
    uint32_t ep_in_transfer_len;
    bool ep_in_complete_pending;
    bool ep_out_ready;
    uint32_t ep_out_received_len;
    int32_t ep_out_start_ret;
    uint32_t ep_out_submit_fail_count;
    bool enumeration_done;
    bool is_connected;
    bool out_armed;
    uint8_t in_queue_kind;
    uint32_t in_q_head;
    uint32_t in_q_tail;
    uint32_t echo_q_head;
    uint32_t echo_q_tail;
};

USB_NOCACHE_RAM_SECTION static volatile struct gs_usb_flag_pool gs_usb_flags;
#define ep_in_busy                 (gs_usb_flags.ep_in_busy)
#define ep_in_start_us             (gs_usb_flags.ep_in_start_us)
#define ep_in_submit_fail_count    (gs_usb_flags.ep_in_submit_fail_count)
#define ep_in_transfer_len         (gs_usb_flags.ep_in_transfer_len)
#define ep_in_complete_pending    (gs_usb_flags.ep_in_complete_pending)
#define ep_out_ready               (gs_usb_flags.ep_out_ready)
#define ep_out_received_len        (gs_usb_flags.ep_out_received_len)
#define gs_usb_out_start_ret       (gs_usb_flags.ep_out_start_ret)
#define gs_usb_out_submit_fail_count (gs_usb_flags.ep_out_submit_fail_count)
#define gs_usb_enumeration_done    (gs_usb_flags.enumeration_done)
#define gs_usb_is_connected        (gs_usb_flags.is_connected)
#define gs_usb_out_armed           (gs_usb_flags.out_armed)
#define ep_in_queue_kind           (gs_usb_flags.in_queue_kind)
#define in_q_head                  (gs_usb_flags.in_q_head)
#define in_q_tail                  (gs_usb_flags.in_q_tail)
#define echo_q_head                (gs_usb_flags.echo_q_head)
#define echo_q_tail                (gs_usb_flags.echo_q_tail)

USB_NOCACHE_RAM_SECTION static uint32_t ep_out_generation[GS_USB_CAN_CHANNEL_COUNT];
static volatile uint32_t ep_in_bad_completion_count = 0U;
static volatile uint64_t ep_in_last_completion_us = 0U;
static volatile uint64_t ep_out_last_completion_us = 0U;
volatile uint32_t out_count = 0;

uint32_t gs_usb_can_clk = 80000000; // Will be runtime detected

volatile uint32_t out_drop = 0;      /* kept as aggregate for the human heartbeat */
static volatile uint32_t out_drop_ring = 0U;
static volatile uint32_t out_drop_invalid = 0U;
static volatile uint32_t tx_failed_count = 0U;
static volatile uint32_t tx_failed_ch[GS_USB_CAN_CHANNEL_COUNT] = {0U};
static volatile uint32_t tx_arb_lost_count = 0U;
static volatile uint32_t tx_timeout_cancel_count = 0U;
static volatile uint32_t tx_halted_count = 0U;
static volatile uint32_t busoff_event_count = 0U;
static uint32_t gs_usb_tx_debug_count = 0;
volatile uint32_t gs_usb_reset_count = 0;
volatile uint32_t gs_usb_configured_count = 0;
volatile uint32_t gs_usb_connected_count = 0;
volatile uint32_t gs_usb_disconnected_count = 0;
volatile uint32_t gs_usb_suspend_count = 0;
volatile uint32_t gs_usb_resume_count = 0;
volatile uint32_t gs_usb_main_loop_count = 0U;
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
    uint32_t generation;
};

static struct in_msg in_queue[IN_QUEUE_SIZE];
static struct in_msg echo_queue[IN_QUEUE_SIZE];
static volatile uint32_t in_rx_dropped = 0;
static volatile uint32_t in_rx_dropped_ch[GS_USB_CAN_CHANNEL_COUNT];
static volatile uint32_t in_rx_queue_peak = 0;
static volatile uint32_t in_rx_queue_peak_interval = 0;
static volatile uint32_t mcan_rxfifo0_lost_events[GS_USB_CAN_CHANNEL_COUNT];
static volatile uint32_t ep_in_completed = 0;
static volatile uint32_t ep_in_rx_completed = 0;
static volatile uint32_t ep_in_completed_bytes = 0;
static volatile uint32_t echo_backpressure = 0;
enum gs_usb_in_queue_kind {
    GS_USB_IN_QUEUE_NONE = 0,
    GS_USB_IN_QUEUE_ECHO,
    GS_USB_IN_QUEUE_RX,
};
USB_NOCACHE_RAM_SECTION static struct tx_ring tx_rings[GS_USB_CAN_CHANNEL_COUNT];
USB_NOCACHE_RAM_SECTION static struct can_tx_state can_tx_states[GS_USB_CAN_CHANNEL_COUNT];
static uint8_t tx_marker_seq = 1U;

/* Match the installed SDK validator, which includes Sync_Seg in its SEG1 max. */
#define GS_USB_NOMINAL_SEG1_MAX 255U
#define GS_USB_DATA_SEG1_MAX 31U

static void gs_usb_get_bt_const(struct gs_device_bt_const *caps, bool data_phase)
{
    caps->feature = GS_CAN_FEATURE_LOOP_BACK | GS_CAN_FEATURE_LISTEN_ONLY |
                    GS_CAN_FEATURE_ONE_SHOT | GS_CAN_FEATURE_FD | GS_CAN_FEATURE_BT_CONST_EXT;
    caps->fclk_can = gs_usb_can_clk;
    caps->tseg1_min = 1U;
    caps->tseg1_max = data_phase ? GS_USB_DATA_SEG1_MAX : GS_USB_NOMINAL_SEG1_MAX;
    caps->tseg2_min = 2U;
    caps->tseg2_max = data_phase ? 16U : 128U;
    caps->sjw_max = data_phase ? 16U : 128U;
    caps->brp_min = 1U;
    caps->brp_max = data_phase ? 32U : 512U;
    caps->brp_inc = 1U;
}

static int gs_usb_set_bittiming(uint16_t ch, bool data_phase, const struct gs_device_bittiming *bt)
{
    struct gs_device_bt_const caps;
    mcan_bit_timing_param_t timing = {0};
    uint64_t seg1 = (uint64_t)bt->prop_seg + bt->phase_seg1;
    gs_usb_get_bt_const(&caps, data_phase);
    if (ch >= GS_USB_CAN_CHANNEL_COUNT || mcan_channels[ch].is_open ||
        bt->brp < caps.brp_min || bt->brp > caps.brp_max ||
        seg1 < caps.tseg1_min || seg1 > caps.tseg1_max ||
        bt->phase_seg2 < caps.tseg2_min || bt->phase_seg2 > caps.tseg2_max ||
        bt->sjw == 0U || bt->sjw > caps.sjw_max ||
        bt->sjw > seg1 || bt->sjw > bt->phase_seg2) {
        GS_USB_LOG("[GS_USB] reject %s timing ch=%u prop=%lu seg1=%lu seg2=%lu sjw=%lu brp=%lu open=%u clk=%lu\r\n",
                   data_phase ? "data" : "nominal", (unsigned)ch,
                   (unsigned long)bt->prop_seg, (unsigned long)bt->phase_seg1,
                   (unsigned long)bt->phase_seg2, (unsigned long)bt->sjw,
                   (unsigned long)bt->brp,
                   (ch < GS_USB_CAN_CHANNEL_COUNT && mcan_channels[ch].is_open) ? 1U : 0U,
                   (unsigned long)gs_usb_can_clk);
        return -1;
    }
    timing.prescaler = (uint16_t)bt->brp;
    timing.num_seg1 = (uint16_t)seg1;
    timing.num_seg2 = (uint16_t)bt->phase_seg2;
    timing.num_sjw = (uint8_t)bt->sjw;
    if (data_phase) {
        mcan_channels[ch].config.canfd_timing = timing;
        mcan_channels[ch].have_canfd_timing = true;
    } else {
        mcan_channels[ch].config.can_timing = timing;
        mcan_channels[ch].have_can_timing = true;
    }
    return 0;
}

static void gs_usb_configure_tdc(mcan_config_t *config)
{
    const mcan_bit_timing_param_t *data = &config->canfd_timing;
    config->enable_tdc = config->enable_canfd &&
                         data->prescaler >= 1U && data->prescaler <= 2U;
    config->canfd_timing.enable_tdc = config->enable_tdc;
    /* TDCO is measured in CAN source-clock periods, not prescaled TQ. */
    config->tdc_config.ssp_offset = config->enable_tdc ?
        (1U + data->num_seg1) * data->prescaler : 0U;
    config->tdc_config.filter_window_length = 0U;
}

static void gs_usb_kick_can_tx(uint8_t ch);
static void gs_usb_service_tx_timeouts(void);
static void gs_usb_service_tx_complete_poll(void);
static void gs_usb_service_out(void);
static uint64_t gs_usb_get_time_us(void);
static void gs_usb_print_rx_diagnostics(uint64_t now_us);
static void gs_usb_print_counters_json(uint64_t now_us);
static uint32_t gs_usb_queue_depth(volatile uint32_t head, volatile uint32_t tail);
static void gs_usb_start_next_in_locked(void);
static void gs_usb_arm_out(void);
static void gs_usb_discard_stale_in(struct in_msg *queue, volatile uint32_t *head,
                                    volatile uint32_t *tail);

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

/* Bounded logging: no UART access from an ISR or an IRQ-masked section. */
static char gs_usb_log_ring[16][192];
static uint8_t gs_usb_log_head, gs_usb_log_tail;
static volatile uint32_t gs_usb_log_dropped;
static uint64_t gs_usb_log_next_us;

static void gs_usb_log(const char *fmt, ...)
{
    uint32_t level = gs_usb_irq_save();
    uint8_t next = (gs_usb_log_head + 1U) % 16U;
    if (next == gs_usb_log_tail) {
        gs_usb_log_dropped++;
    } else {
        va_list args;
        va_start(args, fmt);
        vsnprintf(gs_usb_log_ring[gs_usb_log_head], 192U, fmt, args);
        va_end(args);
        gs_usb_log_head = next;
    }
    gs_usb_irq_restore(level);
}

static void gs_usb_drain_log(uint64_t now_us)
{
    char line[192];
    if (now_us < gs_usb_log_next_us) {
        return;
    }
    uint32_t level = gs_usb_irq_save();
    if (gs_usb_log_head == gs_usb_log_tail) {
        gs_usb_irq_restore(level);
        return;
    }
    memcpy(line, gs_usb_log_ring[gs_usb_log_tail], sizeof(line));
    gs_usb_log_tail = (gs_usb_log_tail + 1U) % 16U;
    gs_usb_irq_restore(level);
    gs_usb_log_next_us = now_us + 10000U;
    CONFIG_USB_PRINTF("%s", line);
}

static uint64_t gs_usb_retry_next_us;
static bool gs_usb_halt_reported[GS_USB_CAN_CHANNEL_COUNT];
static uint64_t gs_usb_submit_log_us[GS_USB_CAN_CHANNEL_COUNT];
static uint8_t gs_usb_logged_error_state[GS_USB_CAN_CHANNEL_COUNT];

static void gs_usb_retry_submissions(uint64_t now_us)
{
    if (now_us < gs_usb_retry_next_us) {
        return;
    }
    gs_usb_retry_next_us = now_us + 1000U;
    uint32_t level = gs_usb_irq_save();
    if (gs_usb_enumeration_done) {
        gs_usb_arm_out();
        gs_usb_start_next_in_locked();
    }
    gs_usb_irq_restore(level);
}

static bool enqueue_in_msg(const void *data, uint32_t len)
{
    uint32_t level = gs_usb_irq_save();
    uint32_t echo_id;
    struct in_msg *queue;
    volatile uint32_t *head;
    volatile uint32_t *tail;
    uint32_t next;
    bool is_echo;

    if (len < 12U || len > sizeof(in_queue[0].data)) {
        gs_usb_irq_restore(level);
        return false;
    }
    uint8_t channel = ((const struct gs_host_frame *)data)->channel;
    if (!gs_usb_enumeration_done || channel >= GS_USB_CAN_CHANNEL_COUNT ||
        !mcan_channels[channel].is_open) {
        gs_usb_irq_restore(level);
        return false;
    }

    memcpy(&echo_id, data, sizeof(echo_id));
    is_echo = echo_id != 0xFFFFFFFFU;
    queue = is_echo ? echo_queue : in_queue;
    head = is_echo ? &echo_q_head : &in_q_head;
    tail = is_echo ? &echo_q_tail : &in_q_tail;
    next = (*head + 1U) % IN_QUEUE_SIZE;
    if (next == *tail) {
        if (is_echo) {
            echo_backpressure++;
        } else {
            in_rx_dropped++;
            uint8_t ch = ((const struct gs_host_frame *)data)->channel;
            if (ch < GS_USB_CAN_CHANNEL_COUNT) {
                in_rx_dropped_ch[ch]++;
            }
        }
        gs_usb_irq_restore(level);
        return false;
    }

    memcpy(queue[*head].data, data, len);
    queue[*head].len = len;
    queue[*head].generation = channel_generation[channel];
    *head = next;
    if (!is_echo) {
        uint32_t depth = gs_usb_queue_depth(*head, *tail);
        if (depth > in_rx_queue_peak) {
            in_rx_queue_peak = depth;
        }
        if (depth > in_rx_queue_peak_interval) {
            in_rx_queue_peak_interval = depth;
        }
    }
    /* Queue publication and the first USB IN submission happen in the same
     * interrupt-owned critical section.  Later packets are chained by the
     * IN-completion callback. */
    gs_usb_start_next_in_locked();
    gs_usb_irq_restore(level);
    return true;
}

static bool enqueue_in_msg_for_generation(const void *data, uint32_t len, uint32_t generation)
{
    uint32_t level = gs_usb_irq_save();
    bool accepted = false;
    if (len >= 12U && len <= sizeof(in_queue[0].data)) {
        uint8_t ch = ((const struct gs_host_frame *)data)->channel;
        if (ch < GS_USB_CAN_CHANNEL_COUNT && channel_generation[ch] == generation) {
            accepted = enqueue_in_msg(data, len);
        }
    }
    gs_usb_irq_restore(level);
    return accepted;
}

static bool gs_usb_read_rx_for_generation(uint8_t ch, uint32_t generation, mcan_rx_message_t *message)
{
    uint32_t level = gs_usb_irq_save();
    bool received = gs_usb_enumeration_done && mcan_channels[ch].is_open &&
                    channel_generation[ch] == generation &&
                    mcan_read_rxfifo(mcan_channels[ch].base, 0, message) == status_success;
    gs_usb_irq_restore(level);
    return received;
}

/* Called only while no IN transfer owns a queue entry and IRQs are masked. */
static void gs_usb_discard_stale_in(struct in_msg *queue, volatile uint32_t *head,
                                  volatile uint32_t *tail)
{
    while (*head != *tail) {
        uint8_t ch = ((const struct gs_host_frame *)queue[*tail].data)->channel;
        if (ch < GS_USB_CAN_CHANNEL_COUNT && mcan_channels[ch].is_open &&
            queue[*tail].generation == channel_generation[ch]) {
            break;
        }
        *tail = (*tail + 1U) % IN_QUEUE_SIZE;
    }
}

static uint32_t gs_usb_queue_depth(volatile uint32_t head, volatile uint32_t tail)
{
    return (head + IN_QUEUE_SIZE - tail) % IN_QUEUE_SIZE;
}

static void gs_usb_print_rx_diagnostics(uint64_t now_us)
{
    static uint64_t last_us;
    static uint32_t last_completed;
    static uint32_t last_rx_completed;
    static uint32_t last_completed_bytes;
    uint32_t level = gs_usb_irq_save();
    uint32_t completed = ep_in_completed;
    uint32_t rx_completed = ep_in_rx_completed;
    uint32_t completed_bytes = ep_in_completed_bytes;
    uint32_t peak = in_rx_queue_peak;
    uint32_t interval_peak = in_rx_queue_peak_interval;
    uint32_t dropped[GS_USB_CAN_CHANNEL_COUNT];
    uint32_t fifo_lost[GS_USB_CAN_CHANNEL_COUNT];
    uint32_t elapsed_us = last_us == 0U ? 0U : (uint32_t)(now_us - last_us);

    for (uint8_t ch = 0U; ch < GS_USB_CAN_CHANNEL_COUNT; ch++) {
        /* TX IRQ also records RF0L before its existing clear-all operation. */
        if (mcan_channels[ch].is_open &&
            (mcan_get_interrupt_flags(mcan_channels[ch].base) & MCAN_INT_RXFIFO0_MSG_LOST) != 0U) {
            mcan_rxfifo0_lost_events[ch]++;
            mcan_clear_interrupt_flags(mcan_channels[ch].base, MCAN_INT_RXFIFO0_MSG_LOST);
        }
        dropped[ch] = in_rx_dropped_ch[ch];
        fifo_lost[ch] = mcan_rxfifo0_lost_events[ch];
    }
    in_rx_queue_peak_interval = gs_usb_queue_depth(in_q_head, in_q_tail);
    gs_usb_irq_restore(level);

    printf("[GS_USB] RX diag peak=%lu/%lu drop_ch=%lu,%lu,%lu,%lu RF0L_evt=%lu,%lu,%lu,%lu IN/s=%lu RX/s=%lu B/s=%lu\r\n",
           (unsigned long)interval_peak, (unsigned long)peak,
           (unsigned long)dropped[0], (unsigned long)dropped[1],
           (unsigned long)dropped[2], (unsigned long)dropped[3],
           (unsigned long)fifo_lost[0], (unsigned long)fifo_lost[1],
           (unsigned long)fifo_lost[2], (unsigned long)fifo_lost[3],
           (unsigned long)(elapsed_us ? ((uint64_t)(completed - last_completed) * 1000000U / elapsed_us) : 0U),
           (unsigned long)(elapsed_us ? ((uint64_t)(rx_completed - last_rx_completed) * 1000000U / elapsed_us) : 0U),
           (unsigned long)(elapsed_us ? ((uint64_t)(completed_bytes - last_completed_bytes) * 1000000U / elapsed_us) : 0U));
    last_us = now_us;
    last_completed = completed;
    last_rx_completed = rx_completed;
    last_completed_bytes = completed_bytes;
}

/* Snapshot firmware counters for the host query and the periodic JSON line. */
static void gs_usb_fill_counters(struct gs_device_counters *c)
{
    uint32_t level = gs_usb_irq_save();

    memset(c, 0, sizeof(*c));
    c->version = GS_USB_COUNTERS_VERSION;
    c->rx_dropped_total = in_rx_dropped;
    c->out_drop_total = out_drop;
    c->out_drop_ring = out_drop_ring;
    c->out_drop_invalid = out_drop_invalid;
    c->tx_failed = tx_failed_count;
    c->tx_arb_lost = tx_arb_lost_count;
    c->tx_timeout_cancel = tx_timeout_cancel_count;
    c->tx_halted = tx_halted_count;
    c->echo_backpressure = echo_backpressure;
    c->ep_in_recovery = 0U; /* Reserved ABI field; no endpoint recovery. */
    c->ep_in_submit_fail = ep_in_submit_fail_count;
    c->busoff_total = busoff_event_count;
    for (uint8_t ch = 0U; ch < GS_USB_CAN_CHANNEL_COUNT; ch++) {
        c->rx_dropped_ch[ch] = in_rx_dropped_ch[ch];
        c->rxfifo0_lost[ch] = mcan_rxfifo0_lost_events[ch];
        c->tx_failed_ch[ch] = tx_failed_ch[ch];
    }
    gs_usb_irq_restore(level);
}

/* Machine-parsable counters snapshot (one JSON object per second on the UART).
 * Kept separate from the human-readable heartbeat so the schema stays stable;
 * RF0L (hardware) and rx_drop (software queue) are reported separately. */
static void gs_usb_print_counters_json(uint64_t now_us)
{
    struct gs_device_counters c;

    gs_usb_fill_counters(&c);
    printf("[GS_USB] counters {\"t_ms\":%lu,\"ver\":%lu,\"rx_drop\":%lu,\"rx_drop_ch\":[%lu,%lu,%lu,%lu],"
           "\"rxfifo0_lost\":[%lu,%lu,%lu,%lu],\"out_drop\":%lu,\"out_drop_ring\":%lu,\"out_drop_invalid\":%lu,"
           "\"tx_failed\":%lu,\"tx_failed_ch\":[%lu,%lu,%lu,%lu],\"tx_arb_lost\":%lu,\"tx_timeout\":%lu,\"tx_halted\":%lu,\"echo_bp\":%lu,"
           "\"ep_in_rec\":%lu,\"ep_in_fail\":%lu,\"busoff\":%lu}\r\n",
           (unsigned long)(now_us / 1000U),
           (unsigned long)c.version,
           (unsigned long)c.rx_dropped_total,
           (unsigned long)c.rx_dropped_ch[0], (unsigned long)c.rx_dropped_ch[1],
           (unsigned long)c.rx_dropped_ch[2], (unsigned long)c.rx_dropped_ch[3],
           (unsigned long)c.rxfifo0_lost[0], (unsigned long)c.rxfifo0_lost[1],
           (unsigned long)c.rxfifo0_lost[2], (unsigned long)c.rxfifo0_lost[3],
           (unsigned long)c.out_drop_total,
           (unsigned long)c.out_drop_ring,
           (unsigned long)c.out_drop_invalid,
           (unsigned long)c.tx_failed,
           (unsigned long)c.tx_failed_ch[0], (unsigned long)c.tx_failed_ch[1],
           (unsigned long)c.tx_failed_ch[2], (unsigned long)c.tx_failed_ch[3],
           (unsigned long)c.tx_arb_lost,
           (unsigned long)c.tx_timeout_cancel,
           (unsigned long)c.tx_halted,
           (unsigned long)c.echo_backpressure,
           (unsigned long)c.ep_in_recovery,
           (unsigned long)c.ep_in_submit_fail,
           (unsigned long)c.busoff_total);
}

/* Called with IRQs masked. USB IN is started when a producer publishes a
 * packet or by the preceding IN-completion callback; the main loop does not
 * poll an endpoint status flag. */
static void gs_usb_start_next_in_locked(void)
{
    if (!gs_usb_enumeration_done) {
        return;
    }
    USB_Type *usb = (USB_Type *)HPM_USB0;
    const uint32_t ep_mask = 1UL << (16U + (GS_USB_EP_IN & 0x0FU));
    if (!ep_in_busy &&
        ((usb->ENDPTCOMPLETE | usb->ENDPTSTAT | usb->ENDPTPRIME) & ep_mask) != 0U) {
        return;
    }
    if (!ep_in_busy) {
        gs_usb_discard_stale_in(echo_queue, &echo_q_head, &echo_q_tail);
        gs_usb_discard_stale_in(in_queue, &in_q_head, &in_q_tail);
    }

    if (!ep_in_busy &&
        (echo_q_head != echo_q_tail || in_q_head != in_q_tail)) {
        bool use_echo = echo_q_head != echo_q_tail;
        struct in_msg *queue = use_echo ? echo_queue : in_queue;
        uint32_t tail = use_echo ? echo_q_tail : in_q_tail;
        uint32_t len = queue[tail].len;
        int start_ret;

        memcpy(ep_in_dma_buffer, queue[tail].data, len);
        ep_in_busy = true;
        ep_in_queue_kind = use_echo ? GS_USB_IN_QUEUE_ECHO : GS_USB_IN_QUEUE_RX;
        ep_in_start_us = gs_usb_get_time_us();
        ep_in_transfer_len = len;
        start_ret = usbd_ep_start_write(0, GS_USB_EP_IN, ep_in_dma_buffer, len);
        if (start_ret != 0) {
            ep_in_busy = false;
            ep_in_queue_kind = GS_USB_IN_QUEUE_NONE;
            ep_in_start_us = 0U;
            ep_in_transfer_len = 0U;
            ep_in_submit_fail_count++;
        }
    }
}

static uint64_t gs_usb_get_time_us(void)
{
    uint64_t cycles;

    if (hpm_core_clock == 0U) {
        return 0U;
    }

    cycles = hpm_csr_get_core_mcycle();
    return gs_usb_cycles_to_us(cycles, hpm_core_clock);
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


static bool tx_ring_peek(uint8_t ch, struct tx_ring_item *item)
{
    struct tx_ring *ring = &tx_rings[ch];
    uint32_t level = gs_usb_irq_save();

    if (ring->count == 0U) {
        gs_usb_irq_restore(level);
        return false;
    }

    *item = ring->items[ring->tail];
    gs_usb_irq_restore(level);
    return true;
}

static void tx_ring_drop_head(uint8_t ch)
{
    struct tx_ring *ring = &tx_rings[ch];
    uint32_t level = gs_usb_irq_save();

    if (ring->count != 0U) {
        ring->tail = (uint16_t)((ring->tail + 1U) % TX_RING_SIZE_PER_CH);
        ring->count--;
    }
    gs_usb_irq_restore(level);
}

static uint16_t tx_ring_count(uint8_t ch)
{
    uint32_t level = gs_usb_irq_save();
    uint16_t count = tx_rings[ch].count;
    gs_usb_irq_restore(level);
    return count;
}

static void gs_usb_arm_out(void)
{
    uint32_t level = gs_usb_irq_save();
    if (gs_usb_enumeration_done && !gs_usb_out_armed && !ep_out_ready) {
        int start_ret = usbd_ep_start_read(0, GS_USB_EP_OUT, ep_out_buffer, GS_USB_MAX_PACKET_SIZE);
        gs_usb_out_start_ret = start_ret;
        gs_usb_out_armed = start_ret == 0;
        if (start_ret != 0) {
            gs_usb_out_submit_fail_count++;
        }
    }
    gs_usb_irq_restore(level);
}



static bool gs_usb_finish_tx(uint8_t ch, bool success,
                             uint32_t expected_fifo_index,
                             uint8_t expected_marker)
{
    struct tx_ring_item item;
    uint32_t level;

    if (ch >= GS_USB_CAN_CHANNEL_COUNT) {
        return false;
    }

    level = gs_usb_irq_save();
    if (can_tx_states[ch].halted || !can_tx_states[ch].busy ||
        !can_tx_states[ch].inflight_valid ||
        can_tx_states[ch].fifo_index != expected_fifo_index ||
        can_tx_states[ch].marker != expected_marker) {
        gs_usb_irq_restore(level);
        return false;
    }

    if (expected_fifo_index < 32U &&
        mcan_is_transmit_request_pending(mcan_channels[ch].base, expected_fifo_index)) {
        gs_usb_irq_restore(level);
        return false;
    }

    item = can_tx_states[ch].inflight_item;
    if (!enqueue_in_msg(&item.host_frame, item.frame_len)) {
        gs_usb_irq_restore(level);
        return false;
    }

    if (expected_fifo_index < 32U) {
        mcan_disable_txbuf_transmission_interrupt(mcan_channels[ch].base,
                                                  (1UL << expected_fifo_index));
        mcan_disable_txbuf_cancel_finish_interrupt(mcan_channels[ch].base,
                                                   (1UL << expected_fifo_index));
    }

    if (!success) {
        /* DAR=1: a failed/arbitration-lost frame sets TXBCF.  Distinguish the
         * firmware's own 20 ms timeout cancel from a bus arbitration loss. */
        tx_failed_count++;
        tx_failed_ch[ch]++;
        if (can_tx_states[ch].cancel_requested) {
            tx_timeout_cancel_count++;
        } else {
            tx_arb_lost_count++;
        }
    }
    can_tx_states[ch].busy = false;
    can_tx_states[ch].fifo_index = 0;
    can_tx_states[ch].start_us = 0U;
    can_tx_states[ch].cancel_requested = false;
    can_tx_states[ch].marker = 0U;
    can_tx_states[ch].inflight_valid = false;

    /* Internal hardware loopback supplies RX; only the TX echo is synthesized. */
    gs_usb_irq_restore(level);

    gs_usb_kick_can_tx(ch);
    return true;
}

static void gs_usb_kick_can_tx(uint8_t ch)
{
    struct tx_ring_item item;
    mcan_tx_frame_t tx_frame = {0};
    uint32_t fifo_index = 0;
    uint32_t level;
    hpm_stat_t tx_stat;

    if (ch >= GS_USB_CAN_CHANNEL_COUNT) {
        return;
    }

    level = gs_usb_irq_save();
    if (can_tx_states[ch].halted || can_tx_states[ch].busy || !mcan_channels[ch].is_open) {
        gs_usb_irq_restore(level);
        return;
    }

    if (!tx_ring_peek(ch, &item)) {
        gs_usb_irq_restore(level);
        return;
    }

    bool fd = (item.host_frame.flags & GS_CAN_FLAG_FD) != 0U;
    bool invalid = (item.host_frame.can_id & CAN_ERR_FLAG) != 0U ||
        item.host_frame.can_dlc > (fd ? 15U : 8U) ||
        (fd && (!mcan_channels[ch].config.enable_canfd ||
                (item.host_frame.can_id & 0x40000000U) != 0U)) ||
        (!fd && (item.host_frame.flags & (GS_CAN_FLAG_BRS | GS_CAN_FLAG_ESI)) != 0U) ||
        (item.host_frame.flags & ~(GS_CAN_FLAG_FD | GS_CAN_FLAG_BRS | GS_CAN_FLAG_ESI)) != 0U;
    if (!invalid) {
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
            uint64_t now = gs_usb_get_time_us();
            if (now >= gs_usb_submit_log_us[ch]) {
                gs_usb_submit_log_us[ch] = now + 1000000U;
            GS_USB_LOG("[GS_USB] TX submit failed ch=%u stat=%ld depth=%u\r\n",
                       (unsigned)ch,
                       (long)tx_stat,
                       (unsigned)tx_ring_count(ch));
            }
            gs_usb_irq_restore(level);
            return;
        }

        tx_ring_drop_head(ch);
        can_tx_states[ch].busy = true;
        can_tx_states[ch].fifo_index = fifo_index;
        can_tx_states[ch].start_us = gs_usb_get_time_us();
        can_tx_states[ch].cancel_requested = false;
        s_gs_can_led_ticks[ch] = GS_USB_LED_ON_TICKS;
        can_tx_states[ch].marker = tx_marker_seq++;
        can_tx_states[ch].inflight_item = item;
        can_tx_states[ch].inflight_valid = true;
        mcan_enable_txbuf_transmission_interrupt(mcan_channels[ch].base, (1UL << fifo_index));
        mcan_enable_txbuf_cancel_finish_interrupt(mcan_channels[ch].base, (1UL << fifo_index));
        GS_USB_TX_TRACE("[GS_USB] TX submit ch=%u fifo=%lu depth=%u\r\n",
                   (unsigned)ch,
                   (unsigned long)fifo_index,
                   (unsigned)tx_ring_count(ch));
    } else {
        /* Retain the request under echo backpressure, then retire it once. */
        if (!enqueue_in_msg(&item.host_frame, item.frame_len)) {
            gs_usb_irq_restore(level);
            return;
        }
        tx_ring_drop_head(ch);
        out_drop++;
        out_drop_invalid++;
    }
    gs_usb_irq_restore(level);
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

/* USB IRQ owns the OUT buffer through parsing and the next read submission. */
void gs_usb_ep_out_handler(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    uint32_t level = gs_usb_irq_save();
    if (!gs_usb_enumeration_done || !gs_usb_out_armed || ep_out_ready) {
        gs_usb_irq_restore(level);
        return;
    }
    gs_usb_out_armed = false;
    if (nbytes > sizeof(ep_out_buffer)) {
        out_drop++;
        out_drop_invalid++;
        gs_usb_irq_restore(level);
        return;
    }
    ep_out_received_len = nbytes;
    for (uint8_t ch = 0; ch < GS_USB_CAN_CHANNEL_COUNT; ch++) {
        ep_out_generation[ch] = channel_generation[ch];
    }
    ep_out_last_completion_us = gs_usb_get_time_us();
    out_count++;
    ep_out_ready = true;
    gs_usb_irq_restore(level);
    gs_usb_service_out();
}

static void gs_usb_service_out(void)
{
    uint32_t level = gs_usb_irq_save();
    uint32_t offset = 0;
    if (!gs_usb_enumeration_done || !ep_out_ready) {
        gs_usb_arm_out();
        gs_usb_irq_restore(level);
        return;
    }
    uint32_t nbytes = ep_out_received_len;
    if (out_count <= 20U) {
        GS_USB_LOG("[GS_USB] EP_OUT #%lu nbytes=%u configured=%u\r\n",
               (unsigned long)out_count,
               (unsigned)nbytes,
               gs_usb_enumeration_done ? 1U : 0U);
    }
    if (out_count <= 45) {
        GS_USB_LOG("[GS_USB] ep_out %u bytes (count=%lu)\r\n",
                   (unsigned)nbytes,
                   (unsigned long)out_count);
    }

    gs_usb_out_armed = false;

    while (offset + 12U <= nbytes) {
        struct gs_host_frame *hf = (struct gs_host_frame *)(ep_out_buffer + offset);
        struct tx_ring_item item = {0};
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

        if (ch >= GS_USB_CAN_CHANNEL_COUNT || !mcan_channels[ch].is_open ||
            ep_out_generation[ch] != channel_generation[ch]) {
            offset += host_stride;
            continue;
        }

        item.frame_len = frame_len;
        item.loopback = (mcan_channels[ch].mode_flags & GS_CAN_MODE_LOOP_BACK) != 0U;
        memcpy(&item.host_frame, hf, frame_len);

        if (gs_usb_tx_debug_count < 32U) {
            GS_USB_LOG("[GS_USB] TX request #%lu ch=%u id=0x%08lx dlc=%u flags=0x%02x len=%lu\r\n",
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
            out_drop_ring++;
            break;
        }

        offset += host_stride;
    }

    ep_out_ready = false;
    ep_out_received_len = 0U;
    gs_usb_arm_out();
    gs_usb_irq_restore(level);
}

/* EP IN Callback (MCU -> PC) */
void gs_usb_ep_in_handler(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    uint32_t level = gs_usb_irq_save();
    if (!gs_usb_enumeration_done || !ep_in_busy || ep_in_complete_pending ||
        nbytes == 0U || nbytes != ep_in_transfer_len) {
        ep_in_bad_completion_count++;
        gs_usb_irq_restore(level);
        return;
    }
    ep_in_last_completion_us = gs_usb_get_time_us();
    ep_in_complete_pending = true;
    if (ep_in_busy) {
        ep_in_completed++;
        ep_in_completed_bytes += ep_in_transfer_len;
        if (ep_in_queue_kind == GS_USB_IN_QUEUE_RX) {
            ep_in_rx_completed++;
        }
    }
    if (ep_in_busy && ep_in_queue_kind == GS_USB_IN_QUEUE_ECHO &&
        echo_q_head != echo_q_tail) {
        echo_q_tail = (echo_q_tail + 1U) % IN_QUEUE_SIZE;
    } else if (ep_in_busy && ep_in_queue_kind == GS_USB_IN_QUEUE_RX &&
               in_q_head != in_q_tail) {
        in_q_tail = (in_q_tail + 1U) % IN_QUEUE_SIZE;
    }
    ep_in_busy = false;
    ep_in_queue_kind = GS_USB_IN_QUEUE_NONE;
    ep_in_start_us = 0U;
    ep_in_transfer_len = 0U;
    ep_in_complete_pending = false;
    gs_usb_start_next_in_locked();
    gs_usb_irq_restore(level);
}

static bool gs_usb_stop_and_clear_channel(uint8_t ch)
{
    if (ch >= GS_USB_CAN_CHANNEL_COUNT) {
        return false;
    }

    uint32_t level = gs_usb_irq_save();
    intc_m_disable_irq(mcan_irqs[ch]);
    if (mcan_channels[ch].is_open) {
        mcan_deinit(mcan_channels[ch].base);
    }
    mcan_channels[ch].is_open = false;
    channel_generation[ch]++;
    mcan_channels[ch].error_state = 0;
    mcan_channels[ch].busoff_detect_us = 0U;
    mcan_channels[ch].busoff_recovery_us = 0U;
    mcan_channels[ch].error_passive_detect_us = 0U;
    tx_rings[ch].head = 0U;
    tx_rings[ch].tail = 0U;
    tx_rings[ch].count = 0U;
    can_tx_states[ch].busy = false;
    can_tx_states[ch].fifo_index = 0U;
    can_tx_states[ch].start_us = 0U;
    can_tx_states[ch].cancel_requested = false;
    can_tx_states[ch].halted = false;
    gs_usb_halt_reported[ch] = false;
    gs_usb_logged_error_state[ch] = 0U;
    can_tx_states[ch].marker = 0U;
    can_tx_states[ch].inflight_valid = false;
    /* Keep the USB buffer owned until its completion IRQ. The IN callback
     * discards queued frames from older channel generations. */
    gs_usb_irq_restore(level);
    return true;
}

static void gs_usb_bus_event_handler(uint8_t busid, uint8_t event)
{
    uint8_t ch;
    uint32_t level = gs_usb_irq_save();
    (void)busid;

    switch (event) {
    case USBD_EVENT_CONNECTED:
        gs_usb_connected_count++;
        gs_usb_is_connected = true;
        break;
    case USBD_EVENT_DISCONNECTED:
        gs_usb_disconnected_count++;
        gs_usb_is_connected = false;
        gs_usb_enumeration_done = false;
        gs_usb_out_armed = false;
        ep_in_busy = false;
        ep_in_queue_kind = GS_USB_IN_QUEUE_NONE;
        ep_in_start_us = 0U;
        ep_in_transfer_len = 0U;
        ep_in_complete_pending = false;
        ep_out_ready = false;
        ep_out_received_len = 0U;
        in_q_head = 0;
        in_q_tail = 0;
        echo_q_head = 0;
        echo_q_tail = 0;
        for (ch = 0; ch < GS_USB_CAN_CHANNEL_COUNT; ch++) {
            gs_usb_stop_and_clear_channel(ch);
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
        /* SET_CONFIGURATION reaches here from the USB IRQ. */
        gs_usb_enumeration_done = true;
        /* The enumeration flag and the first OUT start are published by the
         * same USB interrupt; there is no main-loop EP recovery path. */
        gs_usb_arm_out();
        break;
    case USBD_EVENT_RESET:
        gs_usb_reset_count++;
        gs_usb_is_connected = false;
        gs_usb_enumeration_done = false;
        gs_usb_out_armed = false;
        ep_in_busy = false;
        ep_in_queue_kind = GS_USB_IN_QUEUE_NONE;
        ep_in_start_us = 0U;
        ep_in_transfer_len = 0U;
        ep_in_complete_pending = false;
        ep_out_ready = false;
        ep_out_received_len = 0U;
        in_q_head = 0;
        in_q_tail = 0;
        echo_q_head = 0;
        echo_q_tail = 0;
        for (ch = 0; ch < GS_USB_CAN_CHANNEL_COUNT; ch++) {
            gs_usb_stop_and_clear_channel(ch);
        }
        break;
    default:
        break;
    }
    gs_usb_irq_restore(level);
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
    uint32_t fifo_index;
    uint32_t fifo_mask;
    uint8_t marker;
    uint32_t level;

    if (ch >= GS_USB_CAN_CHANNEL_COUNT) {
        return;
    }

    flags = mcan_get_interrupt_flags(mcan_channels[ch].base);
    if ((flags & MCAN_INT_RXFIFO0_MSG_LOST) != 0U) {
        mcan_rxfifo0_lost_events[ch]++;
    }
    if ((flags & (MCAN_INT_TX_COMPLETED | MCAN_INT_TX_CANCEL_FINISHED)) == 0U) {
        mcan_clear_interrupt_flags(mcan_channels[ch].base, flags);
        return;
    }

    GS_USB_TX_TRACE("[GS_USB] TX IRQ ch=%u flags=0x%08lx fifo=%lu\r\n",
               (unsigned)ch,
               (unsigned long)flags,
               (unsigned long)can_tx_states[ch].fifo_index);

    mcan_clear_interrupt_flags(mcan_channels[ch].base, flags);

    level = gs_usb_irq_save();
    if (!can_tx_states[ch].busy || !can_tx_states[ch].inflight_valid ||
        can_tx_states[ch].fifo_index >= 32U) {
        gs_usb_irq_restore(level);
        return;
    }
    fifo_index = can_tx_states[ch].fifo_index;
    marker = can_tx_states[ch].marker;
    fifo_mask = 1UL << fifo_index;
    gs_usb_irq_restore(level);

    if ((flags & MCAN_INT_TX_COMPLETED) != 0U &&
        (mcan_channels[ch].base->TXBTO & fifo_mask) != 0U) {
        (void)gs_usb_finish_tx(ch, true, fifo_index, marker);
    } else if ((flags & MCAN_INT_TX_CANCEL_FINISHED) != 0U &&
               (mcan_channels[ch].base->TXBCF & fifo_mask) != 0U) {
        (void)gs_usb_finish_tx(ch, false, fifo_index, marker);
    }
}

static void gs_usb_service_tx_timeouts(void)
{
    uint8_t ch;

    for (ch = 0; ch < GS_USB_CAN_CHANNEL_COUNT; ch++) {
        mcan_protocol_status_t psr;
        mcan_error_count_t ecr;
        uint64_t now_us;
        uint64_t start_us;
        uint32_t fifo_index;
        uint32_t fifo_mask;
        uint8_t marker;
        bool cancel_requested;
        uint32_t level;
        uint32_t generation;

        level = gs_usb_irq_save();
        if (can_tx_states[ch].halted || !can_tx_states[ch].busy ||
            !can_tx_states[ch].inflight_valid || can_tx_states[ch].start_us == 0U ||
            can_tx_states[ch].fifo_index >= 32U) {
            gs_usb_irq_restore(level);
            continue;
        }
        start_us = can_tx_states[ch].start_us;
        fifo_index = can_tx_states[ch].fifo_index;
        marker = can_tx_states[ch].marker;
        cancel_requested = can_tx_states[ch].cancel_requested;
        generation = channel_generation[ch];
        gs_usb_irq_restore(level);

        now_us = gs_usb_get_time_us();
        if (now_us < start_us || (now_us - start_us) < GS_USB_TX_TIMEOUT_US) {
            continue;
        }

        fifo_mask = 1UL << fifo_index;
        level = gs_usb_irq_save();
        if (!gs_usb_enumeration_done || !mcan_channels[ch].is_open ||
            channel_generation[ch] != generation) {
            gs_usb_irq_restore(level);
            continue;
        }
        if (!mcan_is_transmit_request_pending(mcan_channels[ch].base, fifo_index)) {
            bool success = (mcan_channels[ch].base->TXBTO & fifo_mask) != 0U;
            (void)gs_usb_finish_tx(ch, success, fifo_index, marker);
            gs_usb_irq_restore(level);
            continue;
        }
        gs_usb_irq_restore(level);

        if (!cancel_requested) {
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
        }

        if ((now_us - start_us) >= (GS_USB_TX_TIMEOUT_US * 2U)) {
            level = gs_usb_irq_save();
            if (channel_generation[ch] == generation &&
                can_tx_states[ch].busy && can_tx_states[ch].inflight_valid &&
                can_tx_states[ch].fifo_index == fifo_index &&
                can_tx_states[ch].marker == marker &&
                mcan_is_transmit_request_pending(mcan_channels[ch].base, fifo_index)) {
                can_tx_states[ch].halted = true;
                tx_halted_count++;
                mcan_enter_init_mode(mcan_channels[ch].base);
                GS_USB_LOG("[GS_USB] TX halted ch=%u fifo=%lu age_us=%lu; host restart required\r\n",
                       (unsigned)ch, (unsigned long)fifo_index,
                       (unsigned long)(now_us - start_us));
            }
            gs_usb_irq_restore(level);
            continue;
        }

        if (!cancel_requested) {
            level = gs_usb_irq_save();
            if (channel_generation[ch] == generation &&
                can_tx_states[ch].busy && can_tx_states[ch].inflight_valid &&
                can_tx_states[ch].fifo_index == fifo_index &&
                can_tx_states[ch].marker == marker) {
                can_tx_states[ch].cancel_requested = true;
                mcan_cancel_tx_buf_send_request(mcan_channels[ch].base, fifo_index);
            }
            gs_usb_irq_restore(level);
        }
    }
}

static void gs_usb_service_halted(void)
{
    for (uint8_t ch = 0; ch < GS_USB_CAN_CHANNEL_COUNT; ch++) {
        uint32_t level = gs_usb_irq_save();
        if (!gs_usb_enumeration_done || !mcan_channels[ch].is_open ||
            !can_tx_states[ch].halted ||
            (mcan_channels[ch].base->CCCR & MCAN_CCCR_INIT_MASK) == 0U) {
            gs_usb_irq_restore(level);
            continue;
        }
        /* INIT has been acknowledged. Never replay these commands on restart. */
        if (can_tx_states[ch].inflight_valid) {
            struct tx_ring_item *item = &can_tx_states[ch].inflight_item;
            if (enqueue_in_msg(&item->host_frame, item->frame_len)) {
                can_tx_states[ch].inflight_valid = false;
                can_tx_states[ch].busy = false;
                tx_failed_count++;
                tx_failed_ch[ch]++;
            }
        }
        struct tx_ring_item item;
        if (!can_tx_states[ch].inflight_valid && tx_ring_peek(ch, &item) &&
            enqueue_in_msg(&item.host_frame, item.frame_len)) {
            tx_ring_drop_head(ch);
        }
        if (!gs_usb_halt_reported[ch]) {
            struct gs_host_frame error = {0};
            error.channel = ch;
            error.echo_id = 0xFFFFFFFFU;
            error.can_id = CAN_ERR_FLAG | 0x00000001U; /* CAN_ERR_TX_TIMEOUT */
            error.can_dlc = 8U;
            gs_usb_halt_reported[ch] = enqueue_in_msg(&error, 20U);
        }
        gs_usb_irq_restore(level);
    }
}

static void gs_usb_service_tx_complete_poll(void)
{
    uint8_t ch;

    for (ch = 0; ch < GS_USB_CAN_CHANNEL_COUNT; ch++) {
        mcan_tx_event_fifo_elem_t tx_evt;
        uint32_t channel_level = gs_usb_irq_save();

        if (!gs_usb_enumeration_done || !mcan_channels[ch].is_open) {
            gs_usb_irq_restore(channel_level);
            continue;
        }

        {
            uint32_t level = gs_usb_irq_save();
            bool valid = !can_tx_states[ch].halted && can_tx_states[ch].busy &&
                         can_tx_states[ch].inflight_valid &&
                         can_tx_states[ch].fifo_index < 32U;
            uint32_t fifo_index = can_tx_states[ch].fifo_index;
            uint8_t marker = can_tx_states[ch].marker;
            uint32_t fifo_mask = valid ? (1UL << fifo_index) : 0U;
            gs_usb_irq_restore(level);

            if (valid && (mcan_channels[ch].base->TXBTO & fifo_mask) != 0U) {
                (void)gs_usb_finish_tx(ch, true, fifo_index, marker);
            } else if (valid && (mcan_channels[ch].base->TXBCF & fifo_mask) != 0U) {
                (void)gs_usb_finish_tx(ch, false, fifo_index, marker);
            }
        }

        /* Status bits for the current slot are authoritative. Event markers
         * are only drained because an 8-bit marker can wrap and match stale data. */
        uint32_t tx_evt_drained = 0U;
        while (tx_evt_drained < GS_USB_TX_EVT_DRAIN_LIMIT &&
               mcan_read_tx_evt_fifo(mcan_channels[ch].base, &tx_evt) == status_success) {
            tx_evt_drained++;
            GS_USB_TX_TRACE("[GS_USB] TX event ch=%u marker=%u evt=%u fifo=%lu\r\n",
                       (unsigned)ch,
                       (unsigned)tx_evt.message_marker,
                       (unsigned)tx_evt.event_type,
                       (unsigned long)can_tx_states[ch].fifo_index);
        }
        gs_usb_irq_restore(channel_level);
    }
}

static void gs_usb_can_irq(uint8_t ch)
{
    gs_usb_can_irq_count[ch]++;
    gs_usb_handle_can_tx_irq(ch);
}

static int gs_usb_set_mode(uint16_t val, const struct gs_device_mode *mode_req)
{
    if (val >= GS_USB_CAN_CHANNEL_COUNT || mode_req->mode > 1U) {
        return -1;
    }
    if (mode_req->mode == 0U) {
        return gs_usb_stop_and_clear_channel((uint8_t)val) ? 0 : -1;
    }
    if (!mcan_channels[val].is_open && ep_in_busy &&
        ((const struct gs_host_frame *)ep_in_dma_buffer)->channel == val) {
        return -1;
    }
    if (mcan_channels[val].is_open) {
        mcan_protocol_status_t psr;
        mcan_get_protocol_status(mcan_channels[val].base, &psr);
        if (!psr.in_bus_off_state && !can_tx_states[val].halted) {
            return mcan_channels[val].mode_flags == mode_req->flags ? 0 : -1;
        }
        /* START on a failed channel must actually reset its hardware. */
        gs_usb_stop_and_clear_channel((uint8_t)val);
    }
    if (!mcan_channels[val].have_can_timing ||
        ((mode_req->flags & GS_CAN_MODE_FD) != 0U && !mcan_channels[val].have_canfd_timing)) {
        return -1;
    }
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    mcan_msg_buf_attr_t attr;
    if (val == 0) { mcan_msg_buf_attr_t tmp = { (uint32_t)&s_mcan_msg_buf0, sizeof(s_mcan_msg_buf0) }; attr = tmp; }
    else if (val == 1) { mcan_msg_buf_attr_t tmp = { (uint32_t)&s_mcan_msg_buf1, sizeof(s_mcan_msg_buf1) }; attr = tmp; }
    else if (val == 2) { mcan_msg_buf_attr_t tmp = { (uint32_t)&s_mcan_msg_buf2, sizeof(s_mcan_msg_buf2) }; attr = tmp; }
    else { mcan_msg_buf_attr_t tmp = { (uint32_t)&s_mcan_msg_buf3, sizeof(s_mcan_msg_buf3) }; attr = tmp; }
    if (mcan_set_msg_buf_attr(mcan_channels[val].base, &attr) != status_success) {
        return -1;
    }
#endif
    mcan_channels[val].mode_flags = mode_req->flags;
    mcan_bit_timing_param_t saved_can_timing = mcan_channels[val].config.can_timing;
    mcan_bit_timing_param_t saved_canfd_timing = mcan_channels[val].config.canfd_timing;
    mcan_config_t *config = &mcan_channels[val].config;
    mcan_get_default_config(mcan_channels[val].base, config);
    config->use_lowlevel_timing_setting = true;
    config->can_timing = saved_can_timing;
    config->canfd_timing = saved_canfd_timing;
    config->enable_canfd = (mode_req->flags & GS_CAN_MODE_FD) != 0U;
    mcan_get_default_ram_config(mcan_channels[val].base, &config->ram_config, config->enable_canfd);
    config->ram_config.enable_tx_evt_fifo = true;
    config->ram_config.tx_evt_fifo_watermark = 1;
    gs_usb_configure_tdc(config);
    config->enable_non_iso_mode = false;
    config->disable_auto_retransmission = (mode_req->flags & GS_CAN_MODE_ONE_SHOT) != 0U;
    if (mode_req->flags & GS_CAN_MODE_LOOP_BACK) {
        config->mode = mcan_mode_loopback_internal;
    } else if (mode_req->flags & GS_CAN_MODE_LISTEN_ONLY) {
        config->mode = mcan_mode_listen_only;
        config->enable_restricted_operation_mode = true;
    } else {
        config->mode = mcan_mode_normal;
    }
    GS_USB_LOG("[GS_USB] ch=%u clk=%lu nominal=%u/%u/%u/%u data=%u/%u/%u/%u TDC=%u offset=%u\r\n",
               val, (unsigned long)gs_usb_can_clk,
               config->can_timing.prescaler, config->can_timing.num_seg1,
               config->can_timing.num_seg2, config->can_timing.num_sjw,
               config->canfd_timing.prescaler, config->canfd_timing.num_seg1,
               config->canfd_timing.num_seg2, config->canfd_timing.num_sjw,
               config->enable_tdc, config->tdc_config.ssp_offset);
    if (mcan_init(mcan_channels[val].base, config, gs_usb_can_clk) != status_success) {
        GS_USB_LOG("[GS_USB] mcan_init failed ch=%u clk=%lu nominal=%u/%u/%u/%u data=%u/%u/%u/%u fd=%u\r\n",
                   val, (unsigned long)gs_usb_can_clk,
                   config->can_timing.prescaler, config->can_timing.num_seg1,
                   config->can_timing.num_seg2, config->can_timing.num_sjw,
                   config->canfd_timing.prescaler, config->canfd_timing.num_seg1,
                   config->canfd_timing.num_seg2, config->canfd_timing.num_sjw,
                   config->enable_canfd ? 1U : 0U);
        mcan_deinit(mcan_channels[val].base);
        mcan_channels[val].is_open = false;
        return -1;
    }
    /* mcan_get_default_config supplies accept-to-FIFO0 global filters;
     * mcan_init applies them while configuration writes are enabled. */
    mcan_channels[val].error_state = 0;
    mcan_channels[val].busoff_detect_us = 0;
    mcan_enable_interrupts(mcan_channels[val].base, MCAN_INT_TX_COMPLETED | MCAN_INT_TX_CANCEL_FINISHED);
    intc_m_enable_irq_with_priority(mcan_irqs[val], 2);
    tx_rings[val].head = 0;
    tx_rings[val].tail = 0;
    tx_rings[val].count = 0;
    can_tx_states[val].busy = false;
    can_tx_states[val].fifo_index = 0;
    can_tx_states[val].start_us = 0U;
    can_tx_states[val].cancel_requested = false;
    can_tx_states[val].halted = false;
    can_tx_states[val].marker = 0U;
    can_tx_states[val].inflight_valid = false;
    mcan_channels[val].is_open = true;
    return 0;
}

/* Vendor Control Request Handler */
int gs_usb_vendor_request_handler(uint8_t busid, struct usb_setup_packet *setup, uint8_t **data, uint32_t *len)
{
    (void)busid;
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

    /* Preserve the earlier firmware's length-qualified request aliases. */
    bool input = (setup->bmRequestType & USB_REQUEST_DIR_MASK) != 0U;
    if (!input && req == 8U && setup->wLength == sizeof(struct gs_device_bittiming)) {
        req = GS_USB_BREQ_DATA_BITTIMING;
    } else if (input && req == 3U && setup->wLength == sizeof(bt_const)) {
        req = GS_USB_BREQ_BT_CONST;
    } else if (input && (req == 4U || req == 6U) && setup->wLength == 12U) {
        req = GS_USB_BREQ_DEVICE_CONFIG;
    } else if (input && req == 9U && setup->wLength == sizeof(bt_const)) {
        if (val >= GS_USB_CAN_CHANNEL_COUNT) {
            return -1;
        }
        gs_usb_get_bt_const(&bt_const, true);
        *data = (uint8_t *)&bt_const;
        *len = sizeof(bt_const);
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
            if (!input || val >= GS_USB_CAN_CHANNEL_COUNT) {
                return -1;
            }
            gs_usb_get_bt_const(&bt_const, false);
            if (req == GS_USB_BREQ_BT_CONST_EXT) {
                struct gs_device_bt_const data_caps;
                gs_usb_get_bt_const(&data_caps, true);
                bt_const_ext.nominal = bt_const;
                bt_const_ext.dtseg1_min = data_caps.tseg1_min;
                bt_const_ext.dtseg1_max = data_caps.tseg1_max;
                bt_const_ext.dtseg2_min = data_caps.tseg2_min;
                bt_const_ext.dtseg2_max = data_caps.tseg2_max;
                bt_const_ext.dsjw_max = data_caps.sjw_max;
                bt_const_ext.dbrp_min = data_caps.brp_min;
                bt_const_ext.dbrp_max = data_caps.brp_max;
                bt_const_ext.dbrp_inc = data_caps.brp_inc;
                *data = (uint8_t *)&bt_const_ext;
                *len = setup->wLength < sizeof(bt_const_ext) ? setup->wLength : sizeof(bt_const_ext);
            } else {
                *data = (uint8_t *)&bt_const;
                *len = setup->wLength < sizeof(bt_const) ? setup->wLength : sizeof(bt_const);
            }
            break;

        case GS_USB_BREQ_MODE:
            if (input || setup->wLength != sizeof(struct gs_device_mode) ||
                *len != sizeof(struct gs_device_mode) || data == NULL || *data == NULL ||
                gs_usb_set_mode(val, (const struct gs_device_mode *)*data) != 0) {
                return -1;
            }
            *len = 0;
            break;

        case GS_USB_BREQ_BITTIMING:
        case GS_USB_BREQ_DATA_BITTIMING:
            if (input || setup->wLength != sizeof(struct gs_device_bittiming) ||
                *len != sizeof(struct gs_device_bittiming) || data == NULL || *data == NULL) {
                return -1;
            }
            if (gs_usb_set_bittiming(val, req == GS_USB_BREQ_DATA_BITTIMING,
                                    (const struct gs_device_bittiming *)*data) != 0) {
                return -1;
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

        case GS_USB_BREQ_COUNTERS: {
            static struct gs_device_counters counters_snapshot;
            if ((setup->bmRequestType & USB_REQUEST_DIR_MASK) == 0U) {
                *len = 0;
                break;
            }
            gs_usb_fill_counters(&counters_snapshot);
            *data = (uint8_t *)&counters_snapshot;
            *len = (setup->wLength < sizeof(counters_snapshot)) ? setup->wLength : sizeof(counters_snapshot);
            break;
        }

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
    gs_usb_build_serial();
    printf("[GS_USB] serial: %s\r\n", gs_usb_string_serial);
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

static void gs_usb_init_pool(void)
{
    memset(mcan_channels, 0, sizeof(mcan_channels));
    memset((void *)&gs_usb_flags, 0, sizeof(gs_usb_flags));
    memset((void *)channel_generation, 0, sizeof(channel_generation));
    memset(ep_out_generation, 0, sizeof(ep_out_generation));
    memset(tx_rings, 0, sizeof(tx_rings));
    memset(can_tx_states, 0, sizeof(can_tx_states));
    mcan_channels[0].base = HPM_MCAN0;
    mcan_channels[1].base = HPM_MCAN1;
    mcan_channels[2].base = HPM_MCAN2;
    mcan_channels[3].base = HPM_MCAN3;
}

void gs_usb_run(void)
{
    gs_usb_init_pool();
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
    for (uint8_t ch = 0; ch < GS_USB_CAN_CHANNEL_COUNT; ch++) {
        printf("[GS_USB] ch=%u base=0x%08lX\r\n", (unsigned)ch,
               (unsigned long)mcan_channels[ch].base);
    }

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

    can_irq_set_handler(gs_usb_can_irq);
    gs_usb_init();
    printf("[GS_USB] init: gs_usb_init() returned\r\n");

    /* 禁用 Suspend 中断：D+上拉后总线空闲>3ms会反复触发SLI形成死循环 */
    ((USB_Type *)HPM_USB0)->USBINTR &= ~USB_USBINTR_SLE_MASK;

    /* Keep the SDK USB wrapper; CAN callbacks run behind machine IRQ wrappers. */
    intc_m_enable_irq_with_priority(CONFIG_HPM_USBD_IRQn, 3);
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
        /* USB events are dispatched only by the SDK interrupt wrapper. */
        loop_now_us = gs_usb_get_time_us();
        gs_usb_retry_submissions(loop_now_us);
        gs_usb_drain_log(loop_now_us);
        if (CONFIG_GS_USB_PERIODIC_DIAGNOSTICS && (heartbeat_last_us == 0U ||
            (loop_now_us - heartbeat_last_us) >= 1000000U)) {
            heartbeat_last_us = loop_now_us;
            USB_Type *heartbeat_usb = (USB_Type *)HPM_USB0;
            printf("[GS_USB] heartbeat pre-usb loops=%lu prev_stage=%u cfg=%u conn=%u USBSTS=0x%08lx OUT=%lu armed=%u ring=%u,%u,%u,%u busy=%u,%u,%u,%u EQ=%lu RXQ=%lu IN=%u fail=%lu echo_bp=%lu rx_drop=%lu\r\n",
                   (unsigned long)gs_usb_main_loop_count,
                   (unsigned)previous_stage,
                   gs_usb_enumeration_done ? 1U : 0U,
                   gs_usb_is_connected ? 1U : 0U,
                   (unsigned long)heartbeat_usb->USBSTS,
                   (unsigned long)out_count,
                   gs_usb_out_armed ? 1U : 0U,
                   (unsigned)tx_ring_count(0),
                   (unsigned)tx_ring_count(1),
                   (unsigned)tx_ring_count(2),
                   (unsigned)tx_ring_count(3),
                   can_tx_states[0].busy ? 1U : 0U,
                   can_tx_states[1].busy ? 1U : 0U,
                   can_tx_states[2].busy ? 1U : 0U,
                   can_tx_states[3].busy ? 1U : 0U,
                   (unsigned long)gs_usb_queue_depth(echo_q_head, echo_q_tail),
                   (unsigned long)gs_usb_queue_depth(in_q_head, in_q_tail),
                   ep_in_busy ? 1U : 0U,
                   (unsigned long)ep_in_submit_fail_count,
                   (unsigned long)echo_backpressure,
                   (unsigned long)in_rx_dropped);
            gs_usb_print_rx_diagnostics(loop_now_us);
            gs_usb_print_counters_json(loop_now_us);
        }
        gs_usb_main_stage = 1U; /* USB service */
        ewdg_refresh(HPM_EWDG0);
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

        if (!gs_usb_enumeration_done) {
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
        gs_usb_service_halted();
        gs_usb_service_tx_timeouts();

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
                uint32_t channel_level = gs_usb_irq_save();
                if (!gs_usb_enumeration_done || !mcan_channels[i].is_open) {
                    gs_usb_irq_restore(channel_level);
                    continue;
                }
                uint32_t generation = channel_generation[i];
                if ((mcan_get_interrupt_flags(mcan_channels[i].base) & MCAN_INT_RXFIFO0_MSG_LOST) != 0U) {
                    mcan_rxfifo0_lost_events[i]++;
                    mcan_clear_interrupt_flags(mcan_channels[i].base, MCAN_INT_RXFIFO0_MSG_LOST);
                }
                
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
                    if (current_err_state != gs_usb_logged_error_state[i]) {
                        gs_usb_logged_error_state[i] = current_err_state;
                        mcan_error_count_t log_ecr;
                        mcan_get_error_counter(mcan_channels[i].base, &log_ecr);
                        GS_USB_LOG("[GS_USB] ch=%u ERR state: %u -> %u\r\n",
                               (unsigned)i,
                               (unsigned)mcan_channels[i].error_state,
                               (unsigned)current_err_state);
                        if (current_err_state >= 2U) {
                            GS_USB_LOG("[GS_USB] ch=%u counters: TEC=%u REC=%u PSR=0x%08lx DBTP=0x%08lx TDCR=0x%08lx\r\n",
                                   (unsigned)i,
                                   (unsigned)log_ecr.transmit_error_count,
                                   (unsigned)log_ecr.receive_error_count,
                                   (unsigned long)mcan_channels[i].base->PSR,
                                   (unsigned long)mcan_channels[i].base->DBTP,
                                   (unsigned long)mcan_channels[i].base->TDCR);
                        }
                    }
                    struct gs_host_frame err_hf = {0};
                    err_hf.channel = (uint8_t)i;
                    err_hf.echo_id = 0xFFFFFFFFU;
                    err_hf.can_id = CAN_ERR_FLAG | CAN_ERR_CRTL;
                    err_hf.can_dlc = 8;

                    if (current_err_state == 3) { // Bus Off
                        err_hf.can_id |= CAN_ERR_BUSOFF;
                        if (mcan_channels[i].busoff_detect_us == 0U) {
                            busoff_event_count++;
                            mcan_error_count_t ecr;
                            mcan_get_error_counter(mcan_channels[i].base, &ecr);
                            GS_USB_LOG("[GS_USB] ch=%u BUS-OFF: TEC=%u REC=%u PSR=0x%08lx\r\n",
                                   (unsigned)i,
                                   (unsigned)ecr.transmit_error_count,
                                   (unsigned)ecr.receive_error_count,
                                   (unsigned long)mcan_channels[i].base->PSR);
                        }
                        if (enqueue_in_msg(&err_hf, 20U)) {
                            mcan_channels[i].error_state = current_err_state;
                        }
                        /* Retain the first detection time until host restart. */
                        if (mcan_channels[i].busoff_detect_us == 0) {
                            mcan_channels[i].busoff_detect_us = gs_usb_get_time_us();
                        }
                        gs_usb_irq_restore(channel_level);
                        continue;
                    } else if (current_err_state == 2) { // Error Passive
                        mcan_error_count_t ecr;
                        mcan_get_error_counter(mcan_channels[i].base, &ecr);
                        /* REC is 7 bits; ECR.RP reports receive error passive. */
                        if (ecr.receive_error_passive) err_hf.data[1] |= CAN_ERR_CRTL_RX_PASSIVE;
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

                    if (enqueue_in_msg(&err_hf, 20U)) {
                        mcan_channels[i].error_state = current_err_state;
                    }
                }

                /* Recovery is owned by the host MODE request. */
                
                gs_usb_irq_restore(channel_level);
                mcan_rx_message_t rx_msg;
                uint32_t rx_drained = 0U;
                // Attempt to read from RX FIFO 0
                while (rx_drained < GS_USB_RX_DRAIN_LIMIT &&
                       gs_usb_read_rx_for_generation((uint8_t)i, generation, &rx_msg)) {
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
                    if (rx_msg.error_state_indicator) hf.flags |= GS_CAN_FLAG_ESI;
                    hf.reserved = 0;

                    uint8_t rx_len = mcan_get_message_size_from_dlc(rx_msg.dlc);
                    memcpy(hf.data, rx_msg.data_8, rx_len);
                    frame_len = 12U + ((hf.flags & GS_CAN_FLAG_FD) ? 64U : 8U);

                    // Disabled Printf to avoid hanging UART
                    // printf("[GS_USB] RX ch=%u ...

                    (void)enqueue_in_msg_for_generation(&hf, frame_len, generation);
                    s_gs_can_led_ticks[i] = GS_USB_LED_ON_TICKS;
                }
            }
        }
        
        gs_usb_main_stage = 6U; /* USB IN is chained from IRQ callbacks */

        gs_usb_main_stage = 7U; /* heartbeat */
        loop_now_us = gs_usb_get_time_us();
        if (CONFIG_GS_USB_PERIODIC_DIAGNOSTICS && (heartbeat_last_us == 0U ||
            (loop_now_us - heartbeat_last_us) >= 1000000U)) {
            heartbeat_last_us = loop_now_us;
            printf("[GS_USB] heartbeat loops=%lu stage=%u OUT=%lu armed=%u ring=%u,%u,%u,%u busy=%u,%u,%u,%u EQ=%lu RXQ=%lu IN=%u fail=%lu echo_bp=%lu rx_drop=%lu\r\n",
                   (unsigned long)gs_usb_main_loop_count,
                   (unsigned)gs_usb_main_stage,
                   (unsigned long)out_count,
                   gs_usb_out_armed ? 1U : 0U,
                   (unsigned)tx_ring_count(0),
                   (unsigned)tx_ring_count(1),
                   (unsigned)tx_ring_count(2),
                   (unsigned)tx_ring_count(3),
                   can_tx_states[0].busy ? 1U : 0U,
                   can_tx_states[1].busy ? 1U : 0U,
                   can_tx_states[2].busy ? 1U : 0U,
                   can_tx_states[3].busy ? 1U : 0U,
                   (unsigned long)gs_usb_queue_depth(echo_q_head, echo_q_tail),
                   (unsigned long)gs_usb_queue_depth(in_q_head, in_q_tail),
                   ep_in_busy ? 1U : 0U,
                   (unsigned long)ep_in_submit_fail_count,
                   (unsigned long)echo_backpressure,
                   (unsigned long)in_rx_dropped);
            gs_usb_print_rx_diagnostics(loop_now_us);
            gs_usb_print_counters_json(loop_now_us);
        }
    }
}
