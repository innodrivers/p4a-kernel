/*
 * arch/arm/mach-p4a/include/mach/p4a_serial.h
 *
 * Copyright (C) 2013 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _P4A_SERIAL_H_
#define _P4A_SERIAL_H_

/*
 * Uart Registers
 */

#define UMR         (0x00)      /* Uart Match register */
#define URXR        (0x04)      /* Uart Receive Buffer register */
#define UTXR        (0x08)      /* Uart Transmit Buffer register */
#define USR         (0x0c)      /* Uart Status register */
#define UCR         (0x10)      /* Uart Control register */

/*------------------------------------------------------------*/
#ifdef P4A_UART2W
/* Uart Match Register (UMR) bit fields */
#define		UMR_VALUE(clk, baud)	(((clk>>4)/baud) - 1)

/* Uart Status Register (USR) bit fields */
#define 	USR_RX_TL_SHIFT			(0)
#define 	USR_RX_TL_MASK			(0x3f<<USR_RX_TL_SHIFT)
#define 	USR_TX_TL_SHIFT			(6)
#define 	USR_TX_TL_MASK			(0x3f<<USR_TX_TL_SHIFT)
#define		USR_RX_EVENT			(1<<12)
#define		USR_TX_EVENT			(1<<13)


/* Uart Control Register (UCR) bit fields */
#define 	UCR_RX_TL(x)		((x)<<0)	/* RX FIFO interrupt trigger throld */
#define 	UCR_TX_TL(x)		((x)<<6)	/* TX FIFO interrupt trigger throld */
#define 	UCR_UART_EN			(1<<12)		/* Enable Uart Unit */
#define		UCR_TXPAR_EN        (1 << 13)   /* Tx Parity Check Enable */
#define		UCR_RXPAR_EN        (1 << 14)	/* Rx Parity Check Enable */
#define 	UCR_PAR_NONE		(0x0<<15)	/* parity none */
#define 	UCR_PAR_ODD			(0x3<<15)	/* parity odd */
#define 	UCR_PAR_EVEN		(0x7<<15)	/* parity even */
#define 	UCR_PAR_SPACE		(0xb<<15)	/* space is a 0-bit (or logic 0) */
#define 	UCR_PAR_MARK		(0xf<<15)	/* mark is a 1-bit (or logic 1) */
#define 	UCR_NBSTOP_1		(0<<17)		/* one stop bit */
#define 	UCR_NBSTOP_2		(1<<17)		/* two stop bit */
#define 	UCR_RESET_TF		(1<<18)		/* reset TX FIFO */
#define 	UCR_RESET_RF		(1<<19)		/* reset RX FIFO */

#define P4A_UART_MAXIMUM_BAUD(__clk__)      ((__clk__) >> 4)        // max baudrate, = (__clk__)/16
#define P4A_UART_FIFO_SIZE		0x3f
#define P4A_UART_PORTS		(2)

#endif

/*-----------------------------------------------------------*/
#ifdef P4A_UART4W

#define UISR        (0x14)      /* Uart Interrupt Status register */
#define UICR        (0x18)      /* Uart Interrupt Clear register */
#define UIER        (0x1c)      /* Uart Interrupt Enable register */

/* Uart Match Register (UMR) bit fields */
#define UMR_VALUE(_clock_, _baudrate_)      ( ((_clock_) << 4)/(_baudrate_) - 256 )


/* Uart Status Register (USR) bit fields */
#define USR_RFLEVEL_SHIFT       (0)
#define USR_RFLEVEL_MASK        (0x7f << USR_RFLEVEL_SHIFT)
#define USR_TFLEVEL_SHIFT       (7)
#define USR_TFLEVEL_MASK        (0x7f << USR_TFLEVEL_SHIFT)
#define USR_RXEVENT             (1 << 14)
#define USR_TXEVENT             (1 << 15)
#define USR_RTS                 (1 << 16)
#define USR_CTS                 (1 << 17)

/* Uart Control Register (UCR) bit fields */
#define UCR_RX_TL_MASK      (0x3F)
#define UCR_RX_TL(x)        ((x) << 0)      /* RX FIFO interrupt trigger throld level */
#define UCR_TX_TL_MASK      (0x3F << 6)
#define UCR_TX_TL(x)        ((x) << 6)      /* TX FIFO interrupt trigger throld level*/
#define UCR_UART_EN         (1 << 12)       /* Uart Enable */
#define UCR_TXPAR_EN        (1 << 13)       /* Tx Parity Check Enable */
#define UCR_RXPAR_EN        (1 << 14)       /* Rx Parity Check Enable */
#define UCR_PAR_SEL_MASK    (0x3 << 15)
#define UCR_PAR_ODD         (0x0 <<15)      /* parity odd */
#define UCR_PAR_EVEN        (0x1<<15)       /* parity even */
#define UCR_PAR_SPACE       (0x2<<15)       /* space is a 0-bit (or logic 0) */
#define UCR_PAR_MARK        (0x3<<15)       /* mark is a 1-bit (or logic 1) */
#define UCR_NBSTOP_SEL_MASK (1 << 17)
#define UCR_NBSTOP_1        (0 << 17)       /* one stop bit */
#define UCR_NBSTOP_2        (1 << 17)       /* two stop bit */
#define UCR_TXFIFO_RESET    (1 << 18)       /* reset TX FIFO */
#define UCR_RXFIFO_RESET    (1 << 19)       /* reset RX FIFO */
#define UCR_CTS_EN          (1 << 20)       /* CTS Flow Control Enable */
#define UCR_RTS_EN          (1 << 21)       /* RTS Flow Control Enable */
#define UCR_LOOPBACK_EN     (1 << 22)       /* loopback mode enable */
#define UCR_DMA_MODE        (1 << 23)
#define UCR_CTS_POL         (1 << 24)       /* CTS Polarity */
#define UCR_RTS_POL         (1 << 25)       /* RTS Polarity */

/* Uart Interrupt Status Register (UISR) bit fields */
#define UISR_RX             (1 << 0)
#define UISR_TX             (1 << 1)
#define UISR_RX_PAR_ERR     (1 << 2)
#define UISR_RX_OVERFLOW    (1 << 3)
#define UISR_TX_OVERFLOW    (1 << 4)

/* Uart Interrupt Clear Register (UICR) bit fields */
#define UICR_RX             (1 << 0)
#define UICR_TX             (1 << 1)
#define UICR_RX_PAR_ERR     (1 << 2)
#define UICR_RX_OVERFLOW    (1 << 3)
#define UICR_TX_OVERFLOW    (1 << 4)

/* Uart Interrupt Enable Register (UIER) bit fields */
#define UIER_RX             (1 << 0)
#define UIER_TX             (1 << 1)
#define UIER_RX_PAR_ERR     (1 << 2)
#define UIER_RX_OVERFLOW    (1 << 3)
#define UIER_TX_OVERFLOW    (1 << 4)


#define P4A_UART4W_PORTS          (1)
#define P4A_UART4W_FIFO_SIZE      (64)
#define P4A_UART4W_MAXIMUM_BAUD(__clk__)      ((__clk__) >> 4)        // max baudrate, = (__clk__)/16

#endif

/*--------------------------------------------------------------------------------*/
#ifndef __ASSEMBLY__

struct uart_p4a_platform_data {
	char			clk_name[16];
	void __iomem	*membase;		/* ioremap cookie or NULL */
};

#endif

#endif	//_P4A_SERIAL_H_
