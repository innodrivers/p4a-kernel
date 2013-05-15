/*
 *  arch/arm/mach-p4a/include/mach/clock.h
 *
 *  Copyright (C) 2013 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ARCH_ARM_P4A_CLOCK_H
#define __ARCH_ARM_P4A_CLOCK_H
#include <asm/clkdev.h>

struct clk;

struct clkrate {
	u32 div;
	u32 val;
};

struct clksel {
	struct clk* parent;
	u32		val;
};

struct clkdiv {
	void __iomem *clkdiv_reg;
	u32		mask;
	u32		shift;
	struct clkrate *rates;
};

struct clk {
	struct list_head	node;
	const char *name;
	struct clk* parent;

	unsigned long		rate;
	unsigned int		usecount;

	int	(*enable)(struct clk *);
	int (*disable)(struct clk *);
	unsigned long (*calcrate)(struct clk *);
	struct clk* (*find_parent)(struct clk*);

	void __iomem* clkpll_reg;

	void __iomem* clksel_reg;
	u32		clksel_mask;
	const struct clksel *clksel;

	const struct clkdiv *clkdiv;
	const struct clkdiv *clkdiv2;

	void __iomem *clken_reg;
	u32			clken_bit;	

	void __iomem* modrst_reg;
	u32			modrst_bit;
};

#define INIT_CLKREG(_clk,_devname,_conname)		\
	{						\
		.clk		= _clk,			\
		.dev_id		= _devname,		\
		.con_id		= _conname,		\
	}


#define DEFINE_CLK(_name, _ops)		\
struct clk clk_##_name = {				\
		.ops	= _ops, 			\
	}


void clks_register(struct clk_lookup *clks, size_t num);

#endif	/* __ARCH_ARM_P4A_CLOCK_H */

