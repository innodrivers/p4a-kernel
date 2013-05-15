/*
 *  linux/arch/arm/mach-p4a/clock_data.c
 *
 *  Copyright (C) 2013 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <asm/clkdev.h>

#include <mach/hardware.h>
#include <mach/p4a-regs.h>
#include <mach/clock.h>

#ifdef CONFIG_P4A_FPGA
static struct clk clk_timer = {
	.name	= "TIMER",
	.rate	= 38400000,
};

static struct clk clk_p4timer = {
	.name	= "p4timer_clk",
	.rate	= 38400000,
};

static struct clk clk_uart1 = {
	.name	= "uart1_clk",
	.rate	= 38400000,
};

static struct clk clk_uart2 = {
	.name	= "uart2_clk",
	.rate	= 38400000,
};

static struct clk clk_uart4w = {
	.name	= "uart4w_clk",
	.rate	= 38400000,
};

static struct clk clk_wdt = {
	.name	= "wdt_clk",
	.rate	= 38400000,
};

static struct clk clk_mbox = {
	.name	= "mbox_clk",
	.rate	= 38400000,
};

static struct clk clk_nand = {
	.name	= "nand_clk",
};

static struct clk_lookup p4a_clks[] = {
	INIT_CLKREG(&clk_timer, NULL, "TIMER_CLK"),
	INIT_CLKREG(&clk_p4timer, NULL, "P4TIMER_CLK"),
	INIT_CLKREG(&clk_uart1, "p4a-uart.0", NULL),
	INIT_CLKREG(&clk_uart2, "p4a-uart.1", NULL),
	INIT_CLKREG(&clk_uart4w, "p4a-uart4w", NULL),
	INIT_CLKREG(&clk_mbox, "p4a-mailbox", "MAILBOX_CLK"),
	INIT_CLKREG(&clk_wdt, "p4a-wdt", "WDT_CLK"),
	INIT_CLKREG(&clk_nand, "p4a-nand", NULL),
};
#else
/*-------------------------------------------------------*/
static inline int p4a_clken_setbits(struct clk *clk)
{
	u32 val;

	if (clk->clken_reg != NULL) {
		val = readl(clk->clken_reg);

		if (((val & clk->clken_bit) ^ clk->clken_bit) != 0) {
			val |= clk->clken_bit;
			writel(val, clk->clken_reg);
		}
	}

	return 0;
}

static inline int p4a_clken_clearbits(struct clk *clk)
{
	u32 val;

	if (clk->clken_reg != NULL) {
		val = readl(clk->clken_reg);

		if (((val & clk->clken_bit) ^ clk->clken_bit) != 0) {
			val &= ~clk->clken_bit;
			writel(val, clk->clken_reg);
		}
	}

	return 0;
}

static inline int p4a_softreset_setbits(struct clk *clk)
{
	u32 val;

	if (clk->modrst_reg != NULL) {
		val = readl(clk->modrst_reg);

		if (((val & clk->modrst_bit) ^ clk->modrst_bit) != 0) {
			val |= clk->modrst_bit;		// reset Reset_n
			writel(val, clk->modrst_reg);
		}
	}

	return 0;
}

static inline int p4a_softreset_clearbits(struct clk *clk)
{
	u32 val;

	if (clk->modrst_reg != NULL) {
		val = readl(clk->modrst_reg);

		if (((val & clk->modrst_bit) ^ clk->modrst_bit) != 0) {
			val &= ~clk->modrst_bit;		// hold Reset_n
			writel(val, clk->modrst_reg);
		}
	}

	return 0;
}

//////////////////////////////////
static int p4a_set_clken_and_softreset(struct clk *clk)
{
	p4a_clken_setbits(clk);
	p4a_softreset_setbits(clk);

	return 0;
}

static int p4a_clear_clken_and_softreset(struct clk *clk)
{
	p4a_softreset_clearbits(clk);
	p4a_clken_clearbits(clk);

	return 0;
}

static unsigned long p4a_followparent_calcrate(struct clk *clk)
{
	unsigned long rate;
	struct clk* parent;

	parent = clk->parent;

	if (parent == NULL) {
		BUG();
	}

	rate = parent->rate;
	if (parent->calcrate)
		rate = parent->calcrate(parent);
		
	return rate;
}

