/*
 * linux/arch/arm/mach-p4a/mailbox.c
 *
 *  modify from omap2 mailbox
 *
 *  Copyright (C) 2013 Innofidei Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>


#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/p4a-regs.h>
#include <mach/p4a_mailbox.h>

enum P4A_MBOX_ID {
	P4A_MBOX_ID_0 = 0,		// CPU1
	P4A_MBOX_ID_1,			// DSP
	P4A_MBOX_ID_2,			// CPU 2
	P4A_MBOX_ID_3,
	P4A_MBOX_ID_4,
	P4A_MBOX_ID_MAX,
};

#define MAILBOX_INTSTATUS(m)	(0x00 + (m) * 0x40)
#define MAILBOX_RAWINTSTATUS(m)	(0x08 + (m) * 0x40)
#define MAILBOX_INTMASK(m)		(0x10 + (m) * 0x40)
#define MAILBOX_MESSAGE(m)		(0x18 + (m) * 0x40)
#define MAILBOX_FIFOSTATUS(m)	(0x20 + (m) * 0x40)
#define MAILBOX_MSGSTATUS(m)	(0x28 + (m) * 0x40)
#define MAILBOX_THRESHOLD(m)	(0x30 + (m) * 0x40)
#define MAILBOX_SOFTINTTRIGGER	0x13c

/* bit define for INTSTATUS, RAWINTSTATUS and INTMASK */
#define MBOX_NOTFULL_BIT(m)		(0x1 << ((m) + 1))
#define MBOX_NEWMSG_BIT			(0x1)
#define MBOX_SWINT_BIT			(0x1 << 6)

/* bit define for FIFOSTATUS */
#define MBOX_FIFO_FULL_BIT		(0x1 << 0)
#define MBOX_FIFO_EMPTY_BIT		(0x1 << 1)

/*-------------------------------------------------*/

static inline u32 mbox_read_reg(struct p4a_mbox *mbox, int off)
{
	return __raw_readl(mbox->base + off);
}

static inline void mbox_write_reg(struct p4a_mbox* mbox, int off, u32 val)
{
	__raw_writel(val, mbox->base + off);
}

static int p4a_mbox_startup(struct p4a_mbox *mbox)
{
	struct p4a_mbox_priv* p = mbox->priv;

	mbox_write_reg(mbox, p->int_mask, ~0x0);
	mbox_write_reg(mbox, p->rawint_stat, ~0x0);		//W1C
	mbox_read_reg(mbox, p->int_stat);

	mbox_write_reg(mbox, p->threshold, 0x1);

	return 0;
}

static int p4a_mbox_shutdown(struct p4a_mbox *mbox)
{
	return 0;
}

static mbox_msg_t p4a_mbox_fifo_read(struct p4a_mbox * mbox)
{
	struct p4a_mbox_priv* p = mbox->priv;
	struct p4a_mbox_fifo *rx_fifo = &p->rx_fifo;

	return (mbox_msg_t)mbox_read_reg(mbox, rx_fifo->msg);
}

static void p4a_mbox_fifo_write(struct p4a_mbox *mbox, mbox_msg_t msg)
{
	struct p4a_mbox_priv* p = mbox->priv;
	struct p4a_mbox_fifo *tx_fifo = &p->tx_fifo;

	return mbox_write_reg(mbox, tx_fifo->msg, msg);
}

static int p4a_mbox_fifo_empty(struct p4a_mbox *mbox)
{
	struct p4a_mbox_priv* p = mbox->priv;
	struct p4a_mbox_fifo *rx_fifo = &p->rx_fifo;

	return ((mbox_read_reg(mbox, rx_fifo->fifo_stat) & MBOX_FIFO_EMPTY_BIT) != 0);
}

static int p4a_mbox_fifo_full(struct p4a_mbox *mbox)
{
	struct p4a_mbox_priv* p = mbox->priv;
	struct p4a_mbox_fifo *tx_fifo = &p->tx_fifo;

	return ((mbox_read_reg(mbox, tx_fifo->fifo_stat) & MBOX_FIFO_FULL_BIT) != 0);
}

static void p4a_mbox_enable_irq(struct p4a_mbox *mbox, p4a_mbox_irq_t irq)
{
	struct p4a_mbox_priv* p = mbox->priv;
	u32 val, bit = (irq == IRQ_TX) ? p->notfull_bit : p->newmsg_bit;

	val = mbox_read_reg(mbox, p->int_mask);
	val &= ~bit;
	mbox_write_reg(mbox, p->int_mask, val);
}

static void p4a_mbox_disable_irq(struct p4a_mbox *mbox, p4a_mbox_irq_t irq)
{
	struct p4a_mbox_priv* p = mbox->priv;
	u32 val, bit = (irq == IRQ_TX) ? p->notfull_bit : p->newmsg_bit;

	val = mbox_read_reg(mbox, p->int_mask);
	val |= bit;
	mbox_write_reg(mbox, p->int_mask, val);
}

