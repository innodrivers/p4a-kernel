/*
 * linux/include/asm-arm/arch-i10/system.h
 *
 *  Copyright (c) 2010	Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <asm/proc-fns.h>
#include <mach/hardware.h>

static inline void arch_idle(void)
{
	cpu_do_idle();
}


extern void arch_reset(char mode, const char* cmd);

#endif
