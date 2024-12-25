// SPDX-License-Identifier: GPL-2.0
#include <linux/can.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include "core.h"
#include "regs.h"

static ktime_t startup_time; // Store system time at boot

void yp_can_set_base_time(void) {
    struct timespec64 _current_time;
    ktime_get_real_ts64(&_current_time);
    const ktime_t current_time = timespec64_to_ktime(_current_time);
    const ktime_t since_boot = ktime_get_boottime();

    // Find system time at boot
    startup_time = ktime_sub(current_time, since_boot);
}

static void yp_can_handle_error(struct yp_can_priv* priv, struct yp_can_regs* regs) {
    struct net_device* ndev = priv->ndev;
    struct sk_buff* skb;
    struct can_frame* cf;

    // Allocate error frame
    skb = alloc_can_err_skb(ndev, &cf);
    if (!skb) {
        netdev_err(ndev, "Cannot allocate error SKB\n");
        return;
    }

    // Set default error flags
    cf->can_id = CAN_ERR_FLAG;
    cf->can_dlc = CAN_ERR_DLC;
    memset(cf->data, 0, CAN_ERR_DLC);

    // Error frame handling based on error type from spec
    if (regs->frame_type.stuff_error) {
        // Bit stuffing error: 6 bits of same level between SOF and CRC
        cf->can_id |= CAN_ERR_PROT;
        cf->data[2] = CAN_ERR_PROT_STUFF;
        netdev_warn(ndev, "Bit stuffing error detected\n");
    }

    if (regs->frame_type.form_error) {
        // Form error: Invalid bit level in SOF/EOF or delimiters
        cf->can_id |= CAN_ERR_PROT;
        cf->data[2] = CAN_ERR_PROT_FORM;
        netdev_warn(ndev, "Form error detected\n");
    }

    if (regs->frame_type.sample_error) {
        // Sample error (ACK error): No receiver made ACK slot dominant
        cf->can_id |= CAN_ERR_ACK;
        netdev_warn(ndev, "ACK error detected\n");
    }

    if (regs->frame_type.crc_error) {
        // CRC error: Calculated CRC differs from received
        cf->can_id |= CAN_ERR_PROT;
        cf->data[2] = CAN_ERR_PROT_LOC_CRC_SEQ;
        netdev_warn(ndev, "CRC error detected\n");
    }

    // Pass error frame up the stack
    netif_receive_skb(skb);
    ndev->stats.rx_errors++;
}

void yp_can_parse_frame(struct yp_can_priv* priv, struct can_frame* cf, struct yp_can_regs* regs, struct sk_buff* skb) {
    // Check for any error bits before processing
    if (regs->frame_type.stuff_error || regs->frame_type.form_error ||
        regs->frame_type.sample_error || regs->frame_type.crc_error) {
        yp_can_handle_error(priv, regs);
        return;
    }

    // Apply timestamp offset
    skb->tstamp = ktime_add(startup_time, ns_to_ktime(regs->timestamp * 1000)); // Convert µs to ns

    // Process frame information
    cf->can_dlc = regs->dlc.dlc;

    // Process CAN ID and flags
    cf->can_id = regs->can_id.id;
    if (regs->can_id.eff)
        cf->can_id |= CAN_EFF_FLAG;
    if (regs->can_id.rtr)
        cf->can_id |= CAN_RTR_FLAG;
    if (regs->can_id.err)
        cf->can_id |= CAN_ERR_FLAG;

    // Handle data based on length
    u32 data_low = (u32)(regs->data & 0xFFFFFFFF);
    u32 data_high = (u32)(regs->data >> 32);

    if (cf->can_dlc <= 4) {
        // For short frames (≤4 bytes), data is in data_low in big endian
        data_low = be32_to_cpu(data_low);
        memcpy(cf->data, &data_low, cf->can_dlc);
    }
    else {
        // For long frames (>4 bytes), need to handle both registers
        // High word goes to lower bytes, low word goes to higher bytes
        data_high = be32_to_cpu(data_high);
        data_low = be32_to_cpu(data_low);
        memcpy(cf->data, &data_high, 4); // First 4 bytes from high
        memcpy(cf->data + 4, &data_low, 4); // Last 4 bytes from low
    }
}