static void p4a_mbox_ack_irq(struct p4a_mbox *mbox, p4a_mbox_irq_t irq)
{
	//TODO
}

static int p4a_mbox_is_irq(struct p4a_mbox *mbox, p4a_mbox_irq_t irq)
{
	struct p4a_mbox_priv* p = mbox->priv;
	u32 bit = (irq == IRQ_TX) ? p->notfull_bit : p->newmsg_bit;
	u32 status;

	status = mbox_read_reg(mbox, p->int_stat);

	return ((status & bit) != 0); 
}

static struct p4a_mbox_ops p4a_mbox_ops = {
	.startup	= p4a_mbox_startup,
	.shutdown	= p4a_mbox_shutdown,
	.fifo_read	= p4a_mbox_fifo_read,
	.fifo_write	= p4a_mbox_fifo_write,
	.fifo_empty	= p4a_mbox_fifo_empty,
	.fifo_full	= p4a_mbox_fifo_full,
	.enable_irq	= p4a_mbox_enable_irq,
	.disable_irq = p4a_mbox_disable_irq,
	.ack_irq	= p4a_mbox_ack_irq,
	.is_irq		= p4a_mbox_is_irq,
};

static struct p4a_mbox_priv p4a_mbox_cpu1_priv = {
	.tx_fifo = {
		.msg	= MAILBOX_MESSAGE(P4A_MBOX_ID_0),
		.fifo_stat	= MAILBOX_FIFOSTATUS(P4A_MBOX_ID_0),
		.msg_stat	= MAILBOX_MSGSTATUS(P4A_MBOX_ID_0),
	},
	.rx_fifo = {
		.msg	= MAILBOX_MESSAGE(P4A_MBOX_ID_2),
		.fifo_stat	= MAILBOX_FIFOSTATUS(P4A_MBOX_ID_2),
		.msg_stat	= MAILBOX_MSGSTATUS(P4A_MBOX_ID_2),
	},
	.threshold = MAILBOX_THRESHOLD(P4A_MBOX_ID_2),
	.int_stat	= MAILBOX_INTSTATUS(P4A_MBOX_ID_2),
	.int_mask	= MAILBOX_INTMASK(P4A_MBOX_ID_2),
	.rawint_stat	= MAILBOX_RAWINTSTATUS(P4A_MBOX_ID_2),
	.notfull_bit	= MBOX_NOTFULL_BIT(P4A_MBOX_ID_0),
	.newmsg_bit		= MBOX_NEWMSG_BIT,
};

/* mailbox communicate with CPU1 */
static struct p4a_mbox mbox_cpu1_info = {
	.name	= "CPU1",
	.ops	= &p4a_mbox_ops,
	.priv	= &p4a_mbox_cpu1_priv,
};

static struct p4a_mbox *p4a_mboxes[] = { &mbox_cpu1_info, NULL };

static int __devinit p4a_mailbox_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct p4a_mbox **list;
	int i;
	int ret;
	struct device *dev;
	int irq;
	void __iomem *base;
	struct clk* clk;


	dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (unlikely(res == NULL)) {
		dev_err(dev, "Invalid IRQ resource!\n");
		return -ENODEV;
	}

	irq = res->start;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(res == NULL)) {
		dev_err(dev, "Invalid MEM resource!\n");
		return -ENODEV;
	}

	base = ioremap(res->start, resource_size(res));
	if (base == NULL) {
		dev_err(dev, "Could not ioremap mailbox!\n");
		return -ENOMEM;
	}

	clk = clk_get(dev, "MAILBOX_CLK");
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_err(dev, "Could not get mailbox clock!\n");
		goto _fail_clk_get;
	}

	list = p4a_mboxes;
	for (i=0; list[i]; i++) {
		list[i]->irq = irq;
		list[i]->base = base;
		list[i]->clk = clk;
	}

	ret = p4a_mbox_register(dev, list);
	if (ret)
		goto _fail_mbox_register;

	return 0;

_fail_mbox_register:
	clk_put(clk);

_fail_clk_get:
	iounmap(base);

	return ret;
}

static struct platform_driver p4a_mailbox_driver = {
	.driver = {
		.name	= "p4a-mailbox",
	},
};

extern int __init p4a_add_device_mailbox(void);

static int __init p4a_mailbox_init(void)
{
	p4a_add_device_mailbox();

	return platform_driver_probe(&p4a_mailbox_driver, p4a_mailbox_probe);
}

/* p4a mailbox driver register needs to be done before 
   machine_init function, so p4a_mailbox_init is a postcore_initcall.
 */
postcore_initcall(p4a_mailbox_init);
