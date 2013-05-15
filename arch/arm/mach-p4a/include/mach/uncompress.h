/*
 * linux/arch/arm/mach-i10/include/mach/uncompress.h
 *
 * Copyright (c) 2010 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <mach/hardware.h>
#include <mach/p4a-regs.h>


#define REG32(addr) ((volatile unsigned long *)(addr))

static inline void putc(char c)
{
	int tf_level;

	do {
		tf_level = (*REG32(LL_DBG_UART_PHYS + USR_OFF) & LL_DBG_USR_TFLEVEL_MASK) >> LL_DBG_USR_TFLEVEL_SHIFT;
	}while(tf_level >= LL_DBG_UTX_FIFOSZ);

	*REG32(LL_DBG_UART_PHYS + UTXR_OFF) = c;
}

/*
 * This does not append a newline
 */
static inline void flush(void)
{
}

static inline void arch_decomp_setup(void)
{
#ifdef CONFIG_P4A_FPGA
	unsigned long pclk = 38400000;

	*REG32(LL_DBG_UART_PHYS + UCR_OFF) = 0;

	*REG32(LL_DBG_UART_PHYS + UMR_OFF) = LL_DBG_UMR_VALUE(pclk, 115200);

	*REG32(LL_DBG_UART_PHYS + UCR_OFF) = 0x20 | (0x1<<6) | (0x1<<12); 
#endif
}

/*
 * nothing to do
 */
#define arch_decomp_wdog()
