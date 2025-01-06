// SPDX-License-Identifier: GPL-2.0+
#ifndef YP_CAN_CORE_H
#define YP_CAN_CORE_H

#include <linux/can/dev.h>
#include <linux/netdevice.h>
#include "regs.h"

#define YP_CAN_NAPI_WEIGHT 32
// The interval is not actual given ms as no processing time is accounted for
#define POLL_INTERVAL_MS 5
// Timeout for error state in ms
#define ERROR_TIMEOUT_MS 15000 // 15 seconds
// Bit timing constraints from hardware
#define YP_CAN_CLOCK_HZ     50000000    // 50MHz base clock
#define YP_CAN_MIN_TSEG1    1           // Minimum TSEG1
#define YP_CAN_MAX_TSEG1    32          // prop_seg + phase_seg1
#define YP_CAN_MIN_TSEG2    1           // Minimum TSEG2
#define YP_CAN_MAX_TSEG2    8           // phase_seg2
#define YP_CAN_SJW          4           // Synchronization Jump Width
#define YP_CAN_MAX_BRP      64          // Maximum Baud Rate Prescaler

struct yp_can_priv {
    struct can_priv can;
    struct net_device* ndev;
    void __iomem * mem_base;
    struct timer_list timer;
    struct napi_struct napi;
    const char* label;
    int instance_id;
    struct yp_can_regs regs;
    unsigned long last_error_log_time;
};


// Network device setup
int yp_can_setup_netdev(struct net_device* ndev);

// Hardware interaction functions
void yp_can_set_base_time(void);
int yp_can_set_bittiming(struct net_device* ndev);
void yp_can_parse_frame(struct yp_can_priv* priv, struct can_frame* cf, struct sk_buff* skb);
u32 yp_can_get_buffer_usage(struct yp_can_priv* priv);

// NAPI poll function
int yp_can_rx_poll(struct napi_struct* napi, int budget);

// Timer function for polling the hardware
void yp_can_poll(struct timer_list* t);

#endif /* YP_CAN_CORE_H */