static u32 __clkdiv_to_divisor(const struct clkdiv *clkd, u32 val)
{
	const struct clkrate *clkr;

	if (clkd->rates == NULL) {
		return val + 1;
	}

	for (clkr = clkd->rates; clkr->div != 0; clkr++) {
		if (clkr->val == val)
			break;
	}

	if (unlikely(clkr->div == 0)) {	// Error
		WARN(1, "could not change clkdiv %d\n", val);
		return 0;
	}

	return clkr->div;
}

static u32 __read_clk_divisor(struct clk *clk)
{
	u32 val;
	u32 div1, div2;

	/* Get 1st stage divider */
	if (clk->clkdiv == NULL)
		return 0;

	val = readl(clk->clkdiv->clkdiv_reg);
	val >>= clk->clkdiv->shift;
	val &= clk->clkdiv->mask;

	div1 = __clkdiv_to_divisor(clk->clkdiv, val);
	if (div1 == 0)
		return 0;

	/* Get 2nd stage divider */
	if (clk->clkdiv2 == NULL)
		return div1;

	val = readl(clk->clkdiv2->clkdiv_reg);
	val >>= clk->clkdiv2->shift;
	val &= clk->clkdiv2->mask;

	div2 = __clkdiv_to_divisor(clk->clkdiv2, val);
	if (div2 == 0)
		return div1;

	return (div1 * div2);
}

static unsigned long p4a_clkdiv_calcrate(struct clk *clk)
{
	unsigned long rate, parent_rate;
	u32 div = 0;

	div = __read_clk_divisor(clk);
	if (div == 0)
		return clk->rate;

	parent_rate = clk->parent->rate;
	if (clk->parent->calcrate)
		parent_rate = clk->parent->calcrate(clk->parent);

	rate = parent_rate / div;

	return rate;
}

static unsigned long p4a_clkpll_calcrate(struct clk* clk)
{
	unsigned long rate, Fin;
	u32 val;
	u32 NR, NF, NO;

	if (unlikely(clk->clkpll_reg == NULL))
		return 0;

	val = readl(clk->clkpll_reg);

	NR = (val & 0x1f) + 1;
	NF = (((val >> 8) & 0x7f) + 1) * 2;
	NO = 1 << ((val >> 24) & 0x3);

	Fin = clk->parent->rate;

	rate = Fin * NF / (NR * NO);

	return rate;
}

/*--------------------------------------------------------*/
static struct clk clk_sys_in;
static struct clk clk_pll1;
static struct clk clk_pll2;
static struct clk clk_pll3;
/*--------------------------------------------------------*/
static const struct clksel arm_source_clksel[] = {
	{.parent = &clk_sys_in,	.val = 0x0},
	{.parent = &clk_pll1, .val = 0x4},
	{.parent = &clk_pll2, .val = 0x5},
	{.parent = &clk_pll3, .val = 0x6},
	{.parent = NULL},
};

static const struct clksel axi_source_clksel[] = {
	{.parent = &clk_sys_in,	.val = 0x0},
	{.parent = &clk_pll1, .val = 0x4},
	{.parent = &clk_pll2, .val = 0x5},
	{.parent = &clk_pll3, .val = 0x6},
	{.parent = NULL},
};

static const struct clksel peri_source_clksel[] = {
	{.parent = &clk_pll1, .val = 0x0},
	{.parent = &clk_pll2, .val = 0x1},
	{.parent = &clk_pll3, .val = 0x2},
	{.parent = NULL},
};

static const struct clkdiv axi_ref_clkdiv = {
	.clkdiv_reg	= PMU_CLKRST2_REG,
	.mask	= 0x1f,
	.shift	= 15,
};

static const struct clkdiv peri_ref_clkdiv = {
	.clkdiv_reg	= PMU_CLKRST2_REG,
	.mask	= 0x1f,
	.shift	= 25,
};

static const struct clkdiv arm_ref_clkdiv = {
	.clkdiv_reg	= PMU_CLKRST2_REG,
	.mask	= 0x1f,
	.shift	= 5,
};

static const struct clkdiv arm2_ref_clkdiv = {
	.clkdiv_reg	= PMU_CLKRST5_REG,
	.mask	= 0x1f,
	.shift	= 0,
};

static const struct clkdiv arm_axi_clkdiv = {
	.clkdiv_reg	= GBL_CFG_ARM_CLK_REG,
	.mask	= 0x1f,
	.shift	= 4,
};

