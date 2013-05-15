/*
 *    Wdt_p4a    0.01:    A Watchdog Device for Innofidei P4A chip
 *
 *    (c) Copyright 2013 innofidei.com (lichangjun <lichangjun@innofidei.com>)
 *
 *            -----------------------
 *
 *    This program is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU General Public License
 *    as published by the Free Software Foundation; either version
 *    2 of the License, or (at your option) any later version.
 *
 *            -----------------------
 *      14-Apr-2013 lichangjun <lichangjun@innofidei.com>
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#include <mach/p4a_wdt.h>
#include "watchdog_dev.h"

#define WATCHDOG_VERSION  "0.01"
#define WATCHDOG_NAME     "Wdt_p4a"
#define PFX WATCHDOG_NAME ": "
#define DRIVER_VERSION    WATCHDOG_NAME " driver, v" WATCHDOG_VERSION "\n"

#define DEFAULT_TIMEOUT 60  /* default timeout in seconds */
#define MIN_TIMEOUT 2 /* minimum timeout in seconds */


struct wdt_p4a {
	struct device *dev;
	void __iomem *base;
	struct watchdog_device wdt_dev;

	struct clk *clk;
};


static  DEFINE_SPINLOCK(spinlock);

/*
 * Start the watchdog
 */
static int wdt_p4a_start(struct watchdog_device *wdd)
{
	unsigned long flags;
	unsigned long reg_value;
	unsigned long clk_rate;
	struct wdt_p4a *wdt_p4ap;

	wdt_p4ap = container_of(wdd, struct wdt_p4a, wdt_dev);

	clk_enable(wdt_p4ap->clk);

	spin_lock_irqsave(&spinlock, flags);

	reg_value = __raw_readl(WDTCR + wdt_p4ap->base);
	reg_value &= ~WDTCR_ENABLE;    /* disable watchdog  */
	reg_value |= WDTCR_COUNT_CLEAR;/* clear watchdog counter */
	reg_value |= WDTCR_RESET_CHIP_EN;/* enable watchdog reset chip */
	__raw_writel(reg_value, WDTCR + wdt_p4ap->base);

	clk_rate =  clk_get_rate(wdt_p4ap->clk);

	if (wdd->timeout > wdd->max_timeout) {
		dev_info(wdt_p4ap->dev, "timeout %d greater than max_timeout%d", wdd->timeout, wdd->max_timeout);
		wdd->timeout = wdd->max_timeout;
	}

	/* set watch dog timeout */
	__raw_writel(wdd->timeout * clk_rate, WDTCNTR);

	/* enable watchdog  */
	reg_value = __raw_readl(WDTCR + wdt_p4ap->base);
	reg_value |= WDTCR_ENABLE;
	__raw_writel(reg_value, WDTCR + wdt_p4ap->base);
	spin_unlock_irqrestore(&spinlock, flags);

	dev_dbg(wdt_p4ap->dev, "activated.\n");

	return 0;
}

/*
 * Stop the watchdog
 */
static int wdt_p4a_stop(struct watchdog_device *wdd)
{
	unsigned long flags;
	unsigned long reg_value;
	struct wdt_p4a *wdt_p4ap;

	wdt_p4ap = container_of(wdd, struct wdt_p4a, wdt_dev);

	spin_lock_irqsave(&spinlock, flags);

	reg_value = __raw_readl(WDTCR + wdt_p4ap->base);
	reg_value &= ~WDTCR_ENABLE;    /* disable watchdog  */
	reg_value |= WDTCR_COUNT_CLEAR;/* clear watchdog counter */
	reg_value &= ~WDTCR_RESET_CHIP_EN;/* disable watchdog reset chip */
	__raw_writel(reg_value, WDTCR + wdt_p4ap->base);

	spin_unlock_irqrestore(&spinlock, flags);

	clk_disable(wdt_p4ap->clk);

	return 0;
}

/*
 * Send a keepalive ping to the watchdog
 */
static int wdt_p4a_keepalive(struct watchdog_device *wdd)
{
	unsigned long flags;
	unsigned long reg_value;
	struct wdt_p4a *wdt_p4ap;

	wdt_p4ap = container_of(wdd, struct wdt_p4a, wdt_dev);

	spin_lock_irqsave(&spinlock, flags);

	reg_value = __raw_readl(WDTCR + wdt_p4ap->base);
	reg_value |= WDTCR_COUNT_CLEAR;/* clear watchdog counter */
	__raw_writel(reg_value, WDTCR + wdt_p4ap->base);

	spin_unlock_irqrestore(&spinlock, flags);

	return 0;
}

/*
 * Set the watchdog timeout value
 */
static int wdt_p4a_set_timeout(struct watchdog_device *wdd, unsigned int t)
{
	unsigned long flags;
	unsigned long reg_value;
	unsigned long clk_rate;
	struct wdt_p4a *wdt_p4ap;

	wdt_p4ap = container_of(wdd, struct wdt_p4a, wdt_dev);

	/* timeout is the timeout in seconds*/
	if (t > wdd->max_timeout) {
		dev_info(wdt_p4ap->dev, "timeout%d greater than max timeout%d", t, wdd->max_timeout);
		wdd->timeout = wdd->max_timeout;
	} else {
		wdd->timeout = t;
	}

	clk_rate =  clk_get_rate(wdt_p4ap->clk);

	spin_lock_irqsave(&spinlock, flags);

	reg_value = __raw_readl(WDTCR + wdt_p4ap->base);
	reg_value &= ~WDTCR_ENABLE;    /* disable watchdog  */
	reg_value |= WDTCR_COUNT_CLEAR;/* clear watchdog counter */
	reg_value |= WDTCR_RESET_CHIP_EN;/* enable watchdog reset chip */
	__raw_writel(reg_value, WDTCR + wdt_p4ap->base);

	/* set watch dog timeout */
	__raw_writel(wdd->timeout * clk_rate, WDTCNTR + wdt_p4ap->base);

	/* enable watchdog  */
	reg_value = __raw_readl(WDTCR + wdt_p4ap->base);
	reg_value |= WDTCR_ENABLE;
	__raw_writel(reg_value, WDTCR + wdt_p4ap->base);

	spin_unlock_irqrestore(&spinlock, flags);

	return 0;
}

