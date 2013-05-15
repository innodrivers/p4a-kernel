/*
 *  linux/drivers/serial/p4a_serial_hs.c
 *
 *	Driver for innofidei P4A 4-Wire UART port
 *
 *  Author:	jimmy.li <lizhengming@innofidei.com>
 *  Copyright:	(C) 2010 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

/*#define DEBUG */

#if defined(CONFIG_SERIAL_P4A_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ		// note : uart_handle_sysrq_char() will use this macro
#endif

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/serial_reg.h>
#include <linux/circ_buf.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/clk.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <mach/hardware.h>
#include <mach/irqs.h>

#define P4A_UART4W
#include <mach/p4a_serial.h>

#define DRV_NAME		"p4a-uart4w"


struct uart4w_p4a_port {
	struct uart_port		port;
	struct platform_device	*pdev;

	struct resource *memres;
	char		name[16];
	struct clk *clk;
};

static struct uart4w_p4a_port*	p4a_ups[P4A_UART4W_PORTS];

/*---------------------------------------------------*/

static inline unsigned int serial_in(struct uart_port* port, int offset)
{
	return readl(port->membase + offset);
}

static inline void serial_out(struct uart_port *port, int offset, int value)
{
	writel(value, port->membase + offset);
}

static inline int txfifo_level(struct uart_port *port)
{
	return (serial_in(port, USR) & USR_TFLEVEL_MASK) >> USR_TFLEVEL_SHIFT;
}

static inline int rxfifo_level(struct uart_port *port)
{
	return (serial_in(port, USR) & USR_RFLEVEL_MASK) >> USR_RFLEVEL_SHIFT;
}

static inline int txfifo_full(struct uart_port *port)
{
	return (txfifo_level(port) >= P4A_UART4W_FIFO_SIZE);
}

static inline int txfifo_empty(struct uart_port *port)
{
	return (txfifo_level(port) == 0);
}

static inline int txfifo_space(struct uart_port *port)
{
	return P4A_UART4W_FIFO_SIZE - txfifo_level(port);
}

static inline int rxfifo_full(struct uart_port *port)
{
	return (rxfifo_level(port) >= P4A_UART4W_FIFO_SIZE);
}

static inline int rxfifo_empty(struct uart_port *port)
{
	return (rxfifo_level(port) == 0);
}

static inline void reset_fifos(struct uart_port* port)
{
	serial_out(port, UCR, (UCR_TXFIFO_RESET | UCR_RXFIFO_RESET));
}

/*---------------------------------------------------*/

/*
 * Return TIOCSER_TEMT when transmitter FIFO is empty, otherwise return 0.
 */
static unsigned int serial_p4a_tx_empty(struct uart_port *port)
{
	return txfifo_level(port) ? 0 : TIOCSER_TEMT;
}

/*
 * Set state of the modem control output lines
 */
static void serial_p4a_set_mctrl(struct uart_port *port, unsigned int mctrl)
{

}

/*
 * Get state of the modem control input lines
 */
static unsigned int serial_p4a_get_mctrl(struct uart_port *port)
{
	return 0;
}


/*
 * Stop transmitting.
 */
static void serial_p4a_stop_tx(struct uart_port *port)
{	
	unsigned long uier;


	uier = serial_in(port, UIER);

	if ((uier & UIER_TX)) {
		uier &= ~UIER_TX;
		serial_out(port, UIER, uier);
	}
}

/*
 * Start transmitting.
 */
static void serial_p4a_start_tx(struct uart_port *port)
{
	unsigned long uier;

	uier = serial_in(port, UIER);

	if (!(uier & UIER_TX)) {
		uier |= UIER_TX;
		serial_out(port, UIER, uier);
	}
}

/*
 * Stop receiving - port is in process of being closed.
 */
static void serial_p4a_stop_rx(struct uart_port *port)
{
	unsigned long uier;

	uier = serial_in(port, UIER);
	uier &= ~(UIER_RX | UIER_RX_PAR_ERR | UIER_RX_OVERFLOW);
	serial_out(port, UIER, uier);
}

/*
 * Enable modem status interrupts
 */
static void serial_p4a_enable_ms(struct uart_port *port)
{

}

/*
 * Control the transmission of a break signal
 */
