// SPDX-License-Identifier: GPL-2.0+
#ifndef YP_CAN_CORE_H
#define YP_CAN_CORE_H

#include <linux/can/dev.h>
#include <linux/netdevice.h>
#include "regs.h"

#define YP_CAN_NAPI_WEIGHT 32
// The interval is not actual given ms as no processing time is accounted for
#define POLL_INTERVAL_MS 5

struct yp_can_priv {
    struct can_priv can;
    struct net_device* ndev;
    void __iomem * mem_base;
    struct timer_list timer;
    struct napi_struct napi;
    const char* label;
    int instance_id;
    struct yp_can_regs regs;
};

// Network device setup
int yp_can_setup_netdev(struct net_device* ndev);

// Hardware interaction functions
void yp_can_set_base_time(void);
void yp_can_parse_frame(struct yp_can_priv* priv, struct can_frame* cf, struct sk_buff* skb);
u32 yp_can_get_buffer_usage(struct yp_can_priv* priv);

// NAPI poll function
int yp_can_rx_poll(struct napi_struct* napi, int budget);

// Timer function for polling the hardware
void yp_can_poll(struct timer_list* t);

#endif /* YP_CAN_CORE_H */
