/*
 * linux/arch/arm/mach-p4a/include/mach/entry-macro.S
 *
 *  Copyright (C) 2013 Innofidei Inc.
 *
 * Low-level IRQ helper macros for warpcore5000.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/p4a-regs.h>
#include <mach/debug-macro.S>

	.macro	disable_fiq
	.endm

	.macro  get_irqnr_preamble, base, tmp
	ldr \base, =P4A_AIC_BASE
	.endm

	.macro  arch_ret_to_user, tmp1, tmp2
	.endm

	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
	ldr		\irqstat, [\base, #0x08]	/* INT VALID */
	mov \irqnr, #0
	teq \irqstat, #0
	beq 1003f

1001:
	tst \irqstat, #15
	bne 1002f
	add \irqnr, \irqnr, #4
	movs    \irqstat, \irqstat, lsr #4
	bne 1001b

1002:
	tst \irqstat, #1
	bne 1003f
	add \irqnr, \irqnr, #1
	movs    \irqstat, \irqstat, lsr #1
	bne 1002b
1003:
	
	.endm

