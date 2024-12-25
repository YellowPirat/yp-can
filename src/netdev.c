// SPDX-License-Identifier: GPL-2.0
#include <linux/netdevice.h>
#include <linux/can/dev.h>
#include "core.h"
#include "regs.h"

#define YP_CAN_NAPI_WEIGHT 256  // Hardware FIFO size

static int yp_can_start(struct net_device* ndev) {
    struct yp_can_priv* priv = netdev_priv(ndev);

    // Set base time for timestamp calculations
    yp_can_set_base_time();

    // Enable NAPI
    napi_enable(&priv->napi);

    // Start polling timer
    mod_timer(&priv->timer, jiffies + msecs_to_jiffies(1));

    priv->can.state = CAN_STATE_ERROR_ACTIVE;
    netif_start_queue(ndev);
    return 0;
}

static int yp_can_stop(struct net_device* ndev) {
    struct yp_can_priv* priv = netdev_priv(ndev);

    // Stop polling timer
    del_timer_sync(&priv->timer);

    // Disable NAPI
    napi_disable(&priv->napi);

    priv->can.state = CAN_STATE_STOPPED;
    netif_stop_queue(ndev);
    return 0;
}

static const struct net_device_ops yp_can_netdev_ops = {
        .ndo_open = yp_can_start,
        .ndo_stop = yp_can_stop,
        // No transmit operation since this is read-only
    };

int yp_can_setup_netdev(struct net_device* ndev) {
    struct yp_can_priv* priv = netdev_priv(ndev);

    ndev->netdev_ops = &yp_can_netdev_ops;

    // Set CAN device properties
    priv->can.ctrlmode_supported = 0x02; // CAN_CTRLMODE_RX_ONLY
    priv->can.bittiming_const = NULL;
    priv->can.do_set_bittiming = NULL;
    priv->can.do_set_mode = NULL;

    // Initialize NAPI
    netif_napi_add_weight(ndev, &priv->napi, yp_can_rx_poll, YP_CAN_NAPI_WEIGHT);

    return 0;
}
