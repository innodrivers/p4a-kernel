/*
 * arch/arm/mach-p4a/include/mach/p4a_wdt.h
 *
 * Copyright (c) 2013 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __P4A_WATCHDOG_TIMER_H
#define __P4A_WATCHDOG_TIMER_H

#define WDTMR		0x00
#define WDTCR		0x04
#define		WDTCR_ENABLE				(0x1<<0)
#define		WDTCR_EVENT_RESET_COUNT		(0x1<<1)
#define		WDTCR_RESET_CHIP_EN			(0x1<<2)
#define		WDTCR_COUNT_CLEAR			(0x1<<3)
#define WDTCNTR		0x08

struct wdt_p4a_platform_data {
	char			clk_name[16];
	void __iomem	*membase;
};

#endif
