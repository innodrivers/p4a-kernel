/*
 * linux/arch/arm/mach-p4a/devices.c
 *
 * Copyright (C) 2013 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/i2c.h>

#include <mach/hardware.h>
#include <mach/irqs.h>

#include <mach/p4a-regs.h>
#include <mach/p4a_serial.h>
#include <mach/p4a_i2c.h>
#include <mach/p4a_ether.h>
#include <mach/p4a_nand.h>
#include <mach/p4a_wdt.h>

#include <mach/micproto.h>
#include <mach/micp_cmd.h>
#include <mach/p4a_mbnand.h>

#define RESOURCE_BUILD_HELPER(irq, base, size)	\
	{											\
		.start	= (irq),						\
		.end	= (irq),						\
		.flags	= IORESOURCE_IRQ,				\
	},											\
	{											\
		.start	= (base),						\
		.end	= (base) + (size) - 1,			\
		.flags	= IORESOURCE_MEM,				\
	},

#define DEVICE_BUILD_HELPER(_name, _id, _res, _data)	\
	{													\
		.id	= (_id),									\
		.name	= (_name),								\
		.num_resources	= ARRAY_SIZE(_res),				\
		.resource	= (_res),							\
		.dev	= {										\
			.platform_data = (_data),					\
		},												\
	}



static int __init p4a_register_device(struct platform_device *pdev, void *data)
{
	int ret;

	pdev->dev.platform_data = data;

	ret = platform_device_register(pdev);
	if (ret) {
		dev_err(&pdev->dev, "unable to register device (%d)\n",ret);
	}

	return ret;
}


/* --------------------------------------------------
 *  UART
 * ------------------------------------------------ */
#if defined(CONFIG_SERIAL_P4A) || defined(CONFIG_SERIAL_P4A_MODULE)

static struct uart_p4a_platform_data uart1_info = {
	.clk_name	= "UART_CLK",
	.membase	= P4A_UART1_BASE,
};

static struct uart_p4a_platform_data uart2_info = {
	.clk_name	= "UART_CLK",
	.membase	= P4A_UART2_BASE,
};

static struct uart_p4a_platform_data uart4w_info = {
	.clk_name	= "UART_CLK",
	.membase	= P4A_UART4W_BASE,
};

