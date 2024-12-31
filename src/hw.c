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

static void yp_can_handle_error(const struct yp_can_priv* priv) {
    // Allocate error frame
    struct can_frame* cf;
    struct sk_buff* skb = alloc_can_err_skb(priv->ndev, &cf);
    if (!skb) {
        netdev_err(priv->ndev, "Cannot allocate error SKB\n");
        return;
    }

    // Set default error flags
    cf->can_id = CAN_ERR_FLAG;
    cf->can_dlc = CAN_ERR_DLC;
    memset(cf->data, 0, CAN_ERR_DLC);

    // Error frame handling based on error type from spec
    if (priv->regs.frame_type.stuff_error) {
        // Bit stuffing error: 6 bits of same level between SOF and CRC
        cf->can_id |= CAN_ERR_PROT;
        cf->data[2] = CAN_ERR_PROT_STUFF;
        netdev_warn(priv->ndev, "Bit stuffing error detected\n");
    }

    if (priv->regs.frame_type.form_error) {
        // Form error: Invalid bit level in SOF/EOF or delimiters
        cf->can_id |= CAN_ERR_PROT;
        cf->data[2] = CAN_ERR_PROT_FORM;
        netdev_warn(priv->ndev, "Form error detected\n");
    }

    if (priv->regs.frame_type.sample_error) {
        // Sample error (ACK error): No receiver made ACK slot dominant
        cf->can_id |= CAN_ERR_ACK;
        netdev_warn(priv->ndev, "ACK error detected\n");
    }

    if (priv->regs.frame_type.crc_error) {
        // CRC error: Calculated CRC differs from received
        cf->can_id |= CAN_ERR_PROT;
        cf->data[2] = CAN_ERR_PROT_LOC_CRC_SEQ;
        netdev_warn(priv->ndev, "CRC error detected\n");
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
    if (priv->regs.missed_status.missed_frames) {
        netdev_warn(priv->ndev, "%s: missed frames: %d\n",
                    priv->label, priv->regs.missed_status.missed_frames);
        if (priv->regs.missed_status.overflow)
            netdev_warn(priv->ndev, "%s: missed frames counter overflow\n",
                        priv->label);
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
