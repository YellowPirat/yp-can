#ifndef YP_CAN_CORE_H
#define YP_CAN_CORE_H

#include <linux/can/dev.h>
#include <linux/timer.h>
#include <linux/netdevice.h>
#include "regs.h"

struct yp_can_priv {
    struct can_priv can;
    struct net_device* ndev;
    void __iomem * mem_base;
    struct timer_list timer;
    struct napi_struct napi;
    const char* label;
    int instance_id;
};

// Hardware interaction functions
void yp_can_parse_frame(struct yp_can_priv* priv, struct can_frame* cf, struct yp_can_regs* regs, struct sk_buff* skb);
u32 yp_can_check_status(struct yp_can_priv* priv);
void yp_can_set_base_time(void);

// Timer function for polling the hardware
void yp_can_poll(struct timer_list* t);

// Network device setup
int yp_can_setup_netdev(struct net_device* ndev);

// NAPI poll function
int yp_can_rx_poll(struct napi_struct* napi, int budget);

#endif /* YP_CAN_CORE_H */
