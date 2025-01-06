// SPDX-License-Identifier: GPL-2.0+
#include <linux/can.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include "core.h"
#include "regs.h"

// Access to AXI bus has to be synchronized otherwise system locks up
// Ideally, a dedicated driver should be used for this, but for simplicity we use a spinlock
static DEFINE_SPINLOCK(yp_can_bus_lock);
static ktime_t startup_time; // Store system time at boot

void yp_can_set_base_time(void) {
    struct timespec64 _current_time;
    ktime_get_real_ts64(&_current_time);
    const ktime_t current_time = timespec64_to_ktime(_current_time);
    const ktime_t since_boot = ktime_get_boottime();

    // Find system time at boot
    startup_time = ktime_sub(current_time, since_boot);
}

static void yp_can_write_timing_regs(struct yp_can_priv* priv, struct can_bittiming* bt) {
    unsigned long flags;
    // struct can_bittiming* bt = &priv->can.bittiming;
    void __iomem * base = priv->mem_base;

    spin_lock_irqsave(&yp_can_bus_lock, flags);

    writel(1, base + REG_SYNC_SEG); // Always 1
    writel(bt->prop_seg, base + REG_PROP_SEG);
    writel(bt->phase_seg1, base + REG_PHASE_SEG1);
    writel(bt->phase_seg2, base + REG_PHASE_SEG2);
    writel(bt->brp, base + REG_QUANTUM_PRESC);

    spin_unlock_irqrestore(&yp_can_bus_lock, flags);
}

static void yp_can_reset_registers(struct yp_can_priv* priv) {
    unsigned long flags;

    spin_lock_irqsave(&yp_can_bus_lock, flags);

    writel(1, priv->mem_base + REG_DRIVER_RESET);
    mb(); // Memory barrier before clearing reset
    writel(0, priv->mem_base + REG_DRIVER_RESET); // Clear reset

    spin_unlock_irqrestore(&yp_can_bus_lock, flags);
}

int yp_can_set_bittiming(struct net_device* ndev) {
    struct yp_can_priv* priv = netdev_priv(ndev);
    const struct can_bittiming* bt = &priv->can.bittiming;
    struct can_bittiming bittiming = {0};

    switch (bt->bitrate) {
    case 500000:
        bittiming.prop_seg = 5;
        bittiming.phase_seg1 = 7;
        bittiming.phase_seg2 = 7;
        bittiming.brp = 4;
        break;
    case 1000000:
        bittiming.prop_seg = 2;
        bittiming.phase_seg1 = 4;
        bittiming.phase_seg2 = 3;
        bittiming.brp = 4;
        break;
    default:
        netdev_err(priv->ndev, "Unsupported bitrate: %d\n", bt->bitrate);
        return -EINVAL;
    }

    // Write to hardware
    yp_can_write_timing_regs(priv, &bittiming);

    // Reset registers after changing bittiming
    yp_can_reset_registers(priv);

    netdev_info(
        ndev, "Set bittiming: %d bps, PS1: %d, PS2: %d, Prop: %d, BRP: %d\n", bt->bitrate, bittiming.phase_seg1,
        bittiming.phase_seg2, bittiming.prop_seg, bittiming.brp);

    return 0;
}

static void yp_can_handle_error(struct yp_can_priv* priv) {
    // Allocate error frame
    struct can_frame* cf;
    struct sk_buff* skb = alloc_can_err_skb(priv->ndev, &cf);
    if (!skb) {
        netdev_err(priv->ndev, "Cannot allocate error SKB\n");
        return;
    }
    char error_msg[64];

    // Set default error flags
    cf->can_id = CAN_ERR_FLAG;
    cf->can_dlc = CAN_ERR_DLC;
    memset(cf->data, 0, CAN_ERR_DLC);

    // Error frame handling based on error type from spec
    if (priv->regs.frame_type.stuff_error) {
        // Bit stuffing error: 6 bits of same level between SOF and CRC
        cf->can_id |= CAN_ERR_PROT;
        cf->data[2] = CAN_ERR_PROT_STUFF;
        snprintf(error_msg, 64, "Bit stuffing error detected");
    }

    if (priv->regs.frame_type.form_error) {
        // Form error: Invalid bit level in SOF/EOF or delimiters
        cf->can_id |= CAN_ERR_PROT;
        cf->data[2] = CAN_ERR_PROT_FORM;
        snprintf(error_msg, 64, "Form error detected");
    }

    if (priv->regs.frame_type.sample_error) {
        // Sample error (ACK error): No receiver made ACK slot dominant
        cf->can_id |= CAN_ERR_ACK;
        snprintf(error_msg, 64, "ACK error detected");
    }

    if (priv->regs.frame_type.crc_error) {
        // CRC error: Calculated CRC differs from received
        cf->can_id |= CAN_ERR_PROT;
        cf->data[2] = CAN_ERR_PROT_LOC_CRC_SEQ;
        snprintf(error_msg, 64, "CRC error detected");
    }

    unsigned long now = get_jiffies_64();

    if (time_after(now, priv->last_error_log_time + msecs_to_jiffies(ERROR_TIMEOUT_MS))) {
        priv->last_error_log_time = now;
        if (priv->regs.frame_type.stuff_error || priv->regs.frame_type.form_error ||
            priv->regs.frame_type.sample_error || priv->regs.frame_type.crc_error) {
            netdev_warn(priv->ndev, "%s\n", error_msg);
        }
    }

    // Pass error frame up the stack
    netif_receive_skb(skb);
    priv->ndev->stats.rx_errors++;
}

