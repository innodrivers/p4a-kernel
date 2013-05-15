#ifndef _P4A_REGS_H
#define _P4A_REGS_H

#ifdef __ASSEMBLY__
#define IOMEM(x)	x
#else
#define IOMEM(x)	((void __force __iomem *)(x))
#endif

/* virtual address 0xf0000000 - 0xf9ffffff, reserved for CPU1 memory */
#define P4A_CPU1MEM_BASE		0xf0000000
#define P4A_CPU1MEM_PHYS		0x42000000

/*
 * 	physcial				<--> virtual
 *
 *  0xe0000000 - 0xe1ffffff <--> 0xfa000000 - 0xfbffffff
 *  0xef000000 - 0xefffffff <--> 0xfc000000 - 0xfcffffff
 */
#define P4A_IO1_P2V(x)		(0xfa000000 + ((x) & 0x01ffffff))
#define P4A_IO1_V2P(x)		(0xe0000000 + ((x) & 0x01ffffff))

#define P4A_IO2_P2V(x)		(0xfc000000 + ((x) & 0x00ffffff))
#define P4A_IO2_V2P(x)		(0xef000000 + ((x) & 0x00ffffff))


/* Interrupt Controller */
#ifdef CONFIG_P4A_CPU2
#define P4A_AIC_PHYS				0xE0105000
#define P4A_AIC_BASE				IOMEM(P4A_IO1_P2V(P4A_AIC_PHYS))
#define P4A_AIC_SIZE				SZ_4K
#elif defined(CONFIG_P4A_CPU1)
#define P4A_AIC_PHYS				0xE0102000
#endif

/* Global */
#define P4A_GLOBAL_PHYS				0xE1200000
#define P4A_GLOBAL_BASE				IOMEM(P4A_IO1_P2V(P4A_GLOBAL_PHYS))
#define P4A_GLOBAL_SIZE				SZ_4K

/* PMU */
#define P4A_PMU_PHYS				0xE1300000
#define P4A_PMU_BASE				IOMEM(P4A_IO1_P2V(P4A_PMU_PHYS))
#define P4A_PMU_SIZE				SZ_4K

/* Peripheral Common */
#define P4A_PERI_PHYS				0xE0103000
#define P4A_PERI_BASE				IOMEM(P4A_IO1_P2V(P4A_PERI_PHYS))
#define P4A_PERI_SIZE				SZ_4K

#define P4A_PERI_PREG(off)			(P4A_PERI_PHYS + (off))
#define P4A_PERI_VREG(off)			(P4A_PERI_BASE + (off))

#define P4A_TIMER1_PHYS				P4A_PERI_PREG(0x0)
#define P4A_TIMER1_BASE				P4A_PERI_VREG(0x0)

#define P4A_TIMER2_PHYS				P4A_PERI_PREG(0x10)
#define P4A_TIMER2_BASE				P4A_PERI_VREG(0x10)

#define P4A_WDT_PHYS				P4A_PERI_PREG(0x20)
#define P4A_WDT_BASE				P4A_PERI_VREG(0x20)

#define P4A_UART1_PHYS				P4A_PERI_PREG(0x40)	// 2-Wire UART
#define P4A_UART1_BASE				P4A_PERI_VREG(0x40)

#define P4A_UART2_PHYS				P4A_PERI_PREG(0x54)	// 2-Wire UART
#define P4A_UART2_BASE				P4A_PERI_VREG(0x54)

#define P4A_I2C1_PHYS				P4A_PERI_PREG(0xc0)
#define P4A_I2C2_PHYS				P4A_PERI_PREG(0xdc)

/* GPIO 1 ~ 6 */
#define P4A_GPIO1_PHYS				P4A_PERI_PREG(0x100)
#define P4A_GPIO1_BASE				P4A_PERI_VREG(0x100)

#define P4A_GPIO2_PHYS				P4A_PERI_PREG(0x154)
#define P4A_GPIO2_BASE				P4A_PERI_VREG(0x154)

#define P4A_GPIO3_PHYS				P4A_PERI_PREG(0x180)
#define P4A_GPIO3_BASE				P4A_PERI_VREG(0x180)

#define P4A_GPIO4_PHYS				P4A_PERI_PREG(0x1c0)
#define P4A_GPIO4_BASE				P4A_PERI_VREG(0x1c0)

#define P4A_GPIO5_PHYS				P4A_PERI_PREG(0x2c0)
#define P4A_GPIO5_BASE				P4A_PERI_VREG(0x2c0)

#define P4A_GPIO6_PHYS				P4A_PERI_PREG(0x300)
#define P4A_GPIO6_BASE				P4A_PERI_VREG(0x300)

/* 4-Wire UART*/
#define P4A_UART4W_PHYS				0xE0F00000
#define P4A_UART4W_BASE				IOMEM(P4A_IO1_P2V(P4A_UART4W_PHYS))
#define P4A_UART4W_SIZE				SZ_4K

/* P4 Timer */
#ifdef CONFIG_P4A_CPU2
#define P4A_P4TIMER_PHYS			0xEF002000
#define P4A_P4TIMER_BASE			IOMEM(P4A_IO2_P2V(P4A_P4TIMER_PHYS))
#define P4A_P4TIMER_SIZE			SZ_4K

#elif defined(CONFIG_P4A_CPU1)
#define P4A_P4TIMER_PHYS			0xEF001000
#endif

/* Mailbox */
#define P4A_MAILBOX_PHYS			0xE0700000
#define P4A_MAILBOX_SIZE			SZ_4K

/* Ethernet */
#define P4A_ETHER_PHYS				0xE0300000
#define P4A_ETHER_SIZE				SZ_4K