static struct resource p4a_uart1_resources[] = {
	{
		.start	= IRQ_PERI_UART1_RX,
		.end	= IRQ_PERI_UART1_TX,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource p4a_uart2_resources[] = {
	{
		.start	= IRQ_PERI_UART2_RX,
		.end	= IRQ_PERI_UART2_TX,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource p4a_uart4w_resources[] = {
	RESOURCE_BUILD_HELPER(IRQ_UART4W, P4A_UART4W_PHYS, P4A_UART4W_SIZE)
};

static struct platform_device p4a_uart_devices[] = {
	DEVICE_BUILD_HELPER("p4a-uart", 0, p4a_uart1_resources, &uart1_info),
	DEVICE_BUILD_HELPER("p4a-uart", 1, p4a_uart2_resources, &uart2_info),
	DEVICE_BUILD_HELPER("p4a-uart4w", -1, p4a_uart4w_resources, &uart4w_info),
};

int __init p4a_serial_init(int id)
{
	if (id < 0 || id >= ARRAY_SIZE(p4a_uart_devices)) {
		return -EINVAL;
	}

	return platform_device_register(&p4a_uart_devices[id]);
}
#else
int __init p4a_serial_init(int id) {}
#endif

/* --------------------------------------------------
 *  Watchdog Timer
 * ------------------------------------------------ */
#if defined(CONFIG_P4A_WATCHDOG) || defined(CONFIG_P4A_WATCHDOG_MODULE)
static struct wdt_p4a_platform_data p4a_wdt_plat_data = {
	.clk_name	= "WDT_CLK",
	.membase	= P4A_WDT_BASE,
};

static struct platform_device p4a_device_wdt = {
	.name	= "p4a-wdt",
	.id		= -1,
};

static int __init p4a_add_device_wdt(void)
{
	return p4a_register_device(&p4a_device_wdt, &p4a_wdt_plat_data);
}
#else
static int __init p4a_add_device_wdt(void) {return 0;}
#endif	//CONFIG_P4A_WATCHDOG

/* --------------------------------------------------
 *  I2C Master Adapter
 * ------------------------------------------------ */
#if defined(CONFIG_I2C_P4A) || defined(CONFIG_I2C_P4A_MODULE)
static struct resource i2c_resources[][2] = {
	{ RESOURCE_BUILD_HELPER(IRQ_PERI_I2C_MASTER1, P4A_I2C1_PHYS, 24) },
	{ RESOURCE_BUILD_HELPER(IRQ_PERI_I2C_MASTER2, P4A_I2C2_PHYS, 24) },
};

#define P4A_I2C_PORT_NUM	(ARRAY_SIZE(i2c_resources))

static struct p4a_i2c_platform_data i2c_pdata[P4A_I2C_PORT_NUM];

static struct platform_device p4a_i2c_devices[P4A_I2C_PORT_NUM] = {
	DEVICE_BUILD_HELPER("p4a-i2c", 0, i2c_resources[0], &i2c_pdata[0]),
	DEVICE_BUILD_HELPER("p4a-i2c", 1, i2c_resources[1], &i2c_pdata[1]),
};

/**
 * @brief : register I2C bus with device descriptors
 *
 * @param[in] bus_id : I2C bus id counting from number 0.
 * @param[in] clkrate : clock rate of the bus in KHz.
 * @param[in] info : pointer into I2C device descriptors table or NULL.
 * @param[in] len : the number of descriptors in table
 *
 * @return : if success return 0, otherwise return a negative error code.
 */
int __init p4a_register_i2c_bus(int bus_id, u32 clkrate,
			  struct i2c_board_info const *info,
			  unsigned len)
{
	int err;

	BUG_ON(bus_id < 0 || bus_id > P4A_I2C_PORT_NUM);

	if (info && len) {
		err = i2c_register_board_info(bus_id, info, len);
		if (err)
			return err;
	}

	snprintf(i2c_pdata[bus_id].clk_name, sizeof(i2c_pdata[bus_id].clk_name), "%s", "I2C_CLK");

	i2c_pdata[bus_id].clkrate = clkrate;

	return platform_device_register(&p4a_i2c_devices[bus_id]);
}
#else
int __init p4a_register_i2c_bus(int bus_id, u32 clkrate,
			  struct i2c_board_info const *info,
			  unsigned len)
{
	return 0;
}
#endif

/* --------------------------------------------------
 *  Ethernet MAC Controller
 * ------------------------------------------------ */
#if defined(CONFIG_P4A_ETHER) || defined(CONFIG_P4A_ETHER_MODULE)

static u64 ether_dmamask = DMA_BIT_MASK(32);

static struct resource p4a_ether_resources[] = {
	RESOURCE_BUILD_HELPER(IRQ_ETHER, P4A_ETHER_PHYS, P4A_ETHER_SIZE)
};

static struct platform_device p4a_device_ether = 
	DEVICE_BUILD_HELPER("p4a-ether", -1, p4a_ether_resources, NULL);

int __init p4a_ether_init(struct p4a_ether_platdata* data)
{
	p4a_device_ether.dev.dma_mask = &ether_dmamask;
	p4a_device_ether.dev.coherent_dma_mask = DMA_BIT_MASK(32);

	return p4a_register_device(&p4a_device_ether, data);
}
#else
int __init p4a_ether_init(struct p4a_ether_platdata* data) {return 0;}
#endif	//CONFIG_P4A_ETHER

/* --------------------------------------------------
 *  SDHCI
 * ------------------------------------------------ */
#if defined(CONFIG_MMC_SDHCI_P4A) || defined(CONFIG_MMC_SDHCI_P4A_MODULE)
static struct resource p4a_mmc_resources[][2] = {
	{ RESOURCE_BUILD_HELPER(IRQ_SDIO_MASTER1, P4A_SDIOM1_PHYS, P4A_SDIOM1_SIZE)},
	{ RESOURCE_BUILD_HELPER(IRQ_SDIO_MASTER2, P4A_SDIOM2_PHYS, P4A_SDIOM2_SIZE)},
};

static struct platform_device p4a_mmc_devices[] = {
	DEVICE_BUILD_HELPER("p4a-sdhci", 0, p4a_mmc_resources[0], NULL),
	DEVICE_BUILD_HELPER("p4a-sdhci", 1, p4a_mmc_resources[1], NULL),
};

int __init p4a_mmc_init(int id)
{
	if (id < 0 || id >= ARRAY_SIZE(p4a_mmc_devices))
		return -EINVAL;

	return p4a_register_device(&p4a_mmc_devices[id], NULL);
}
#else
int __init p4a_mmc_init(int id) {return 0;}
#endif	//CONFIG_MMC_SDHCI_P4A

/* --------------------------------------------------
 *  NAND Controller
 * ------------------------------------------------ */
#if defined(CONFIG_MTD_NAND_P4A) || defined(CONFIG_MTD_NAND_P4A_MODULE)
static struct resource p4a_nand_resources[] = {
	RESOURCE_BUILD_HELPER(IRQ_NAND, P4A_NANDC_PHYS, P4A_NANDC_SIZE)
};

static u64 nand_dma_mask = ~(u64)0;

static struct platform_device p4a_nand_device = 
	DEVICE_BUILD_HELPER("p4a-nand", -1, p4a_nand_resources, NULL);

int __init p4a_nand_init(struct p4a_nand_platdata* pdata)
{
	p4a_nand_device.dev.dma_mask = &nand_dma_mask;
	p4a_nand_device.dev.coherent_dma_mask = DMA_BIT_MASK(32);

	return p4a_register_device(&p4a_nand_device, pdata);
}
#else
int __init p4a_nand_init(struct p4a_nand_platdata* pdata) {return 0;}
#endif	//CONFIG_MTD_NAND_P4A

/* --------------------------------------------------
 *  Mailbox
 * ------------------------------------------------ */
#ifdef CONFIG_P4A_MAILBOX
static struct resource p4a_mailbox_resources[] = {
	RESOURCE_BUILD_HELPER(IRQ_MAILBOX_DSP2ARM, P4A_MAILBOX_PHYS, P4A_MAILBOX_SIZE)
};

static struct platform_device p4a_mailbox_device =
	DEVICE_BUILD_HELPER("p4a-mailbox", -1, p4a_mailbox_resources, NULL);


int __init p4a_add_device_mailbox(void)
{
	return p4a_register_device(&p4a_mailbox_device, NULL);
}

#else
int __init p4a_add_device_mailbox(void) {return 0;}
#endif	//CONFIG_P4A_MAILBOX

/* --------------------------------------------------
 *  Virtual MTD Nand (Mailbox based)
 * ------------------------------------------------ */
#if defined(CONFIG_MTD_MAILBOX_NAND_P4A) || defined(CONFIG_MTD_MAILBOX_NAND_P4A_MODULE)

static struct platform_device p4a_device_mbnand = {
	.name	= "p4a-mbnand",
	.id		= -1,
};

static void __mb_nand_getinfo_complete(struct mreqb *rq)
{
	struct p4a_mbnand_platdata *bdata = rq->context;

	if (rq->result == 0) {
		struct mbnand_getinfo_arg *result = rq->extra_data;

		snprintf(bdata->devname, sizeof(bdata->devname), "NAND %luMiB %d-bit", \
					result->chipsize, result->iowidth16 ? 16 : 8);

		bdata->devs[0].name = bdata->devname;
		bdata->devs[0].id = result->id[1];    /* dev id */
		bdata->devs[0].pagesize = result->pagesize;
		bdata->devs[0].erasesize = result->blocksize;
		bdata->devs[0].chipsize = result->chipsize;
		if (result->iowidth16)
			bdata->devs[0].options |= NAND_BUSWIDTH_16;

		memset(&bdata->devs[1], 0, sizeof(struct nand_flash_dev));

		bdata->id_len = result->id_len;
		memcpy(bdata->id, result->id, result->id_len);

		p4a_register_device(&p4a_device_mbnand, bdata);
	}

	mreqb_free(rq);
}

int __init p4a_mbnand_init(struct p4a_mbnand_platdata *data)
{
	struct mreqb* rq;

	rq = mreqb_alloc(sizeof(struct mbnand_arg));

	MREQB_BIND_CMD(rq, NAND_REQUEST);
	MREQB_SET_SUBCMD(rq, MB_NAND_GETINFO);

	rq->complete = __mb_nand_getinfo_complete;
	rq->context = data;

	mreqb_submit(rq);

	return 0;
}
#else
int __init p4a_mbnand_init(struct p4a_mbnand_platdata *data) {return 0;}
#endif

/* --------------------------------------------------
 *  Virtual Serial (Mailbox based)
 * ------------------------------------------------ */
#if defined(CONFIG_MAILBOX_SERIAL_P4A) || defined(CONFIG_MAILBOX_SERIAL_P4A_MODULE)
static int __init p4a_add_device_mbserial(void)
{
	int i;

	for (i=0; i<CONFIG_MAILBOX_SERIAL_P4A_NRPORTS; i++) {
		struct platform_device * pdev;

		pdev = platform_device_alloc("p4a-mbserial", i);

		platform_device_add(pdev);
	}

	return 0;
}
#else
static int __init p4a_add_device_mbserial(void) { return 0; }
#endif

/* --------------------------------------------------
 *  Virtual Ethernet (Mailbox based)
 * ------------------------------------------------ */
#if defined(CONFIG_P4A_MAILBOX_ETHER) || defined(CONFIG_P4A_MAILBOX_ETHER_MODULE)
static struct platform_device p4a_device_mbether = {
	.id	= -1,
	.name = "p4a-mbether",
};

static int __init p4a_add_device_mbether(void)
{
	return platform_device_register(&p4a_device_mbether);
}
#else
static int __init p4a_add_device_mbether(void) { return 0; }
#endif

void __init p4a_devices_init(void)
{
	p4a_add_device_wdt();

	/* virtual devices */
	p4a_add_device_mbserial();
	p4a_add_device_mbether();
}
