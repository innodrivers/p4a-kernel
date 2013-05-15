/*
 *  linux/drivers/serial/p4a_serial.c
 *
 *  Copyright:	(C) 2008 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#if defined(CONFIG_SERIAL_P4A_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
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
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <mach/hardware.h>
#include <mach/p4a-regs.h>
#include <mach/irqs.h>

#define P4A_UART2W
#include <mach/p4a_serial.h>

#define DRV_NAME		"p4a-uart"


/*-----------------------------------------------------------*/
struct uart_p4a_port {
	struct uart_port        port;

	int rxirq_enabled;
	int txirq_enabled;

	struct resource *memres;
	struct clk	*clk;
	struct platform_device	*pdev;
	char		name[16];

};

static struct uart_p4a_port * p4a_ports[P4A_UART_PORTS];

/*-----------------------------------------------------------*/

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
	return (serial_in(port, USR) & USR_TX_TL_MASK) >> USR_TX_TL_SHIFT;
}

static inline int txfifo_full(struct uart_port *port)
{
	return (txfifo_level(port) >= P4A_UART_FIFO_SIZE);
}

static inline int txfifo_empty(struct uart_port *port)
{
	return (txfifo_level(port) == 0);
}

static inline int txfifo_space(struct uart_port *port)
{
	return P4A_UART_FIFO_SIZE - txfifo_level(port);
}

static inline int rxfifo_level(struct uart_port *port)
{
	return (serial_in(port, USR) & USR_RX_TL_MASK) >> USR_RX_TL_SHIFT;
}

static inline int rxfifo_full(struct uart_port *port)
{
	return (rxfifo_level(port) >= P4A_UART_FIFO_SIZE);
}

static inline int rxfifo_empty(struct uart_port *port)
{
	return (rxfifo_level(port) == 0);
}

/*--------------------------------------------------------------*/

/*
 * Return TIOCSER_TEMT when transmitter FIFO and Shift register is empty.
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


static inline void disable_uart_tx_irq(struct uart_p4a_port *up)
{
	if(up->txirq_enabled){
		disable_irq_nosync(up->port.irq + 1);

		up->txirq_enabled = 0;
	}
	
}

static inline void enable_uart_tx_irq(struct uart_p4a_port *up)
{
	if(!up->txirq_enabled){
		enable_irq(up->port.irq + 1);
		up->txirq_enabled = 1;
	}
}

static inline void disable_uart_rx_irq(struct uart_p4a_port *up)
{
	if (up->rxirq_enabled){
		disable_irq_nosync(up->port.irq);

		up->rxirq_enabled = 0;
	}
}

static inline void enable_uart_rx_irq(struct uart_p4a_port *up)
{
	if (!up->rxirq_enabled) {
		enable_irq(up->port.irq);

		up->rxirq_enabled = 1;
	}
}

/*
 * Stop transmitting.
 */
static void serial_p4a_stop_tx(struct uart_port *port)
{	
	struct uart_p4a_port *up = (struct uart_p4a_port *)port;

	disable_uart_tx_irq(up);
}

/*
 * Start transmitting.
 */
static void serial_p4a_start_tx(struct uart_port *port)
{
	struct uart_p4a_port *up = (struct uart_p4a_port *)port;

	enable_uart_tx_irq(up);
}

/*
 * Stop receiving - port is in process of being closed.
 */
