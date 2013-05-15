/*
 * linux/arch/arm/mach-p4a/musb.c
 *
 * This file contains the board specific details for the
 * MENTOR USB OTG controller on P4A.
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

#include <linux/usb/musb.h>

#include <mach/hardware.h>
#include <mach/irqs.h>

#include <mach/p4a-regs.h>

#ifdef CONFIG_USB_MUSB_SOC

#define P4A_USB_NAME	"musb_hdrc"

static u64 musb_dmamask = DMA_BIT_MASK(32);

#define MUSB_RESOURCE_BUILD_HELPER(base, size, irq, dma_irq)    \
{                                           \
	.start  = (base),                       \
	.end    = (base) + (size) - 1,          \
	.flags  = IORESOURCE_MEM,               \
},                                          \
{       /* general IRQ */                   \
	.start  = (irq),                        \
	.end    = (irq),                        \
	.flags  = IORESOURCE_IRQ,               \
	.name   = "mc",                         \
},                                          \
{       /* DMA IRQ */                       \
	.start  = (dma_irq),                    \
	.end    = (dma_irq),                    \
	.flags  = IORESOURCE_IRQ,               \
	.name   = "dma",                        \
}

#define MUSB_DEVICE_BUILD_HELPER(_name, _id, _res, _data)   \
{                                                   \
	.id = (_id),                                    \
	.name   = (_name),                              \
	.num_resources  = ARRAY_SIZE(_res),             \
	.resource   = (_res),                           \
	.dev    = {                                     \
		.dma_mask   = &musb_dmamask,                \
		.coherent_dma_mask = DMA_BIT_MASK(32),      \
		.platform_data = (_data),                   \
	},                                              \
}

static struct resource p4a_musb_resources[][3] = {
    { MUSB_RESOURCE_BUILD_HELPER(P4A_USB_PHYS, P4A_USB_SIZE, IRQ_USB_MCU, IRQ_USB_DMA) },
    { MUSB_RESOURCE_BUILD_HELPER(P4A_USB2_PHYS, P4A_USB2_SIZE, IRQ_USB2_MCU, IRQ_USB2_DMA) },
};


static struct musb_fifo_cfg p4a_musb_fifo_cfg[] = {
    { .hw_ep_num = 1,   .style = FIFO_TX,   .maxpacket = 512, },
	{ .hw_ep_num = 1,   .style = FIFO_RX,   .maxpacket = 512, },
	{ .hw_ep_num = 2,   .style = FIFO_TX,   .maxpacket = 512, },
	{ .hw_ep_num = 2,   .style = FIFO_RX,   .maxpacket = 512, },
	{ .hw_ep_num = 3,   .style = FIFO_TX,   .maxpacket = 512, },
	{ .hw_ep_num = 3,   .style = FIFO_RX,   .maxpacket = 512, },
	{ .hw_ep_num = 4,   .style = FIFO_TX,   .maxpacket = 512, },
	{ .hw_ep_num = 4,   .style = FIFO_RX,   .maxpacket = 512, },
	{ .hw_ep_num = 5,   .style = FIFO_TX,   .maxpacket = 512, },
	{ .hw_ep_num = 5,   .style = FIFO_RX,   .maxpacket = 512, },
	{ .hw_ep_num = 6,   .style = FIFO_TX,   .maxpacket = 1024, },
	{ .hw_ep_num = 6,   .style = FIFO_RX,   .maxpacket = 1024, },
	{ .hw_ep_num = 7,   .style = FIFO_RX,   .maxpacket = 4096, },
	{ .hw_ep_num = 7,   .style = FIFO_TX,   .maxpacket = 4096, },
};

static struct musb_hdrc_config musb_config = {
	.fifo_cfg   = p4a_musb_fifo_cfg,
	.fifo_cfg_size = ARRAY_SIZE(p4a_musb_fifo_cfg),
	.multipoint = 1,
	.num_eps    = 8,
	.ram_bits   = 12,
};

static struct musb_hdrc_platform_data p4a_musb_plats[] = {
	{       /* USB (with ULPI) */
#ifdef CONFIG_USB_MUSB_OTG
		.mode       = MUSB_OTG,
#elif defined(CONFIG_USB_MUSB_HDRC_HCD)
		.mode       = MUSB_HOST,
#elif defined(CONFIG_USB_GADGET_MUSB_HDRC)
		.mode       = MUSB_PERIPHERAL,
#endif
		.clock		= "USB_CLK",
		.config     = &musb_config,

		.power      = 250,  /* up to 500 mA */
	},
	{       /* USB2 (with UTMI) */
#ifdef CONFIG_USB_MUSB_OTG
		.mode       = MUSB_OTG,
#elif defined(CONFIG_USB_MUSB_HDRC_HCD)
		.mode       = MUSB_HOST,
#elif defined(CONFIG_USB_GADGET_MUSB_HDRC)
		.mode       = MUSB_PERIPHERAL,
#endif
		.clock		= "USB_CLK",
		.config     = &musb_config,

		.power      = 250,  /* up to 500 mA */
	}
};

static struct platform_device p4a_musb_devices[] = {
	MUSB_DEVICE_BUILD_HELPER(P4A_USB_NAME, 0, p4a_musb_resources[0], &p4a_musb_plats[0]),
	MUSB_DEVICE_BUILD_HELPER(P4A_USB_NAME, 1, p4a_musb_resources[1], &p4a_musb_plats[1]),
};

int __init p4a_usb_musb_init(int id)
{
	if (id < 0 || id >= ARRAY_SIZE(p4a_musb_devices))
		return -EINVAL;

	return platform_device_register(&p4a_musb_devices[id]);
}

#else
int __init p4a_usb_musb_init(int id)
{
	return 0;
}
#endif /* CONFIG_USB_MUSB_SOC */
