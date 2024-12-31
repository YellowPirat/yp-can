// SPDX-License-Identifier: GPL-2.0+
/*
 * Linux CAN driver for YellowPirat project
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include "core.h"

#define DRV_NAME "yp-can"

static const struct of_device_id yp_can_of_match[] = {
        {.compatible = "yellowpirat,can-fifo"},
        {}
    };
MODULE_DEVICE_TABLE(of, yp_can_of_match);

static int yp_can_probe(struct platform_device* pdev) {
    // Get label from device tree
    const char* label = of_get_property(pdev->dev.of_node, "label", NULL);
    if (!label) {
        dev_err(&pdev->dev, "no label provided in device tree\n");
        return -EINVAL;
    }

    dev_info(&pdev->dev, "%s: probing YellowPirat CAN device\n", label);

    // Get memory resource
    struct resource* mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!mem) {
        dev_err(&pdev->dev, "no memory resource provided\n");
        return -ENODEV;
    }

    void* addr = devm_ioremap_resource(&pdev->dev, mem);
    if (IS_ERR(addr)) {
        dev_err(&pdev->dev, "%s: cannot ioremap memory region\n", label);
        return PTR_ERR(addr);
    }

    // Allocate network device
    struct net_device* ndev = alloc_candev(sizeof(struct yp_can_priv), 0);
    if (!ndev) {
        dev_err(&pdev->dev, "%s: cannot allocate CAN device\n", label);
        return -ENOMEM;
    }

    struct yp_can_priv* priv = netdev_priv(ndev);
    priv->mem_base = addr;
    priv->ndev = ndev;
    priv->label = label;

    // Setup timer for FIFO polling
    timer_setup(&priv->timer, yp_can_poll, 0);

    // Extract instance ID from label
    int err;
    if (sscanf(label, "can%d", &priv->instance_id) != 1) {
        dev_err(&pdev->dev, "%s: invalid label format\n", label);
        err = -EINVAL;
        goto out_free;
    }

    // Setup CAN network device and NAPI
    err = yp_can_setup_netdev(ndev);
    if (err) {
        dev_err(&pdev->dev, "%s: failed to setup netdev\n", label);
        goto out_free;
    }

    SET_NETDEV_DEV(ndev, &pdev->dev);
    platform_set_drvdata(pdev, ndev);

    err = register_candev(ndev);
    if (err) {
        dev_err(&pdev->dev, "%s: failed to register CAN device\n", label);
        goto out_free_napi;
    }

    dev_info(&pdev->dev, "%s: successfully initialized and registered\n", label);
    return 0;

out_free_napi:
    netif_napi_del(&priv->napi);
out_free:
    del_timer_sync(&priv->timer);
    free_candev(ndev);
    return err;
}

static int yp_can_remove(struct platform_device* pdev) {
    struct net_device* ndev = platform_get_drvdata(pdev);
    struct yp_can_priv* priv = netdev_priv(ndev);

    del_timer_sync(&priv->timer);
    unregister_candev(ndev);
    netif_napi_del(&priv->napi);
    free_candev(ndev);

    return 0;
}


static struct platform_driver yp_can_driver = {
        .driver = {
            .name = DRV_NAME,
            .of_match_table = yp_can_of_match,
        },
        .probe = yp_can_probe,
        .remove = yp_can_remove,
    };

module_platform_driver(yp_can_driver);

MODULE_AUTHOR("YellowPirat Team");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CAN driver for YellowPirat project");
