/*
 * arch/arm/mach-p4a/include/mach/p4a_Timer.h
 *
 * Copyright (c) 2013 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __P4A_TIME_H
#define __P4A_TIME_H

#define TM0R		0x00		/* Timer Match Register 0 */
#define TM1R		0x04		/* Timer Match Register 1 */
#define TCR			0x08		/* Timer Control Register */
#define TCNTR		0x0c		/* Timer Counter Register */
#define TISR		0x10		/* Timer Interrupt Status Register */
#define TICR		0x14		/* Timer Interrupt Clear Register */
#define TIER		0x18		/* Timer Interrupt Enable Register */

/* TCR bit fields */
#define TCR_EN		(0x1<<0)
#define TCR_M0RST	(0x1<<1)
#define TCR_M1RST	(0x1<<2)
#define TCR_CLR		(0x1<<3)

/* TISR bit fields */
#define TISR_M0		(0x1<<0)
#define TISR_M1		(0x1<<1)

/* TICR bit fields */
#define TICR_M0		(0x1<<0)
#define TICR_M1		(0x1<<1)

/* TIER bit fields */
#define TIER_M0		(0x1<<0)
#define TIER_M1		(0x1<<1)

#endif