static void serial_p4a_stop_rx(struct uart_port *port)
{
	struct uart_p4a_port *up = (struct uart_p4a_port *)port;

	disable_uart_rx_irq(up);
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
	struct uart_p4a_port *up = (struct uart_p4a_port *)port;
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
static inline void receive_chars(struct uart_p4a_port *up)
{
	struct uart_port* port = &up->port;
	struct tty_struct *tty = port->state->port.tty;
	unsigned int ch;
	
	while ( !rxfifo_empty(port) ) {
		ch = serial_in(port, URXR);
		port->icount.rx++;

		if(uart_handle_sysrq_char(port, ch))
			goto ignore_char;

		tty_insert_flip_char(tty, ch, TTY_NORMAL);
	
	ignore_char:
		break;
	}

	tty_flip_buffer_push(tty);
}

/*
 * Transmit characters (called from interrupt handler)
 */
static void transmit_chars(struct uart_p4a_port *up)
{
	struct  uart_port* port = &up->port;
	struct circ_buf* xmit = &port->state->xmit;
	unsigned int count;
	
	if (port->x_char) {
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
	while (count > 0) {
		serial_out(port, UTXR, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE -1);
		port->icount.tx++;

		if (uart_circ_empty(xmit))
			break;
		count--;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		serial_p4a_stop_tx(port);

}

/*
 * This handles the interrupt from one port.
 */
static irqreturn_t serial_rx_p4a_irq(int irq, void *dev_id)
{
	struct uart_p4a_port *up = dev_id;

	receive_chars(up);

	return IRQ_HANDLED;
}

static irqreturn_t serial_tx_p4a_irq(int irq, void *dev_id)
{
	struct uart_p4a_port *up = dev_id;
	
	transmit_chars(up);
	return IRQ_HANDLED;
}

/*
 * Perform initialization and enable port for reception
 */
static int serial_p4a_startup(struct uart_port *port)
{
	struct uart_p4a_port *up = (struct uart_p4a_port *)port;
	int retval;

	serial_out(port, UCR, 0);	// disable UART first.

	// reset FIFOs
	serial_out(port, UCR, (UCR_RESET_TF | UCR_RESET_RF));
	udelay(5);

	/*
	 * Allocate the IRQ
	 */
	retval = request_irq(up->port.irq, serial_rx_p4a_irq, 0, up->name, up);
	if (retval){
		dev_err(&up->pdev->dev, "Can't get irq (%d)\n", up->port.irq);
		return retval;
	}

	retval = request_irq((up->port.irq + 1), serial_tx_p4a_irq, 0, up->name, up);
	if (retval){
		dev_err(&up->pdev->dev, "Can't get irq (%d)\n", (up->port.irq + 1));
		free_irq(up->port.irq, up);
		return retval;
	}
	up->rxirq_enabled = up->txirq_enabled = 1;

	disable_uart_tx_irq(up);

	/* Now, initialize the UART */
	serial_out(port, UCR, UCR_RX_TL(0x01) | UCR_TX_TL(0x20)|	\
				UCR_PAR_NONE | \
				UCR_NBSTOP_1 | \
				UCR_UART_EN);
	return 0;
}

/*
 * Disable the port
 */
static void serial_p4a_shutdown(struct uart_port *port)
{
	struct uart_p4a_port *up = (struct uart_p4a_port *)port;
	
	
	// reset FIFOs
	serial_out(port, UCR, (UCR_RESET_TF | UCR_RESET_RF));
	udelay(5);

	// disable the port
	serial_out(port, UCR, 0);

	/* free irqs*/
	free_irq(up->port.irq, up);
	free_irq( (up->port.irq + 1), up);
}

/*
 * Change the port parameters
 */
static void serial_p4a_set_termios(struct uart_port *port, struct ktermios *termios, struct ktermios *old)
{
	struct uart_p4a_port *up = (struct uart_p4a_port *)port;
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


	/*
	 * Ask the core to calculate the divisor for us.
	 */
	port->uartclk = (unsigned int)clk_get_rate(up->clk);
	baud = uart_get_baud_rate(port, termios, old, 0, P4A_UART_MAXIMUM_BAUD(port->uartclk));

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
	serial_out(port, UCR, cval | UCR_UART_EN | UCR_RX_TL(0x1)| UCR_TX_TL(P4A_UART_FIFO_SIZE >> 1) );

	spin_unlock_irqrestore(&up->port.lock, flags);
}

/*
 * Power / Clock management.
 */
static void serial_p4a_pm(struct uart_port *port, unsigned int state, unsigned int oldstate)
{
	struct uart_p4a_port* up = (struct uart_p4a_port *)port;

	switch(state){
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
			dev_err(&up->pdev->dev, "unknown pm %d\n", state);
	}
}

/*
 * Return string describing the specified port
 */
static const char *serial_p4a_type(struct uart_port *port)
{
	struct uart_p4a_port* up = (struct uart_p4a_port *)port;

	return up->name;
}

/*
 * Release the memory region(s) being used by 'port'.
 */
static void serial_p4a_release_port(struct uart_port *port)
{
	struct uart_p4a_port* up = (struct uart_p4a_port*)port;
	int size;
	
	if (up->memres == NULL)
		return;

	size = resource_size(up->memres);

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
	struct uart_p4a_port* up = (struct uart_p4a_port*)port;
	int size;

	if (up->memres == NULL)
		return 0;

	size = resource_size(up->memres);

	if (!request_mem_region(port->mapbase, size, "p4a_serial")) {
		return -EBUSY;
	}


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
	if (flags & UART_CONFIG_TYPE) {
		serial_p4a_request_port(port);
		port->type = PORT_P4A;
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


struct uart_ops serial_p4a_ops = {
	.tx_empty		= serial_p4a_tx_empty,
	.set_mctrl		= serial_p4a_set_mctrl,
	.get_mctrl		= serial_p4a_get_mctrl,
	.stop_tx		= serial_p4a_stop_tx,
	.start_tx		= serial_p4a_start_tx,
	.stop_rx		= serial_p4a_stop_rx,
	.enable_ms		= serial_p4a_enable_ms,
	.break_ctl		= serial_p4a_break_ctl,
	.startup		= serial_p4a_startup,
	.shutdown		= serial_p4a_shutdown,
	.set_termios	= serial_p4a_set_termios,
	.pm				= serial_p4a_pm,
	.type			= serial_p4a_type,
	.release_port	= serial_p4a_release_port,
	.request_port	= serial_p4a_request_port,
	.config_port	= serial_p4a_config_port,
	.verify_port	= serial_p4a_verify_port,
};

#ifdef CONFIG_SERIAL_P4A_CONSOLE
static struct uart_driver p4a_uart;

static inline void wait_for_xmitr(struct uart_p4a_port *up)
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

	if (timeout)
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
	struct uart_p4a_port *up = p4a_ports[co->index];

	uart_console_write(&up->port, s, count, serial_p4a_console_putchar);

	// wait for transmitter to become empty
	wait_for_xmitr(up);
}

static int __init p4a_console_setup(struct console *co, char *options)
{
	struct uart_p4a_port *up;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	up = p4a_ports[co->index];
	if (unlikely(!up))
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&up->port, co, baud, parity, bits, flow);
}

static struct console serial_p4a_console = {
	.name		= "ttyS",
	.write		= p4a_console_write,
	.device		= uart_console_device,
	.setup		= p4a_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &p4a_uart,
};

#define P4A_CONSOLE_DEVICE	&serial_p4a_console
#else
#define P4A_CONSOLE_DEVICE	NULL
#endif	// CONFIG_SERIAL_P4A_CONSOLE


/*
 * Configure the port from the platform device resource info.
 */
static struct uart_driver p4a_uart = {
	.owner			= THIS_MODULE,
	.driver_name	= "p4a_serial",
	.dev_name		= "ttyS",
	.major			= TTY_MAJOR,
	.minor			= 64,
	.nr				= P4A_UART_PORTS,
	.cons			= P4A_CONSOLE_DEVICE,
};

#ifdef CONFIG_PM
static int p4a_serial_suspend(struct platform_device *dev, pm_message_t state)
{
	struct uart_p4a_port *up = platform_get_drvdata(dev);

	if (up)
		uart_suspend_port(&p4a_uart, &up->port);

	return 0;
}

static int p4a_serial_resume(struct platform_device *dev)
{
	struct uart_p4a_port *up = platform_get_drvdata(dev);

	if (up)
		uart_resume_port(&p4a_uart, &up->port);

	return 0;
}
#else
#define p4a_serial_suspend	NULL
#define p4a_serial_resume	NULL
#endif


static int p4a_serial_probe(struct platform_device *pdev)
{
	struct uart_p4a_port* up;
	struct resource *memres, *irqres;
	struct uart_p4a_platform_data *up_info;
	int line;
	int ret;
	
	up_info = pdev->dev.platform_data;
	if (up_info == NULL)
		return -EINVAL;

	line = (pdev->id >= 0) ? pdev->id : 0;
	if (unlikely(line >= P4A_UART_PORTS))
		return -ENXIO;

	memres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
/*
	if (memres == NULL) {
		dev_err(&pdev->dev, "no mem resource\n");
		return -ENODEV;
	}
*/

	irqres = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (unlikely(irqres == NULL)) {
		dev_err(&pdev->dev, "no irq resource\n");
		return -ENODEV;
	}

	up = kzalloc(sizeof(*up), GFP_KERNEL);
	if (unlikely(up == NULL))
		return -ENOMEM;

	snprintf(up->name, sizeof(up->name), "UART%d", line);
	up->pdev = pdev;
	up->memres = memres;
	up->clk = clk_get(&pdev->dev, up_info->clk_name);
	if (unlikely(IS_ERR(up->clk))) {
		ret = PTR_ERR(up->clk);
		goto err_free;
	}

	up->port.dev = &pdev->dev;
	up->port.type = PORT_P4A;
	up->port.iotype = UPIO_MEM;
	up->port.fifosize = P4A_UART_FIFO_SIZE;
	up->port.ops = &serial_p4a_ops;
	up->port.line = line;
	up->port.flags = UPF_BOOT_AUTOCONF;
	up->port.irq = irqres->start;
	up->port.irqflags = 0;


	if (up_info->membase) {
		up->port.membase = up_info->membase;

	} else if (up->memres != NULL) {
		up->port.mapbase = memres->start;		// mapbase : physical address of the IO port
		up->port.flags |= UPF_IOREMAP;
		up->port.membase = NULL;

	} else {
		dev_err(&pdev->dev, "no mem resource and mem base!\n");
		ret = -ENOMEM;
		goto err_free;
	}

	p4a_ports[line] = up;

	platform_set_drvdata(pdev, up);

	ret = uart_add_one_port(&p4a_uart, &up->port);
	if(ret){
		goto err_drvdata;
	}

	return 0;

err_drvdata:
	platform_set_drvdata(pdev, NULL);

	clk_put(up->clk);

err_free:
	kfree(up);

	return ret;
}

static int __devexit p4a_serial_remove(struct platform_device *pdev)
{
	struct uart_p4a_port *up = platform_get_drvdata(pdev);
	int ret = 0;

	platform_set_drvdata(pdev, NULL);


	if (up) {
		ret = uart_remove_one_port(&p4a_uart, &up->port);
		
		clk_put(up->clk);
	
		kfree(up);
	}

	return ret;
}

static struct platform_driver p4a_serial_driver = {
	.probe		= p4a_serial_probe,
	.remove		= __devexit_p(p4a_serial_remove),
	.suspend	= p4a_serial_suspend,
	.resume		= p4a_serial_resume,
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init p4a_serial_init(void)
{
	int ret;

	pr_info("Serial: UART driver\n");

	ret = uart_register_driver(&p4a_uart);
	if (ret != 0)
		return ret;

	ret = platform_driver_register(&p4a_serial_driver);
	if (ret != 0)
		uart_unregister_driver(&p4a_uart);

	return ret;
}

static void __exit p4a_serial_exit(void)
{
	platform_driver_unregister(&p4a_serial_driver);
	uart_unregister_driver(&p4a_uart);
}

module_init(p4a_serial_init);
module_exit(p4a_serial_exit);

MODULE_AUTHOR("Jimmy li <lizhengming@innofidei.com>");
MODULE_DESCRIPTION("Innofidei P4A serial port driver");
MODULE_LICENSE("GPL");

