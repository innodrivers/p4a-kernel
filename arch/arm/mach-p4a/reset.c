/*
 * linux/arch/arm/mach-p4a/reset.c
 *
 *  Copyright (C) 2013 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <mach/system.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>

#include <mach/hardware.h>
#include <mach/p4a-regs.h>
#include <mach/p4a_wdt.h>

static void do_wdt_reset(void)
{
	struct clk* clk;
	unsigned long timeout = 500;	// milliseconds
	void __iomem* wdt_base = P4A_WDT_BASE; 
	unsigned long rate;
	unsigned int load;

	clk = clk_get_sys("p4a-wdt", "WDT_CLK");
	BUG_ON(IS_ERR(clk));
	clk_enable(clk);

	rate = clk_get_rate(clk);
	load = timeout * rate / 1000;

	writel(0, wdt_base + WDTCR);		//disable watchdog
	writel(WDTCR_COUNT_CLEAR, wdt_base + WDTCR);	// clear count

	writel(load, wdt_base + WDTMR);

	writel(readl(PMU_CTRL_REG) | (1<<28), PMU_CTRL_REG);	// watchdog reset whole chip enable

	writel(WDTCR_ENABLE | WDTCR_RESET_CHIP_EN, 
			wdt_base + WDTCR);

	/* wait for reset to assert ... */
	mdelay(timeout);

	printk(KERN_ERR "Watchdog reset failed to assert reset!\n");
}

void arch_reset(char mode, const char* cmd)
{
	switch (mode) {
	case 's':
		break;
	case 'h':
		/* initialize the watchdog and let it fire */
		do_wdt_reset();
		break;
	}
}