static void yp_can_read_regs(struct yp_can_priv* priv, struct yp_can_regs* regs) {
    void __iomem * base = priv->mem_base;
    u32 temp_lo, temp_hi;

    // Read status registers first
    regs->buffer_status.raw = readl(base + REG_STATUS_BUFFER);
    regs->error_status.raw = readl(base + REG_STATUS_ERROR);
    regs->missed_status.raw = readl(base + REG_STATUS_MISSED);

    // Then read frame registers
    regs->frame_type.raw = readl(base + REG_FRAME_TYPE);

    // Read 64-bit timestamp
    // TODO: This has to be reverted
    temp_lo = readl(base + REG_TIMESTAMP_LOW);
    temp_hi = readl(base + REG_TIMESTAMP_HIGH);
    regs->timestamp = ((u64)temp_hi << 32) | temp_lo;

    regs->can_id.raw = readl(base + REG_CAN_ID);
    regs->dlc.raw = readl(base + REG_DLC);
    regs->crc.raw = readl(base + REG_CRC);

    // Read 64-bit data
    temp_lo = readl(base + REG_DATA_LOW);
    mb(); // Memory barrier before final read that advances FIFO
    temp_hi = readl(base + REG_DATA_HIGH);
    regs->data = ((u64)temp_hi << 32) | temp_lo;
}

u32 yp_can_check_status(struct yp_can_priv* priv) {
    struct yp_can_regs regs;

    // Read status registers
    regs.buffer_status.raw = readl(priv->mem_base + REG_STATUS_BUFFER);
    regs.error_status.raw = readl(priv->mem_base + REG_STATUS_ERROR);
    regs.missed_status.raw = readl(priv->mem_base + REG_STATUS_MISSED);

    // Check for peripheral errors
    if (regs.error_status.peripheral_error) {
        netdev_err(priv->ndev, "%s: peripheral error: %x\n",
                   priv->label, regs.error_status.peripheral_error);
    }

    // Check for missed frames
    if (regs.missed_status.missed_frames) {
        netdev_warn(priv->ndev, "%s: missed frames: %d\n",
                    priv->label, regs.missed_status.missed_frames);
        if (regs.missed_status.overflow)
            netdev_warn(priv->ndev, "%s: missed frames counter overflow\n",
                        priv->label);
    }

    return regs.buffer_status.buffer_usage;
}

int yp_can_rx_poll(struct napi_struct* napi, int budget) {
    struct yp_can_priv* priv = container_of(napi, struct yp_can_priv, napi);
    struct net_device* ndev = priv->ndev;
    int received = 0;
    struct yp_can_regs regs;

    while (received < budget) {
        struct sk_buff* skb;
        struct can_frame* cf;

        // Read all registers for current frame
        yp_can_read_regs(priv, &regs);

        if (!regs.buffer_status.buffer_usage)
            break;

        skb = alloc_can_skb(ndev, &cf);
        if (!skb)
            break;

        yp_can_parse_frame(priv, cf, &regs, skb);

        // Only increment stats for valid frames
        if (!(cf->can_id & CAN_ERR_FLAG)) {
            ndev->stats.rx_packets++;
            ndev->stats.rx_bytes += cf->can_dlc;
        }

        netif_receive_skb(skb);
        received++;
    }

    if (received < budget) {
        napi_complete_done(napi, received);
        mod_timer(&priv->timer, jiffies + msecs_to_jiffies(1));
    }

    return received;
}

void yp_can_poll(struct timer_list* t) {
    struct yp_can_priv* priv = from_timer(priv, t, timer);
    struct napi_struct* napi = &priv->napi;

    if (yp_can_check_status(priv) > 0) {
        if (napi_schedule_prep(napi)) {
            // Disable timer while NAPI is processing
            del_timer(&priv->timer);
            __napi_schedule(napi);
        }
    }
    else {
        // No frames available, check again later
        mod_timer(&priv->timer, jiffies + msecs_to_jiffies(1));
    }
}
