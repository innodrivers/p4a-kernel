/*
 *  linux/arch/arm/mach-p4a/clock.c
 *
 *  Copyright (C) 2013 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#include <asm/clkdev.h>
#include <mach/hardware.h>
#include <mach/p4a-regs.h>
#include <mach/clock.h>

static DEFINE_SPINLOCK(clocks_lock);

static int p4a_clk_enable(struct clk* clk);
static void p4a_clk_disable(struct clk* clk);

int clk_enable(struct clk *clk)
{
	unsigned long flags;
	int ret;

	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	spin_lock_irqsave(&clocks_lock, flags);

	ret = p4a_clk_enable(clk);

	spin_unlock_irqrestore(&clocks_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_enable);


void clk_disable(struct clk *clk)
{
	unsigned long flags;

	if (clk == NULL || IS_ERR(clk))
		return ;

	spin_lock_irqsave(&clocks_lock, flags);

	p4a_clk_disable(clk);

	spin_unlock_irqrestore(&clocks_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	unsigned long flags;
	unsigned long rate;

	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	spin_lock_irqsave(&clocks_lock, flags);

	if (clk->calcrate)
		clk->rate = clk->calcrate(clk);

	rate = clk->rate;

	spin_unlock_irqrestore(&clocks_lock, flags);

	return rate;
}
EXPORT_SYMBOL(clk_get_rate);

void clks_register(struct clk_lookup *clks, size_t num)
{
	int i;

	for (i = 0; i < num; i++)
		clkdev_add(&clks[i]);
}

/*-----------------------------------------------------*/
static struct clk* p4a_clksel_parent(struct clk *clk)
{
	u32 v;
	struct clk* parent = NULL;
	const struct clksel *clks;

	v = __raw_readl(clk->clksel_reg) & clk->clksel_mask;
	v >>= __ffs(clk->clksel_mask);

	for (clks = clk->clksel; clks->parent != NULL; clks++) {
		if (clks->val == v) {
			parent = clks->parent;
			break;
		}
	}

	return parent;
}

static int p4a_clk_enable(struct clk* clk)
{
	int ret;

	clk->usecount++;

	if (clk->usecount > 1) {
		// already enabled
		return 0;
	}

	if (/*clk->parent == NULL && */clk->clksel != NULL) {
		/* we always find parent if this clock comes from a gate */
		clk->parent = p4a_clksel_parent(clk);
	}

	if (clk->parent != NULL) {
		ret = p4a_clk_enable(clk->parent);
		if (ret) {
			WARN(1, "clock %s could not enable parent %s\n",
						clk->name, clk->parent->name);
			goto err1;
		}
	}

	if (clk->enable != NULL) {
		ret = clk->enable(clk);
		if (ret) {
			WARN(1, "clock %s could not enable\n", clk->name);
			goto err2;
		}
	}

	return 0;
err2:
	if (clk->parent != NULL) {
		p4a_clk_disable(clk->parent);
	}
err1:
	clk->usecount--;

	return ret;
}

static void p4a_clk_disable(struct clk* clk)
{
	if (clk->usecount == 0) {
		WARN(1, "Trying disable clock %s with zero usecount\n", \
				clk->name);

		return ;
	}

	clk->usecount--;

	if (clk->usecount > 0) {
		// somebody still use this clock
		return;
	}

	if (clk->disable != NULL)
		clk->disable(clk);

	if (clk->parent != NULL)
		p4a_clk_disable(clk->parent);
}