static void serial_p4a_break_ctl(struct uart_port *port, int break_state)
{
	struct uart4w_p4a_port *up = (struct uart4w_p4a_port *)port;
	unsigned long flags;

	spin_lock_irqsave(&up->port.lock, flags);
	if (break_state == -1){
		//TODO	
		/* start break */
	}else{
		//TODO
		/* stop break */
	}
	spin_unlock_irqrestore(&up->port.lock, flags);
}

/*
 * Characters received (called from interrupt handler)
 */
static inline void handle_rx(struct uart4w_p4a_port *up, unsigned int isr)
{
	struct uart_port* port = &up->port;
	struct tty_struct *tty = port->state->port.tty;
	unsigned int ch;
	int maxcount = 256;

	if (isr & UISR_RX_OVERFLOW)
		tty_insert_flip_char(tty, 0, TTY_OVERRUN);

	while( !rxfifo_empty(port) ) {
		ch = serial_in(port, URXR);
		port->icount.rx++;

		if(uart_handle_sysrq_char(port, ch))
			goto ignore_char;

		tty_insert_flip_char(tty, ch, TTY_NORMAL);
	
ignore_char:
		if (maxcount--)
			break;
	}

	tty_flip_buffer_push(tty);
}

/*
 * Transmit characters (called from interrupt handler)
 */