static const struct clkdiv arm_hclk_clkdiv = {
	.clkdiv_reg	= GBL_CFG_ARM_CLK_REG,
	.mask	= 0x1f,
	.shift	= 12,
};

static const struct clkdiv arm2_axi_clkdiv = {
	.clkdiv_reg	= GBL_CFG_ARM2_CLK_REG,
	.mask	= 0x1f,
	.shift	= 8,
};

static const struct clkdiv arm2_hclk_clkdiv = {
	.clkdiv_reg	= GBL_CFG_ARM2_CLK_REG,
	.mask	= 0x1f,
	.shift	= 16,
};

static const struct clkdiv hclk_clkdiv = {
	.clkdiv_reg	= GBL_CFG_BUS_CLK_REG,
	.mask	= 0x1f,
	.shift	= 0,

};

static const struct clkdiv dma_aclk_clkdiv = {
	.clkdiv_reg	= GBL_CFG_PERI_CLK_REG, 
	.mask	= 0x1f,
	.shift	= 24,
};

static const struct clkdiv spim_clkdiv = {
	.clkdiv_reg	= GBL_CFG_PERI_CLK_REG,
	.mask	= 0x1f,
	.shift	= 16,
};

static const struct clkdiv spim2_clkdiv = {
	.clkdiv_reg	= GBL_CFG_PERI2_CLK_REG,
	.mask	= 0x1f,
	.shift	= 8,
};

static const struct clkdiv timer_clkdiv = {
	.clkdiv_reg	= GBL_CFG_PERI_CLK_REG,
	.mask	= 0x1f,
	.shift	= 8,
};

/*------------------------------------------------------*/
/* Clock Input : 19.2MHz */
static struct clk clk_sys_in = {
	.name	= "SYS_IN",
	.rate	= 19200000,
};

/* PLL 1 ~ 3 */
static struct clk clk_pll1 = {
	.name	= "PLL1",
	.parent	= &clk_sys_in,
	.clkpll_reg	= PMU_PLL1_CTRL_REG,
	.calcrate = p4a_clkpll_calcrate,
};

static struct clk clk_pll2 = {
	.name	= "PLL2",
	.parent	= &clk_sys_in,
	.clkpll_reg	= PMU_PLL2_CTRL_REG,
	.calcrate = p4a_clkpll_calcrate,
};

static struct clk clk_pll3 = {
	.name	= "PLL3",
	.parent	= &clk_sys_in,
	.clkpll_reg	= PMU_PLL3_CTRL_REG,
	.calcrate = p4a_clkpll_calcrate,
};

/* ARM2_REF Clock */
static struct clk clk_arm2_ref = {
	.name		= "ARM2_REF",

	/* clock selector */
	.clksel_reg		= PMU_CLKRST5_REG,
	.clksel_mask	= (0x7 << 20),		//bit22:20
	.clksel			= arm_source_clksel, 

	/* clock dividor */
	.clkdiv	= &arm2_ref_clkdiv,

	.calcrate	= p4a_clkdiv_calcrate,
};

/* PERI_REF Clock */
static struct clk clk_peri_ref = {
	.name = "PERI_REF",

	/* clock selector */
	.clksel_reg		= PMU_CLKRST3_REG,
	.clksel_mask	= (0x3 << 25),	//bit26:25
	.clksel			= peri_source_clksel,

	/* clock gating */
	.enable			= p4a_set_clken_and_softreset,
	.disable		= p4a_clear_clken_and_softreset,
	.clken_reg		= PMU_CLKRST1_REG,
	.clken_bit		= (0x1 << 8),

	/* clock dividor */
	.clkdiv			= &peri_ref_clkdiv,

	.calcrate		= p4a_clkdiv_calcrate,
};

/* AXI_REF Clock */
static struct clk clk_axi_ref = {
	.name			= "AXI_REF",

	/* clock selector */
	.clksel_reg		= PMU_CLKRST3_REG,
	.clksel_mask	= (0x7 << 22),	//bit24:22
	.clksel			= axi_source_clksel,

	/* clock gating */
	.enable			= p4a_set_clken_and_softreset,
	.disable		= p4a_clear_clken_and_softreset,
	.clken_reg		= PMU_CLKRST1_REG,
	.clken_bit		= (0x1 << 5),

	/* clock dividor */
	.clkdiv			= &axi_ref_clkdiv,