/* SDIO Host */
#define P4A_SDIOM1_PHYS				0xE0A00000
#define P4A_SDIOM1_SIZE				SZ_4K

#define P4A_SDIOM2_PHYS				0xE0E00000
#define P4A_SDIOM2_SIZE				SZ_4K

/* NAND Controller */
#define P4A_NANDC_PHYS				0xE0800000
#define P4A_NANDC_SIZE				SZ_4K

/* USB Controller */
#define P4A_USB_PHYS				0xE0200000		// with ULPI PHY
#define P4A_USB_SIZE				SZ_4K

#define P4A_USB2_PHYS				0xE1100000		//with UTMI PHY
#define P4A_USB2_SIZE				SZ_4K

//////////////////////////////////////////////////////////////////////////////////////

/* Global Registers */
#define GBL_REG(off)		(P4A_GLOBAL_BASE + (off))

#define GBL_CFG_ARM_CLK_REG		GBL_REG(0x0c)
#define GBL_CFG_DSP_CLK_REG		GBL_REG(0x10)
#define GBL_CFG_BUS_CLK_REG		GBL_REG(0x14)
#define GBL_CFG_AB_CLK_REG		GBL_REG(0x18)
#define GBL_CFG_PERI_CLK_REG	GBL_REG(0x1c)
#define GBL_CFG_SOFTRST_REG		GBL_REG(0x20)
#define GBL_BOOT_SEL_REG		GBL_REG(0x24)
#define GBL_CFG_DSP_REG			GBL_REG(0x28)
#define GBL_GPR_0_REG			GBL_REG(0x2c)
#define GBL_GPR_1_REG			GBL_REG(0x30)
#define GBL_GPR_2_REG			GBL_REG(0x34)
#define GBL_GPR_3_REG			GBL_REG(0x38)
#define GBL_CTRL_DDR_REG		GBL_REG(0x3c)
#define GBL_CTRL_DBG_REG		GBL_REG(0x40)
#define GBL_ARM_RST_REG			GBL_REG(0x6c)
#define GBL_CTRL_TOP_REG		GBL_REG(0x70)
#define GBL_CFG_ARM2_CLK_REG	GBL_REG(0x74)
#define GBL_CFG_PERI2_CLK_REG	GBL_REG(0x78)
#define GBL_CFG_DSP2_CLK_REG	GBL_REG(0x7c)
#define GBL_SMID_OCR_REG		GBL_REG(0x100)
#define GBL_SMID_CTL_REG		GBL_REG(0x104)
#define GBL_A8_MAP_ADDR_REG		GBL_REG(0x108)
#define GBL_PERI_HTRANS_REG		GBL_REG(0x160)

/* PMU Registers */
#define PMU_REG(off)		(P4A_PMU_BASE + (off))

#define PMU_CTRL_REG			PMU_REG(0x00)

#define PMU_PLL1_CTRL_REG		PMU_REG(0x74)
#define PMU_PLL2_CTRL_REG		PMU_REG(0x78)
#define PMU_PLL3_CTRL_REG		PMU_REG(0xa4)

#define PMU_CLKRST1_REG			PMU_REG(0x250)
#define PMU_CLKRST2_REG			PMU_REG(0x254)
#define PMU_CLKRST3_REG			PMU_REG(0x258)
#define PMU_CLKRST4_REG			PMU_REG(0x25c)
#define PMU_CLKRST5_REG			PMU_REG(0x260)



/*----------------LowLevel Debug UART -------------------------*/
#define UMR_OFF		0x00
#define URXR_OFF	0x04
#define UTXR_OFF	0x08
#define USR_OFF		0x0c
#define UCR_OFF		0x10

#ifdef CONFIG_P4A_LL_DEBUG_UART4W
#define LL_DBG_UART_PHYS		P4A_UART4W_PHYS
#define LL_DBG_UART_BASE		P4A_UART4W_BASE

#define LL_DBG_UMR_VALUE(_clock_, _baudrate_)      ( ((_clock_) << 4)/(_baudrate_) - 256 )
#define LL_DBG_USR_TFLEVEL_SHIFT	(7)
#define LL_DBG_USR_TFLEVEL_MASK 	(0x7f<<LL_DBG_USR_TFLEVEL_SHIFT)
#define LL_DBG_UTX_FIFOSZ			(63)

#elif defined(CONFIG_P4A_LL_DEBUG_UART2)
#define LL_DBG_UART_PHYS		P4A_UART2_PHYS
#define LL_DBG_UART_BASE		P4A_UART2_BASE

#define	LL_DBG_UMR_VALUE(clk, baud)	(((clk>>4)/baud) - 1)
#define LL_DBG_USR_TFLEVEL_SHIFT	(6)
#define LL_DBG_USR_TFLEVEL_MASK 	(0x3f<<LL_DBG_USR_TFLEVEL_SHIFT)
#define LL_DBG_UTX_FIFOSZ			(0x3e)

#elif defined(CONFIG_P4A_LL_DEBUG_UART1)
#define LL_DBG_UART_PHYS		P4A_UART1_PHYS
#define LL_DBG_UART_BASE		P4A_UART1_BASE

#define	LL_DBG_UMR_VALUE(clk, baud)	(((clk>>4)/baud) - 1)
#define LL_DBG_USR_TFLEVEL_SHIFT	(6)
#define LL_DBG_USR_TFLEVEL_MASK 	(0x3f<<LL_DBG_USR_TFLEVEL_SHIFT)
#define LL_DBG_UTX_FIFOSZ			(0x3e)

#endif

#endif