static void handle_tx(struct uart4w_p4a_port *up, unsigned int isr)
{
	struct  uart_port* port = &up->port;
	struct circ_buf* xmit = &port->state->xmit;
	int count;

	if (port->x_char) {	// XON/XOFF character
		serial_out(port, UTXR, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return ;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		serial_p4a_stop_tx(port);
		return ;
	}

	count = txfifo_space(port); 
	do {
		serial_out(port, UTXR, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE -1);
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while( --count > 0 );

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		serial_p4a_stop_tx(port);
}

/*
 * This handles the interrupt from one port.
 */
static irqreturn_t serial_p4a_irq(int irq, void *dev_id)
{
	struct uart4w_p4a_port *up = dev_id;
	struct uart_port* port = &up->port;
	unsigned int isr;

	isr = serial_in(port, UISR);
	serial_out(port, UICR, isr);	//clear interrupt

	if (isr & (UISR_RX | UISR_RX_OVERFLOW)) {
		handle_rx(up, isr);

	} else if (isr & (UISR_TX | UISR_TX_OVERFLOW)) {
		handle_tx(up, isr);

	}

	return IRQ_HANDLED;
}

/*
 * Perform initialization and enable port for reception
 */
static int serial_p4a_startup(struct uart_port *port)
{
	struct uart4w_p4a_port *up = (struct uart4w_p4a_port *)port;
	int retval;
	
	dev_dbg(port->dev, "serial_p4a_startup (%d)\n", port->line);

	// disable interrupts
	serial_out(port, UIER, 0);
	serial_out(port, UICR, 0xffffffff);

	/*
	 * Allocate the IRQ
	 */
	retval = request_irq(up->port.irq, serial_p4a_irq, up->port.irqflags, up->name, up);
	if (retval){
		dev_err(port->dev, "serial_p4a: serial_p4a_startup - Can't get irq (%d)\n", up->port.irq);
		return retval;
	}


	//Clear the FIFO buffers
	reset_fifos(port);
	udelay(5);

	/* Now, initialize the UART */
	//serial_out(port, URTSCR, URTSCR_DEASSERT_THRESHOLD(32) | URTSCR_REASSERT_THRESHOLD(16));	//RTS threshold setting

	serial_out(port, UCR, UCR_RX_TL(0x01) | UCR_TX_TL(P4A_UART4W_FIFO_SIZE >> 1)|	\
							UCR_NBSTOP_1 | \
							UCR_UART_EN);

	/* Enable the UART interrupt */
	serial_out(port, UIER, (serial_in(port, UIER) | UIER_RX | UIER_RX_PAR_ERR | UIER_RX_OVERFLOW));

	return 0;
}

/*
 * Disable the port
 */
static void serial_p4a_shutdown(struct uart_port *port)
{
	struct uart4w_p4a_port *up = (struct uart4w_p4a_port *)port;

	dev_dbg(port->dev, "serial_p4a_shutdown (%d)\n", port->line);
	
	/* disable all interrupts */
	serial_out(port, UIER, 0);
	
	// flush FIFO
	reset_fifos(port);	
	udelay(5);
	
	// clear all interrupt status
	serial_out(port, UICR, 0xffffffff);

	// disable the port
	serial_out(port, UCR, 0);


	// Free the interrupt
	free_irq(port->irq, up);
}


/*
 * Change the port parameters
 */
static void serial_p4a_set_termios(struct uart_port *port, struct ktermios *termios, struct ktermios *old)
{
	struct uart4w_p4a_port *up = (struct uart4w_p4a_port *)port;
	unsigned int cval = 0;
	unsigned long flags;
	unsigned int baud;

	/* data width */
	switch (termios->c_cflag & CSIZE) {
	case CS5:
	case CS6:
	case CS7:
		dev_warn(port->dev, "p4a serial port only support 8bit data\n");
	case CS8:
	default:
		break;
	}

	/* stop bits */
	if (termios->c_cflag & CSTOPB)	//send 2bit stop
		cval |= UCR_NBSTOP_2;
	
	/* parity */
	if (termios->c_cflag & PARENB) {
		cval |= (UCR_TXPAR_EN | UCR_RXPAR_EN); 

		if (termios->c_cflag & CMSPAR) {            /* Mark or Space parity */
			if (termios->c_cflag & PARODD)
				cval |= UCR_PAR_MARK;
			else
				cval |= UCR_PAR_SPACE;
		} else if (termios->c_cflag & PARODD) {
			cval |= UCR_PAR_ODD;
		} else {
			cval |= UCR_PAR_EVEN;
		}
	}

	/* hardware flow control */
	if (termios->c_cflag & CRTSCTS) {
		cval |= (UCR_CTS_EN | UCR_RTS_EN | UCR_CTS_POL | UCR_RTS_POL);	
	}

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	port->uartclk = (unsigned int)clk_get_rate(up->clk);
	baud = uart_get_baud_rate(port, termios, old, 0, P4A_UART4W_MAXIMUM_BAUD(port->uartclk));

	/*
	 * Ok, we're now changing the port state.  Do it with
	 * interrupts disabled.
	 */
	spin_lock_irqsave(&up->port.lock, flags);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	up->port.read_status_mask = 0;
	up->port.ignore_status_mask = 0xffffffff;

	serial_out(port, UCR, 0);
	serial_out(port, UMR, UMR_VALUE(port->uartclk, baud));
	serial_out(port, UCR, cval | UCR_UART_EN | UCR_RX_TL(0x1)| UCR_TX_TL(P4A_UART4W_FIFO_SIZE >> 1) );
	
	spin_unlock_irqrestore(&up->port.lock, flags);
}

/*
 * Power / Clock management.
 */
static void serial_p4a_pm(struct uart_port *port, unsigned int state, unsigned int oldstate)
{
	struct uart4w_p4a_port *up = (struct uart4w_p4a_port*)port;

	dev_dbg(port->dev, "serial_p4a_pm : D%d (old D%d)\n", state, oldstate);

	switch (state) {
	case 0:
		/*
		 * Enable the peripheral clock for this serial port.
		 * This is called on uart_open() or a resume event.
		 */
		clk_enable(up->clk);
		break;
	case 3:
		/*
		 * Disable the peripheral clock for this serial port.
		 * This is called on uart_close() or a suspend event.
		 */
		clk_disable(up->clk);
		break;
	default:
		dev_err(port->dev, "p4a-serial: unknown pm %d\n", state);
	}
}

/*
 * Return string describing the specified port
 */
static const char *serial_p4a_type(struct uart_port *port)
{
	struct uart4w_p4a_port* up = (struct uart4w_p4a_port *)port;

	return up->name;
}

/*
 * Release the memory region(s) being used by 'port'.
 */
static void serial_p4a_release_port(struct uart_port *port)
{
	struct uart4w_p4a_port *up = (struct uart4w_p4a_port*)port;
	int size = resource_size(up->memres);

	dev_dbg(port->dev, "serial_p4a_release_port\n");

	release_mem_region(port->mapbase, size);

	if (port->flags & UPF_IOREMAP) {
		iounmap(port->membase);
		port->membase = NULL;
	}
}

/*
 * Request the memory region(s) being used by 'port'.
 */
static int serial_p4a_request_port(struct uart_port *port)
{
	struct uart4w_p4a_port *up = (struct uart4w_p4a_port*)port;
	int size = resource_size(up->memres);

	dev_dbg(port->dev, "serial_p4a_request_port\n");
	
	if (!request_mem_region(port->mapbase, size, "p4a_serial"))
		return -EBUSY;
	
	if (port->flags & UPF_IOREMAP) {
		port->membase = ioremap(port->mapbase, size);
		if (port->membase == NULL) {
			release_mem_region(port->mapbase, size);
			return -ENOMEM;
		}
	}

	return 0;
}

/*
 * Configure/autoconfigure the port.
 */
static void serial_p4a_config_port(struct uart_port *port, int flags)
{
	dev_dbg(port->dev, "serial_p4a_config_port\n");

	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_P4A;
		serial_p4a_request_port(port);
	}
}

/*
 * Verify the new serial_struct (for TIOCSSERIAL).
 */
static int serial_p4a_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	/* we don't want the core code to modify any port params */
	return -EINVAL;
}


