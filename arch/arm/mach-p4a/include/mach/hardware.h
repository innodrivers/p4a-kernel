/*
 *  arch/arm/mach-p4a/include/mach/hardware.h
 *
 *  Copyright (c) 2013	Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARCH_HARDWARE_H
#define _ASM_ARCH_HARDWARE_H

#if 0
/* macro to get at IO space when running virtually */
#ifdef CONFIG_MMU
/*
 * 	physcial				<--> virtual
 *
 *  0xe0000000 - 0xe1ffffff <--> 0xfa000000 - 0xfbffffff
 */
#define io_p2v(x) ( 0xfa000000 + ((x) & 0x01ffffff))
#define io_v2p(x) ( 0xe0000000 + ((x) & 0x01ffffff))
#else
#define io_p2v(x)		(x)
#endif

#ifndef __ASSEMBLY__

# define __REG(x)	(*((volatile u32 *)io_p2v(x)))

#else

# define __REG(x)	io_p2v(x)

#endif

#endif

#endif  /* _ASM_ARCH_HARDWARE_H */