	.calcrate		= p4a_clkdiv_calcrate,
};

/* SDIOM_REF Clock */
static struct clk clk_sdiom_ref = {
	.name	= "sdiom_ref_clk",
	.enable	= p4a_set_clken_and_softreset,
	.disable = p4a_clear_clken_and_softreset,
	.clken_reg	= PMU_CLKRST1_REG,
	.clken_bit	= (0x1 << 18),
};

#if 0
/* ARM2_AXI Clock */
static struct clk clk_arm2_axi = {
	.name	= "arm2_axi_clk",
	.parent	= &clk_arm2_ref,
	.clkdiv	= &arm2_axi_clkdiv,
	.calcrate	= p4a_clkdiv_calcrate,
};

/* ARM2 Clock */
static struct clk clk_arm2 = {
	.name	= "arm2_clk",
	.parent	= &clk_arm2_ref,
	.enable	= p4a_set_clken_and_softreset,
	.disable = p4a_clear_clken_and_softreset,
	.clken_reg	= GBL_CFG_ARM_CLK_REG,
	.clken_bit	= (0x1 << 1),
};
#endif

/* ARM2_HCLK  Clock */
static struct clk clk_arm2_hclk = {
	.name	= "arm2_hclk",
	.parent	= &clk_arm2_ref,
	.clkdiv		= &arm2_axi_clkdiv,
	.clkdiv2	= &arm2_hclk_clkdiv,
	.calcrate	= p4a_clkdiv_calcrate,
};

static struct clk clk_hclk_div_clk_1 = {
	.name	= "hclk_div_clk_1",
	.parent	= &clk_axi_ref,
	.clkdiv	= &hclk_clkdiv,
	.calcrate	= p4a_clkdiv_calcrate,
};

static struct clk clk_hclk = {
	.name	= "hclk",
	.parent	= &clk_hclk_div_clk_1,
	.calcrate	= p4a_followparent_calcrate,
};


static struct clk clk_usbphy_clk = {	/* actually no exists */
	.name	= "usb1_phy",
};

static struct clk clk_usb_hclk = {	/* ULPI USB */
	.name	= "usb_hclk",
	.parent	= &clk_hclk_div_clk_1,
	.enable	= p4a_set_clken_and_softreset,
	.disable = p4a_clear_clken_and_softreset,
	.clken_reg	= GBL_CFG_BUS_CLK_REG,
	.clken_bit	= (0x1 << 14),
	.modrst_reg	= GBL_CFG_SOFTRST_REG,
	.modrst_bit = (0x1 << 4),
};

static struct clk clk_usb2phy_clk = {
	.name	= "usb2_phy",
	.enable	= p4a_set_clken_and_softreset,
	.disable = p4a_clear_clken_and_softreset,
	.clken_reg	= PMU_CLKRST1_REG,
	.clken_bit	= (0x1 << 1),
};

static struct clk clk_usb2_hclk = {		/* UTMI USB */
	.name	= "usb2_hclk",
	.parent	= &clk_hclk_div_clk_1,
	.enable	= p4a_set_clken_and_softreset,
	.disable = p4a_clear_clken_and_softreset,
	.clken_reg	= GBL_CFG_BUS_CLK_REG,
	.clken_bit	= (0x1 << 9),
	.modrst_reg	= GBL_CFG_SOFTRST_REG,
	.modrst_bit = (0x1 << 19),
};

static struct clk clk_sdiom1_hclk = {
	.name	= "sdiom1_hclk",
	.parent	= &clk_hclk_div_clk_1,
	.calcrate	= p4a_followparent_calcrate,
};

static struct clk clk_sdiom2_hclk = {
	.name	= "sdiom2_hclk",
	.parent	= &clk_hclk_div_clk_1,
	.calcrate	= p4a_followparent_calcrate,
};

static struct clk clk_sdiom1 = {
	.name	= "sdiom1_clk",
	.parent	= &clk_sdiom_ref,
	.calcrate	= p4a_followparent_calcrate,
	.enable = p4a_set_clken_and_softreset,
	.disable = p4a_clear_clken_and_softreset,
	.clken_reg = GBL_CFG_BUS_CLK_REG, 
	.clken_bit	= (0x3 << 29),
	.modrst_reg	= GBL_CFG_SOFTRST_REG,
	.modrst_bit = (0x1 << 8),
};

