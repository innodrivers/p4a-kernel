/*
 * arch/arm/mach-i10/include/mach/timex.h
 *
 * Copyright (c) 2010 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARCH_TIMEX_H
#define __ASM_ARCH_TIMEX_H

#ifdef CONFIG_P4A_FPGA
#define CLOCK_TICK_RATE		 (38400000)
#else
#define CLOCK_TICK_RATE		 (102400000)
#endif

#endif
