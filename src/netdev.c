// SPDX-License-Identifier: GPL-2.0+
#include <linux/netdevice.h>
#include <linux/can/dev.h>
#include "core.h"

// CAN timing constraints for Linux CAN stack
static const struct can_bittiming_const yp_can_bittiming_const = {
        .name = "yp_can",
        .tseg1_min = YP_CAN_MIN_TSEG1,
        .tseg1_max = YP_CAN_MAX_TSEG1,
        .tseg2_min = YP_CAN_MIN_TSEG2,
        .tseg2_max = YP_CAN_MAX_TSEG2,
        .sjw_max = YP_CAN_SJW,
        .brp_min = 1,
        .brp_max = YP_CAN_MAX_BRP,
        .brp_inc = 1,
    };

static int yp_can_start(struct net_device* ndev) {
    struct yp_can_priv* priv = netdev_priv(ndev);

    // Check if bittiming is set
    if (!priv->can.bittiming.bitrate) {
        netdev_err(ndev, "Cannot start without bittiming being set. Please configure bitrate first.\n");
        return -EINVAL;
    }

    // Set base time for timestamp calculations
    yp_can_set_base_time();

    // Enable NAPI
    napi_enable(&priv->napi);

    // Start polling timer
    mod_timer(&priv->timer, jiffies + msecs_to_jiffies(POLL_INTERVAL_MS));

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
    priv->can.do_set_mode = NULL;
    priv->can.bittiming_const = &yp_can_bittiming_const;
    priv->can.do_set_bittiming = yp_can_set_bittiming;
    priv->can.clock.freq = YP_CAN_CLOCK_HZ;

    // Initialize NAPI
    netif_napi_add_weight(ndev, &priv->napi, yp_can_rx_poll, YP_CAN_NAPI_WEIGHT);

    return 0;
}