static struct clk clk_sdiom2 = {
	.name	= "sdiom2_clk",
	.parent	= &clk_sdiom_ref,
	.calcrate	= p4a_followparent_calcrate,
	.enable = p4a_set_clken_and_softreset,
	.disable = p4a_clear_clken_and_softreset,
	.clken_reg = GBL_CFG_PERI2_CLK_REG, 
	.clken_bit	= (0x3 << 5),
	.modrst_reg	= GBL_ARM_RST_REG,
	.modrst_bit = (0x1 << 19),
};

static struct clk clk_spim1_hclk = {
	.name	= "spim1_hclk",
	.parent	= &clk_hclk_div_clk_1,
	.enable	= p4a_set_clken_and_softreset,
	.disable = p4a_clear_clken_and_softreset,
	.clken_reg	= GBL_CFG_BUS_CLK_REG,
	.clken_bit	= (0x1 << 18),
};

static struct clk clk_spim2_hclk = {
	.name	= "spim2_hclk",
	.parent	= &clk_hclk_div_clk_1,
	.enable	= p4a_set_clken_and_softreset,
	.disable = p4a_clear_clken_and_softreset,
	.clken_reg	= GBL_CFG_BUS_CLK_REG,
	.clken_bit	= (0x1 << 19),
};

static struct clk clk_spim1 = {
	.name	= "spim1_clk",
	.parent	= &clk_peri_ref,
	.enable	= p4a_set_clken_and_softreset,
	.disable = p4a_clear_clken_and_softreset,
	.clken_reg	= GBL_CFG_PERI_CLK_REG,
	.clken_bit	= (0x1 << 23),
	.clkdiv	= &spim_clkdiv,
	.calcrate	= p4a_clkdiv_calcrate,
};

static struct clk clk_spim2 = {
	.name	= "spim2_clk",
	.parent	= &clk_peri_ref,
	.enable	= p4a_set_clken_and_softreset,
	.disable = p4a_clear_clken_and_softreset,
	.clken_reg	= GBL_CFG_PERI2_CLK_REG,
	.clken_bit	= (0x1 << 13),
	.clkdiv	= &spim2_clkdiv,
	.calcrate	= p4a_clkdiv_calcrate,
};

static struct clk clk_eth_hclk = {
	.name	= "eth_hclk",
	.parent	= &clk_hclk_div_clk_1,
	.enable	= p4a_set_clken_and_softreset,
	.disable = p4a_clear_clken_and_softreset,
	.clken_reg	= GBL_CFG_BUS_CLK_REG,
	.clken_bit	= (0x1 << 15),
	.modrst_reg	= GBL_CFG_SOFTRST_REG,
	.modrst_bit = (0x1 << 5),
};

static struct clk clk_uart = {
	.name	= "UART",
	.parent	= &clk_peri_ref,
	.enable	= p4a_set_clken_and_softreset,
	.disable = p4a_clear_clken_and_softreset,
	.clken_reg	= GBL_CFG_PERI_CLK_REG,
	.clken_bit	= (0x1 << 7),
	.clkdiv	= &timer_clkdiv,		// uart dividor same as timer
	.calcrate	= p4a_clkdiv_calcrate,
};

static struct clk clk_uart1 = {
	.name	= "UART1",
	.parent	= &clk_uart,
	.calcrate	= p4a_followparent_calcrate,
};

static struct clk clk_uart2 = {
	.name	= "UART2",
	.parent	= &clk_uart,
	.calcrate	= p4a_followparent_calcrate,
};

static struct clk clk_uart4w = {
	.name	= "UART4W",
	.parent	= &clk_hclk,
	.calcrate	= p4a_followparent_calcrate,
	.enable	= p4a_set_clken_and_softreset,
	.disable = p4a_clear_clken_and_softreset,
	.clken_reg	= GBL_CFG_PERI_CLK_REG,
	.clken_bit	= (0x1 << 5),
	.modrst_reg	= GBL_CFG_SOFTRST_REG,
	.modrst_bit = (0x1 << 26),
};

static struct clk clk_i2c = {
	.name	= "I2C",
	.parent	= &clk_peri_ref,
	.clkdiv	= &timer_clkdiv,		// i2c dividor same as timer
	.calcrate	= p4a_clkdiv_calcrate,
};

static struct clk clk_i2c1 = {
	.name	= "I2C Master1",
	.parent	= &clk_i2c,
	.calcrate	= p4a_followparent_calcrate,
};

