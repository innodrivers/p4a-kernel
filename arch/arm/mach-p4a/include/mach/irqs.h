/*
 * linux/arch/arm/mach-p4a/include/mach/irqs.h
 *
 *  Copyright (c) 2013 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H


#define AIC_IRQ(x)	(x)

#define IRQ_TIMER				AIC_IRQ(0)	/* timer */			//!!!NOTICE: don't use directly
#define IRQ_GPIO				AIC_IRQ(1)	/* GPIO */			//!!!NOTICE: don't use directly
#define ARM_INTERNAL			AIC_IRQ(2)	/* ARM internal */	//!!!NOTICE: don't use directly
#define IRQ_PERI_COMM			AIC_IRQ(3)	/* Peripheral communication */	//!!!NOTICE: don't use directly
#define IRQ_SFLASH		      	AIC_IRQ(4)
#define IRQ_MAILBOX_DSP2ARM     AIC_IRQ(5)
#define IRQ_ODMA	     		AIC_IRQ(6)
#define IRQ_NDMA		 		AIC_IRQ(7)	// new DMA interrupt
#define IRQ_USB_MCU     		AIC_IRQ(8)	// ULPI
#define IRQ_USB_DMA     		AIC_IRQ(9)
#define IRQ_ETHER	     		AIC_IRQ(10)

#define IRQ_A2ARM_INT0          AIC_IRQ(11)
#define IRQ_A2ARM_INT1	     	AIC_IRQ(12)
#define IRQ_A2ARM_INT2	     	AIC_IRQ(13)
#define IRQ_A2ARM_INT3	     	AIC_IRQ(14)

#define IRQ_PDSCH_INT			AIC_IRQ(13)
#define IRQ_PDCCH_INT			AIC_IRQ(14)

#define IRQ_FFT_ENGINE	     	AIC_IRQ(15)
#define IRQ_DDR_INT	     		AIC_IRQ(16)

#define IRQ_SDIO_SLAVE			AIC_IRQ(17)
#define IRQ_TIMER_P4            AIC_IRQ(18)  /* P4 timer */
#define IRQ_UART4W				AIC_IRQ(19)
#define IRQ_SPI_MASTER1			AIC_IRQ(20)
#define IRQ_SPI_SLAVE			AIC_IRQ(21)
#define IRQ_SDIO_MASTER1		AIC_IRQ(22)
#define IRQ_CIPHER				AIC_IRQ(23)
#define IRQ_SPI_MASTER2			AIC_IRQ(24)
#define IRQ_SDIO_MASTER2		AIC_IRQ(25)
#define IRQ_NAND				AIC_IRQ(26)
#define IRQ_PMU					AIC_IRQ(27)
#define IRQ_SIM_SLAVE			AIC_IRQ(28)
#define IRQ_SIM_MASTER			AIC_IRQ(29)
#define IRQ_USB2_MCU			AIC_IRQ(30)		//UTMI
#define IRQ_USB2_DMA			AIC_IRQ(31)

#define NR_AIC_IRQS		32



#define IRQ_TIMER_BASE		(AIC_IRQ(0) + NR_AIC_IRQS)

#define IRQ_TIMER1M0		(IRQ_TIMER_BASE + 0)
#define IRQ_TIMER1M1		(IRQ_TIMER_BASE + 1)
#define IRQ_TIMER2M0		(IRQ_TIMER_BASE + 2)
#define IRQ_TIMER2M1		(IRQ_TIMER_BASE + 3)
#define IRQ_WDT				(IRQ_TIMER_BASE + 4)

#define NR_IRQS_TIMER		5



#define IRQ_PERI(x)		( IRQ_TIMER_BASE + NR_IRQS_TIMER + (x))
#define IRQ_PERI_BASE		IRQ_PERI(0)

#define IRQ_PERI_UART1_RX		IRQ_PERI(0)
#define IRQ_PERI_UART1_TX		IRQ_PERI(1)
#define IRQ_PERI_UART2_RX		IRQ_PERI(2)
#define IRQ_PERI_UART2_TX		IRQ_PERI(3)
#define IRQ_PERI_EXT_INT1		IRQ_PERI(4)
#define IRQ_PERI_EXT_INT2		IRQ_PERI(5)
#define IRQ_PERI_I2C_MASTER1	IRQ_PERI(6)
#define IRQ_PERI_I2C_MASTER2	IRQ_PERI(7)
#define IRQ_PERI_RESERVED1		IRQ_PERI(8)
#define IRQ_PERI_RESERVED2		IRQ_PERI(9)
#define IRQ_PERI_SIM1_RX		IRQ_PERI(10)
#define IRQ_PERI_SIM1_TX		IRQ_PERI(11)
#define IRQ_PERI_SIM1_RX_RE		IRQ_PERI(12)
#define IRQ_PERI_SIM1_TX_RE		IRQ_PERI(13)
#define IRQ_PERI_SIM2_RX		IRQ_PERI(14)
#define IRQ_PERI_SIM2_TX		IRQ_PERI(15)
#define IRQ_PERI_SIM2_RX_RE		IRQ_PERI(16)
#define IRQ_PERI_SIM2_TX_RE		IRQ_PERI(17)
#define IRQ_PERI_WDT_BIDIR		IRQ_PERI(18)
#define IRQ_PERI_WDT_UNIDIR		IRQ_PERI(19)
#define IRQ_PERI_SIM2_RX_EMPTY	IRQ_PERI(20)
#define IRQ_PERI_SIM2_RX_FULL	IRQ_PERI(21)
#define IRQ_PERI_SIM2_TX_EMPTY	IRQ_PERI(22)
#define IRQ_PERI_SIM2_TX_FULL	IRQ_PERI(23)

#define NR_IRQS_PERI		24



/* GPIO IRQs */
#define IRQ_GPIO_BASE		(IRQ_PERI(0) + NR_IRQS_PERI)
#define NR_GPIO_IRQS		192

#define GPIO_TO_IRQ(x)		(IRQ_GPIO_BASE + (x))
#define IRQ_TO_GPIO(i)		((i) - IRQ_GPIO_BASE)



/* 
 * Figure out the MAX IRQ number
 */
#define NR_IRQS				(NR_AIC_IRQS + NR_IRQS_TIMER + NR_IRQS_PERI + NR_GPIO_IRQS)

#endif	//__ASM_ARCH_IRQS_H


