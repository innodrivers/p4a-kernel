/*
 * Copyright (C) 2012 Innofidei Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include "sdhci.h"

#define DRV_NAME	"p4a-sdhci"

struct sdhci_p4a_host {
	struct sdhci_host *host;
	struct clk *clk;
	struct resource *res;
};

static unsigned int sdhci_p4a_get_max_clk(struct sdhci_host *host)
{
	return 102000000;		//TODO
}

static struct sdhci_ops p4a_sdhci_ops = {
	.get_max_clock	= sdhci_p4a_get_max_clk,
};


static int __devinit sdhci_p4a_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sdhci_host *host = NULL;
	struct resource *iomem;
	struct sdhci_p4a_host *p4a = NULL;
	int irq;
	int ret = 0;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "no irq specified\n");
		ret = irq;
		goto err;
	}

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iomem) {
		dev_err(dev, "no memory specified!\n");
		ret = -ENOMEM;
		goto err;
	}

	host = sdhci_alloc_host(&pdev->dev, sizeof(struct sdhci_p4a_host));
	if (IS_ERR(host)) {
		dev_err(dev, "failed to alloc host!\n");
		ret = PTR_ERR(host);
		goto err;
	}

	p4a = sdhci_priv(host);
	p4a->host = host;

	p4a->clk = clk_get(dev, "SDCLK");
	if (IS_ERR(p4a->clk)) {
		dev_err(dev, "failed to get clock!\n");
		ret = PTR_ERR(p4a->clk);
		goto err_clk;
	}
	clk_enable(p4a->clk);

	p4a->res = request_mem_region(iomem->start, resource_size(iomem),
				      mmc_hostname(host->mmc));
	if (!p4a->res) {
		dev_err(&pdev->dev, "cannot request region\n");
		ret = -EBUSY;
		goto err_request;
	}

	host->ioaddr = ioremap(iomem->start, resource_size(iomem));
	if (!host->ioaddr) {
		dev_err(&pdev->dev, "failed to remap registers!\n");
		ret = -ENOMEM;
		goto err_ioremap;
	}

	host->hw_name = dev_name(dev);
	host->ops = &p4a_sdhci_ops,
	host->irq = irq;
	host->quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
			SDHCI_QUIRK_BROKEN_ADMA |
			SDHCI_QUIRK_32BIT_DMA_ADDR | SDHCI_QUIRK_32BIT_DMA_SIZE |
			SDHCI_QUIRK_BROKEN_TIMEOUT_VAL;
	
	if (1) {
		host->quirks |= SDHCI_QUIRK_BROKEN_CARD_DETECTION;
		host->mmc->caps |= MMC_CAP_NONREMOVABLE;
	}

	ret = sdhci_add_host(host);
	if (ret) {
		dev_err(&pdev->dev, "failed to add host!\n");
		goto err_add_host;
	}

	platform_set_drvdata(pdev, host);

	return 0;

err_add_host:
	iounmap(host->ioaddr);
err_ioremap:
	release_mem_region(iomem->start, resource_size(iomem));
err_request:
	clk_put(p4a->clk);
err_clk:
	sdhci_free_host(host);
err:
	return ret;
}

static int __devexit sdhci_p4a_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_p4a_host *p4a = sdhci_priv(host);
	int dead = 0;
	u32 scratch;

	if (host) {
		scratch = readl(host->ioaddr + SDHCI_INT_STATUS);
		if (scratch == (u32)-1)
			dead = 1;

		sdhci_remove_host(host, dead);
		iounmap(host->ioaddr);
		release_mem_region(p4a->res->start, resource_size(p4a->res));

		clk_put(p4a->clk);

		sdhci_free_host(host);
		platform_set_drvdata(pdev, NULL);
	}

	return 0;
}

#ifdef CONFIG_PM
static int sdhci_p4a_suspend(struct platform_device *dev, pm_message_t state)
{
	struct sdhci_host *host = platform_get_drvdata(dev);

	return sdhci_suspend_host(host, state);
}

static int sdhci_p4a_resume(struct platform_device *dev)
{
	struct sdhci_host *host = platform_get_drvdata(dev);

	return sdhci_resume_host(host);
}
#else
#define sdhci_p4a_suspend	NULL
#define sdhci_p4a_resume	NULL
#endif

static struct platform_driver sdhci_p4a_driver = {
	.probe		= sdhci_p4a_probe,
	.remove		= __devexit_p(sdhci_p4a_remove),
	.suspend	= sdhci_p4a_suspend,
	.resume		= sdhci_p4a_resume,
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init sdhci_p4a_init(void)
{
	return platform_driver_register(&sdhci_p4a_driver);
}

static void __exit sdhci_p4a_exit(void)
{
	platform_driver_unregister(&sdhci_p4a_driver);
}

module_init(sdhci_p4a_init);
module_exit(sdhci_p4a_exit);

MODULE_DESCRIPTION("SDHCI driver for innofidei P4A");
MODULE_AUTHOR("jimmy.li <lizhengming@innofidei.com>");
MODULE_LICENSE("GPL v2");