static struct clk clk_i2c2 = {
	.name	= "I2C Master2",
	.parent	= &clk_i2c,
	.calcrate	= p4a_followparent_calcrate,
};

static struct clk clk_timer = {
	.name	= "timer1and2",
	.parent	= &clk_peri_ref,
	.enable	= p4a_set_clken_and_softreset,
	.disable = p4a_clear_clken_and_softreset,
	.clken_reg	= GBL_CFG_PERI_CLK_REG,
	.clken_bit	= (0x1 << 15),
	.clkdiv	= &timer_clkdiv,
	.calcrate	= p4a_clkdiv_calcrate,
};

static struct clk clk_p4timer = {
	.name	= "p4timer_clk",
#ifdef CONFIG_P4A_CPU2
	.parent	= &clk_arm2_hclk,
#elif defined(CONFIG_P4A_CPU1)
	.parent	= &clk_arm_hclk,
#endif
	.calcrate	= p4a_followparent_calcrate,
};

static struct clk clk_wdt= {
	.name	= "watchdog",
	.parent	= &clk_peri_ref,
	.calcrate	= p4a_followparent_calcrate,
};

static struct clk clk_mbox = {
	.name	= "mailbox",
	.enable	= p4a_set_clken_and_softreset,
	.disable = p4a_clear_clken_and_softreset,
	.clken_reg	= GBL_CFG_PERI_CLK_REG,
	.clken_bit	= (0x1 << 6),
	.modrst_reg	= GBL_CFG_SOFTRST_REG,
	.modrst_bit = (0x1 << 7),
};

static struct clk clk_nand = {
	.name	= "nand_hclk",
	.parent	= &clk_hclk_div_clk_1,
	.enable = p4a_set_clken_and_softreset,
	.disable = p4a_clear_clken_and_softreset,
	.clken_reg	= GBL_CFG_BUS_CLK_REG,
	.clken_bit	= (0x1 << 16),
	.modrst_reg	= GBL_CFG_SOFTRST_REG,
	.modrst_bit = (0x1 << 10),
};

static struct clk_lookup p4a_clks[] = {
	INIT_CLKREG(&clk_timer, NULL, "TIMER_CLK"),
	INIT_CLKREG(&clk_p4timer, NULL, "P4TIMER_CLK"),
	INIT_CLKREG(&clk_wdt, "p4a-wdt", "WDT_CLK"),
	INIT_CLKREG(&clk_uart1, "p4a-uart.0", NULL),
	INIT_CLKREG(&clk_uart2, "p4a-uart.1", NULL),
	INIT_CLKREG(&clk_uart4w, "p4a-uart4w", NULL),
	INIT_CLKREG(&clk_mbox, "p4a-mailbox", "MAILBOX_CLK"),
	INIT_CLKREG(&clk_i2c1, "p4a-i2c.0", NULL),
	INIT_CLKREG(&clk_i2c2, "p4a-i2c.1", NULL),
	INIT_CLKREG(&clk_spim1, "p4a-spi.0", "SPI_CLK"),
	INIT_CLKREG(&clk_spim2, "p4a-spi.1", "SPI_CLK"),
	INIT_CLKREG(&clk_spim1_hclk, "p4a-spi.0", "SPI_HCLK"),
	INIT_CLKREG(&clk_spim2_hclk, "p4a-spi.1","SPI_HCLK"),
	INIT_CLKREG(&clk_eth_hclk, "p4a-ether", NULL),
	INIT_CLKREG(&clk_nand, "p4a-nand", NULL),
	INIT_CLKREG(&clk_sdiom1, "p4a-sdhci.0", NULL),
	INIT_CLKREG(&clk_sdiom2, "p4a-sdhci.1", NULL),
	INIT_CLKREG(&clk_usb_hclk, "musb_hdrc.0", "USB_CLK"),
	INIT_CLKREG(&clk_usbphy_clk, "musb_hdrc.0", "USB_PHY_CLK"),
	INIT_CLKREG(&clk_usb2_hclk, "musb_hdrc.1", "USB_CLK"),
	INIT_CLKREG(&clk_usb2phy_clk, "musb_hdrc.1", "USB_PHY_CLK"),

};
#endif

void __init p4a_clk_init(void)
{

	clks_register(p4a_clks, ARRAY_SIZE(p4a_clks));

}