struct uart_ops serial_p4a_pops = {
	.tx_empty	= serial_p4a_tx_empty,
	.set_mctrl	= serial_p4a_set_mctrl,
	.get_mctrl	= serial_p4a_get_mctrl,
	.stop_tx	= serial_p4a_stop_tx,
	.start_tx	= serial_p4a_start_tx,
	.stop_rx	= serial_p4a_stop_rx,
	.enable_ms	= serial_p4a_enable_ms,
	.break_ctl	= serial_p4a_break_ctl,
	.startup	= serial_p4a_startup,
	.shutdown	= serial_p4a_shutdown,
	.set_termios	= serial_p4a_set_termios,
	.pm				= serial_p4a_pm,
	.type			= serial_p4a_type,
	.release_port	= serial_p4a_release_port,
	.request_port	= serial_p4a_request_port,
	.config_port	= serial_p4a_config_port,
	.verify_port	= serial_p4a_verify_port,
};


#ifdef CONFIG_SERIAL_P4A_CONSOLE

static inline void wait_for_xmitr(struct uart4w_p4a_port *up)
{
	int timeout = 10000;

	// wait up to 10ms for characters to be sent
	do {
		if (--timeout == 0)
			break;

		udelay(1);
	} while (!txfifo_empty(&up->port));
}

static void serial_p4a_console_putchar(struct uart_port *port, int ch)
{
	int timeout = 100;
	
	while (txfifo_full(port) && --timeout) {
		udelay(1);
	}

	if(timeout)
		serial_out(port, UTXR, ch);
}

/*
 * Print a string to the serial port trying not to disturb
 * any possible real use of the port...
 *
 *	The console_lock must be held when we get here.
 */
static void p4a_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart4w_p4a_port *up = p4a_ups[co->index];
	unsigned long uier;

	//unsigned long flags;

	//spin_lock_irqsave(&up->port.lock, flags);
	// disable interrupt
	uier = serial_in(&up->port, UIER);
	serial_out(&up->port, UIER, 0);

	uart_console_write(&up->port, s, count, serial_p4a_console_putchar);

	// wait for transmitter to become empty
	wait_for_xmitr(up);
	serial_out(&up->port, UIER, uier);
	//spin_unlock_irqrestore(&up->port.lock, flags);
}

static int __init p4a_console_setup(struct console *co, char *options)
{
	struct uart4w_p4a_port *up;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index == -1 || co->index >= P4A_UART4W_PORTS)
		co->index = 0;
	
	up = p4a_ups[co->index];
	if (unlikely(!up))
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&up->port, co, baud, parity, bits, flow);
}

static struct uart_driver uart4w_p4a_reg;

static struct console serial_p4a_console = {
	.name		= "ttyHS",
	.write		= p4a_console_write,
	.device		= uart_console_device,
	.setup		= p4a_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &uart4w_p4a_reg,
};

#define P4A_UART4W_CONSOLE		&serial_p4a_console
#else
#define P4A_UART4W_CONSOLE		NULL
#endif	// CONFIG_SERIAL_P4A_CONSOLE


#define UART4W_SERIAL_MAJOR	202		//TTY_MAJOR		//202
#define UART4W_SERIAL_MINOR	64

static struct uart_driver uart4w_p4a_reg = {
	.owner			= THIS_MODULE,
	.driver_name	= "4w-uart",
	.dev_name		= "ttyHS",
	.major			= UART4W_SERIAL_MAJOR,
	.minor			= UART4W_SERIAL_MINOR,
	.nr				= P4A_UART4W_PORTS,
	.cons			= P4A_UART4W_CONSOLE,
};