static struct watchdog_info wdt_p4a_info = {
	.options =  WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.firmware_version = 1,
	.identity = WATCHDOG_NAME,
};

static struct watchdog_ops wdt_p4a_ops = {
	.owner = THIS_MODULE,
	.start = wdt_p4a_start,
	.stop  = wdt_p4a_stop,
	.ping  = wdt_p4a_keepalive,
	.status = NULL,
	.set_timeout = wdt_p4a_set_timeout,
	.ioctl = NULL,
};


/* ......................................................................... */

static int __devinit wdt_p4a_probe(struct platform_device *pdev)
{
	struct wdt_p4a_platform_data *pdata = pdev->dev.platform_data;
	struct wdt_p4a *p4a;
	struct clk *clk;
	unsigned long clk_rate = 0;
	int rc;
	struct device *dev = &pdev->dev;

	printk(KERN_INFO PFX DRIVER_VERSION);

	if (unlikely(NULL == pdev)) {
		dev_err(dev, "error pdev NULL\n");
		return -EINVAL;
	}

	if (unlikely(NULL == pdata)) {
		dev_err(dev, "error platform_data NULL\n");
		return -EINVAL;
	}

	if (unlikely(NULL == pdata->membase)) {
		return -EINVAL;
	}

	clk = clk_get(dev, pdata->clk_name ? pdata->clk_name : "WDT_CLK");
	if (IS_ERR(clk)) {
		rc = PTR_ERR(clk);
		dev_err(dev, "Could not get watchdog clock!\n");
		goto fail_clk_get;
	}


	p4a = kzalloc(sizeof(struct wdt_p4a), GFP_KERNEL);
	if (unlikely(p4a == NULL)) {
		dev_err(&pdev->dev, "Unable to malloc p4a_wdt\n");
		rc = -ENOMEM;
		goto err_kzalloc;
	}

	p4a->dev = &pdev->dev;
	p4a->base = pdata->membase;
	p4a->wdt_dev.info = &wdt_p4a_info;
	p4a->wdt_dev.ops = &wdt_p4a_ops;
	p4a->wdt_dev.bootstatus =  WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	p4a->wdt_dev.timeout = DEFAULT_TIMEOUT,
	p4a->wdt_dev.min_timeout = MIN_TIMEOUT,

	clk_rate =  clk_get_rate(clk);
	p4a->wdt_dev.max_timeout = UINT_MAX / clk_rate;

	rc = watchdog_dev_register(&p4a->wdt_dev);
	if (rc) {
		dev_err(dev, "cannot register wdtdev (err=%d)\n", rc);
		goto err_out;
	}

	platform_set_drvdata(pdev, p4a);

	return 0;

err_out:

	kfree(p4a);
err_kzalloc:
fail_clk_get:
	return rc;
}

static int __devexit wdt_p4a_remove(struct platform_device *pdev)
{
	int rc;
	struct wdt_p4a *p4a;
	p4a = (struct wdt_p4a *)platform_get_drvdata(pdev);

	wdt_p4a_stop(&p4a->wdt_dev);

	rc = watchdog_dev_unregister(&p4a->wdt_dev);

	kfree(p4a);

	return rc;
}

static void wdt_p4a_shutdown(struct platform_device *pdev)
{
	struct wdt_p4a *p4a;
	p4a = (struct wdt_p4a *)platform_get_drvdata(pdev);

	wdt_p4a_stop(&p4a->wdt_dev);
}

#ifdef CONFIG_PM

static int wdt_p4a_suspend(struct platform_device *pdev, pm_message_t message)
{
	int rc;
	struct wdt_p4a *p4a;
	p4a = (struct wdt_p4a *)platform_get_drvdata(pdev);

	wdt_p4a_stop(&p4a->wdt_dev);

	return 0;
}

static int wdt_p4a_resume(struct platform_device *pdev)
{
	int rc;
	struct wdt_p4a *p4a;
	p4a = (struct wdt_p4a *)platform_get_drvdata(pdev);

	if (test_bit(WDOG_ACTIVE, &p4a->wdt_dev.status)) {
		wdt_p4a_start(&p4a->wdt_dev);
	}

	return 0;
}

#else
#define wdt_p4a_suspend NULL
#define wdt_p4a_resume	NULL
#endif

static struct platform_driver wdt_p4a_driver = {
	.probe		= wdt_p4a_probe,
	.remove		= __devexit_p(wdt_p4a_remove),
	.shutdown	= wdt_p4a_shutdown,
	.suspend	= wdt_p4a_suspend,
	.resume		= wdt_p4a_resume,
	.driver		= {
		.name	= "p4a-wdt",
		.owner	= THIS_MODULE,
	},
};

static int __init wdt_p4a_init(void)
{
	return platform_driver_register(&wdt_p4a_driver);
}

static void __exit wdt_p4a_exit(void)
{
	platform_driver_unregister(&wdt_p4a_driver);
}


module_init(wdt_p4a_init);
module_exit(wdt_p4a_exit);

MODULE_AUTHOR("lichangjun <lichangjun@innofidei.com>");
MODULE_DESCRIPTION("Innofidei P4A Watchdog Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