void yp_can_parse_frame(struct yp_can_priv* priv, struct can_frame* cf, struct sk_buff* skb) {
    // Check for peripheral errors
    if (priv->regs.error_status.peripheral_error) {
        netdev_err(priv->ndev, "%s: peripheral error: %x\n",
                   priv->label, priv->regs.error_status.peripheral_error);
    }

    // Check for missed frames
    unsigned long now = get_jiffies_64();
    if (priv->regs.missed_status.missed_frames) {
        if (time_after(now, priv->last_error_log_time + msecs_to_jiffies(ERROR_TIMEOUT_MS))) {
            priv->last_error_log_time = now;
            netdev_warn(priv->ndev, "%s: missed frames: %d\n",
                        priv->label, priv->regs.missed_status.missed_frames);
        }
        if (priv->regs.missed_status.overflow) {
            if (time_after(now, priv->last_error_log_time + msecs_to_jiffies(ERROR_TIMEOUT_MS))) {
                priv->last_error_log_time = now;
                netdev_warn(priv->ndev, "%s: missed frames counter overflow\n",
                            priv->label);
            }
        }
        yp_can_reset_registers(priv);
    }

    // Check for any error bits before processing
    if (priv->regs.frame_type.stuff_error || priv->regs.frame_type.form_error ||
        priv->regs.frame_type.sample_error || priv->regs.frame_type.crc_error) {
        yp_can_handle_error(priv);
        return;
    }

    // Apply timestamp offset
    skb->tstamp = ktime_add(startup_time, ns_to_ktime(priv->regs.timestamp * 1000)); // Convert µs to ns

    // Process frame information
    cf->can_dlc = priv->regs.dlc.dlc;

    // Process CAN ID and flags
    cf->can_id = priv->regs.can_id.id;
    if (priv->regs.can_id.eff)
        cf->can_id |= CAN_EFF_FLAG;
    if (priv->regs.can_id.rtr)
        cf->can_id |= CAN_RTR_FLAG;
    if (priv->regs.can_id.err)
        cf->can_id |= CAN_ERR_FLAG;

    be64_to_cpus(&priv->regs.data);
    memcpy(cf->data, &priv->regs.data, 8);
}


static void yp_can_read_regs(struct yp_can_priv* priv) {
    void __iomem * base = priv->mem_base;
    u32 temp_lo, temp_hi;
    unsigned long flags;

    spin_lock_irqsave(&yp_can_bus_lock, flags);

    // Read status registers
    priv->regs.error_status.raw = readl(priv->mem_base + REG_STATUS_ERROR);
    priv->regs.missed_status.raw = readl(priv->mem_base + REG_STATUS_MISSED);

    // Read frame registers
    priv->regs.frame_type.raw = readl(base + REG_FRAME_TYPE);

    // Read 64-bit timestamp
    temp_lo = readl(base + REG_TIMESTAMP_LOW);
    temp_hi = readl(base + REG_TIMESTAMP_HIGH);
    priv->regs.timestamp = ((u64)temp_hi << 32) | temp_lo;

    priv->regs.can_id.raw = readl(base + REG_CAN_ID);
    priv->regs.dlc.raw = readl(base + REG_DLC);
    priv->regs.crc.raw = readl(base + REG_CRC);

    // Read 64-bit data
    temp_lo = readl(base + REG_DATA_LOW);
    mb(); // Memory barrier before final read that advances FIFO
    temp_hi = readl(base + REG_DATA_HIGH);
    priv->regs.data = ((u64)temp_lo << 32) | temp_hi;

    spin_unlock_irqrestore(&yp_can_bus_lock, flags);
}

u32 yp_can_get_buffer_usage(struct yp_can_priv* priv) {
    // Read status registers
    unsigned long flags;
    spin_lock_irqsave(&yp_can_bus_lock, flags);

    priv->regs.buffer_status.raw = readl(priv->mem_base + REG_STATUS_BUFFER);

    spin_unlock_irqrestore(&yp_can_bus_lock, flags);

    return priv->regs.buffer_status.buffer_usage;
}

int yp_can_rx_poll(struct napi_struct* napi, const int budget) {
    struct yp_can_priv* priv = container_of(napi, struct yp_can_priv, napi);
    int received = 0;

    while (received < budget) {
        if (!yp_can_get_buffer_usage(priv)) break;

        // Read all registers for current frame
        yp_can_read_regs(priv);
        struct can_frame* cf;
        struct sk_buff* skb = alloc_can_skb(priv->ndev, &cf);
        if (!skb) break;

        yp_can_parse_frame(priv, cf, skb);

        // Only increment stats for valid frames
        if (!(cf->can_id & CAN_ERR_FLAG)) {
            priv->ndev->stats.rx_packets++;
            priv->ndev->stats.rx_bytes += cf->can_dlc;
        }

        netif_receive_skb(skb);
        received++;
    }

    if (received < budget) {
        napi_complete_done(napi, received);
        mod_timer(&priv->timer, jiffies + msecs_to_jiffies(POLL_INTERVAL_MS));
    }

    return received;
}

void yp_can_poll(struct timer_list* t) {
    struct yp_can_priv* priv = from_timer(priv, t, timer);
    struct napi_struct* napi = &priv->napi;

    if (yp_can_get_buffer_usage(priv) > 0 && napi_schedule_prep(napi)) { __napi_schedule(napi); }
    else {
        // No frames available, check again later
        mod_timer(&priv->timer, jiffies + msecs_to_jiffies(POLL_INTERVAL_MS));
    }
}
