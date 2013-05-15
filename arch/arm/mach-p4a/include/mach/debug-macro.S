/* arch/arm/mach-i10/include/mach/debug-macro.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 2010 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#include <mach/hardware.h>
#include <mach/p4a-regs.h>

	.macro	addruart, rp, rv
	ldr		\rp, =(LL_DBG_UART_PHYS)		@ physical
	ldr		\rv, =(LL_DBG_UART_BASE)		@ virtual
	.endm
	
	.macro	senduart, rd, rx
	strb    \rd, [\rx, #UTXR_OFF]       @ wirte to Transmitter Holding register
	.endm


	.macro waituart, rd, rx
1001:
	ldr \rd, [\rx, #USR_OFF]			@read status register
	and \rd, \rd, #LL_DBG_USR_TFLEVEL_MASK
	lsr \rd, \rd, #LL_DBG_USR_TFLEVEL_SHIFT
	cmp \rd, #LL_DBG_UTX_FIFOSZ
	bhs 1001b
	.endm

	.macro busyuart, rd, rx
	.endm
