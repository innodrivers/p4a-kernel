/*
 *  linux/arch/arm/mach-p4a/iomux.c
 *
 *  Copyright (C) 2013 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/p4a-regs.h>
#include <mach/iomux.h>

#define IOMUX_REG(i)	(P4A_PMU_BASE + 0x200 + (((i) - 1)<<2))		/* i : [1:18] */

typedef struct {
	union {
		unsigned long mpv;
		struct {
			u8	mode;
			u8	shift;
			u8	regid;
			u8	noused;
		};
	};
} mux_pin_s;

static inline u32 rd_iomux_regl(int iomux_regid)
{
	return readl(IOMUX_REG(iomux_regid));
}

static inline void wr_iomux_regl(int iomux_regid, u32 val)
{
	writel(val, IOMUX_REG(iomux_regid));
}

void p4a_iomux_config(p4a_mux_pin_t pins[], int num)
{
	unsigned long flags;
	int cached_regid = -1;
	unsigned long val = 0;
	int i, cnt = 0;

	local_irq_save(flags);
	
	for (i=0; i<num; i++) {
		mux_pin_s *p = (mux_pin_s*)(&pins[i]);

		if (p->regid < 1 || p->regid > 18)
			continue;
	
		if (cached_regid != p->regid) {
			if (cnt)
				wr_iomux_regl(cached_regid, val);

			cached_regid = p->regid;
			val = rd_iomux_regl(cached_regid);
			cnt = 0;
		}

		val &= ~(0xf << p->shift);
		val |= (p->mode << p->shift);
		cnt += 1;
	}

	if (cnt)
		wr_iomux_regl(cached_regid, val);
	local_irq_restore(flags);
}

EXPORT_SYMBOL(p4a_iomux_config);