#ifdef CONFIG_PM
static int serial_p4a_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct uart4w_p4a_port *up = platform_get_drvdata(pdev);

	uart_suspend_port(&uart4w_p4a_reg, &up->port);

	return 0;
}

static int serial_p4a_resume(struct platform_device *pdev)
{
	struct uart4w_p4a_port *up = platform_get_drvdata(pdev);

	uart_resume_port(&uart4w_p4a_reg, &up->port);

	return 0;
}
#else
#define serial_p4a_suspend		NULL
#define serial_p4a_resume		NULL
#endif	// CONFIG_PM

static int serial_p4a_probe(struct platform_device *pdev)
{
	struct uart4w_p4a_port* up;
	struct resource *memres, *irqres;
	struct uart_p4a_platform_data *up_info;
	int line;
	int ret;

	up_info = pdev->dev.platform_data;
	if (up_info == NULL)
		return -EINVAL;

	line = (pdev->id >= 0) ? pdev->id : 0;
	if (unlikely(line >= P4A_UART4W_PORTS))
		return -ENXIO;

	memres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!memres)) {
		dev_err(&pdev->dev, "no mem resource?\n");
		return -ENODEV;
	}

	irqres = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (unlikely(!irqres)) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return -ENODEV;
	}

	up = kzalloc(sizeof(*up), GFP_KERNEL);
	if (unlikely(up == NULL)) {
		return -ENOMEM;
	}

	snprintf(up->name, sizeof(up->name), "UART4W%d", line);
	up->pdev = pdev;
	up->memres = memres;
	up->clk = clk_get(&pdev->dev, up_info->clk_name);
	if (unlikely(IS_ERR(up->clk))) {
		ret = PTR_ERR(up->clk);
		dev_err(&pdev->dev, "cannot get clk!\n");
		goto err_free;
	}

	up->port.dev = &pdev->dev;
	up->port.type = PORT_P4A;
	up->port.iotype = UPIO_MEM;
	up->port.fifosize = P4A_UART4W_FIFO_SIZE;
	up->port.ops = &serial_p4a_pops;
	up->port.line = line;
	up->port.flags = UPF_BOOT_AUTOCONF;
	up->port.irq = irqres->start;
	up->port.irqflags = 0;
	up->port.mapbase = memres->start;	// mapbase : physical address of the IO port

	if (up_info->membase) {
		// Already mapped
		up->port.membase = up_info->membase;
	} else {
		up->port.flags |= UPF_IOREMAP;
		up->port.membase = NULL;
	}

	p4a_ups[line] = up;

	platform_set_drvdata(pdev, up);
	
	ret = uart_add_one_port(&uart4w_p4a_reg, &up->port);
	if (ret)
		goto err_drvdata;
	
	return 0;

err_drvdata:
	platform_set_drvdata(pdev, NULL);

err_free:
	kfree(up);

	return ret;
}

static int __devexit serial_p4a_remove(struct platform_device *pdev)
{
	struct uart4w_p4a_port *up = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	if (up) {
		uart_remove_one_port(&uart4w_p4a_reg, &up->port);

		clk_put(up->clk);
		kfree(up);
	}
	
	return 0;
}

static struct platform_driver serial_p4a_driver = {
	.probe      = serial_p4a_probe,
	.remove		= __devexit_p(serial_p4a_remove),

	.suspend	= serial_p4a_suspend,
	.resume		= serial_p4a_resume,

	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init serial_p4a_init(void)
{
	int ret;
	
	pr_info("Serial: UART4W driver\n");

	ret = uart_register_driver(&uart4w_p4a_reg);
	if (ret) {
		pr_info("register uart driver failed!(%d)\n", ret);
		return ret;
	}

	ret = platform_driver_register(&serial_p4a_driver);
	if (ret)
		uart_unregister_driver(&uart4w_p4a_reg);

	return ret;
}

static void __exit serial_p4a_exit(void)
{
	platform_driver_unregister(&serial_p4a_driver);
	uart_unregister_driver(&uart4w_p4a_reg);
}

module_init(serial_p4a_init);
module_exit(serial_p4a_exit);

MODULE_AUTHOR("Jimmy.li <lizhengming@innofidei.com>");
MODULE_DESCRIPTION("P4A 4-Wires Serial Driver");
MODULE_LICENSE("GPL");

