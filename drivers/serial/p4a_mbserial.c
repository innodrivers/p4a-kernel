/*
 *  linux/drivers/serial/p4a_mbserial.c
 *
 *	Innofidei P4A mailbox serial driver
 *
 *  Author:	wangxiwei <wangxiwei@innofidei.com>
 *  Copyright:	(C) 2012 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <asm/io.h>
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

#include <mach/micproto.h>
#include <mach/micp_cmd.h>


#define DRV_NAME				"p4a-mbserial"

#define MAILBOX_SERIAL_MINOR	(230)
#define	P4A_MBSERIAL_NUM		CONFIG_MAILBOX_SERIAL_P4A_NRPORTS

#define	MBSERIAL_FIFO_SIZE		(1024)


#define PRE_ALLOC_MREQ_NUM			(32)
#define PRE_ALLOC_DATA_SIZE			(256)


static void p4a_mbserial_tx_work(struct work_struct *work);

enum MBSERIAL_SUBCMD {
	MBSERIAL_CMD_STARTUP  = 0,
	MBSERIAL_CMD_SHUTDOWN,
	MBSERIAL_CMD_DATA_TRANSFER,
};

/**
 * @brif - Runtime info holder for mailbox serial driver.
 *
 * @port: uart port.
 * @work: work run in work queue.
 * @free_mreqb_list: .
 * @mreqb_list_lock: .
 * @transfer_pending: have data to transfer flag.
 *
 */
struct p4a_mbserial_port {
	struct uart_port	u_port;	/* must be first element */

	struct device *dev;

	struct work_struct	work;
	int transfer_pending;

	struct list_head free_mreqb_list;
	spinlock_t	mreqb_list_lock;
};

static struct p4a_mbserial_port *allports[P4A_MBSERIAL_NUM];

static struct uart_driver p4a_mailbox_uart = {
	.owner			= THIS_MODULE,
	.driver_name	= "p4a_mbserial",
	.dev_name		= "ttyM",
	.major			= TTY_MAJOR,
	.minor			= MAILBOX_SERIAL_MINOR,
	.nr				= P4A_MBSERIAL_NUM,
	.cons			= NULL,
};

/*-----------------------------------------------------------------------*/


/* prepare alloc mailbox request block  */
static void pre_alloc_mreqb_init(struct p4a_mbserial_port *mp)
{
	struct list_head *free_mreqb_list = &mp->free_mreqb_list;
	spinlock_t	*mreqb_list_lock = &mp->mreqb_list_lock;
	struct mreqb* mreq;
	int i;

	spin_lock_init(mreqb_list_lock);
	INIT_LIST_HEAD(free_mreqb_list);


	for (i = 0; i < PRE_ALLOC_MREQ_NUM; i++) {
		mreq = mreqb_alloc(PRE_ALLOC_DATA_SIZE);
		list_add_tail(&(mreq->node), free_mreqb_list);
	}
}

/* free  mailbox request block which prepare alloced */
static void pre_alloc_mreqb_exit(struct p4a_mbserial_port *mp)
{
	struct list_head *free = &mp->free_mreqb_list;
	spinlock_t	*lock = &mp->mreqb_list_lock;
	struct mreqb* mreq;

	spin_lock(lock);
	
	while (!list_empty(free)) {
		mreq = list_entry(free->next, struct mreqb, node);
		list_del_init(&(mreq->node));
		mreqb_free(mreq);	
	}
	
	spin_unlock(lock);
}

/* get unused mreqb */
static struct mreqb* get_mreqb(struct p4a_mbserial_port *mp)
{
	struct list_head *free = &mp->free_mreqb_list;
	spinlock_t *lock = &mp->mreqb_list_lock;
	struct mreqb* mreq = NULL;

	spin_lock(lock);
	
	if (!list_empty(free)) {
		mreq = list_entry(free->next, struct mreqb, node);
		list_del_init(&(mreq->node));
	}
	
	spin_unlock(lock);

	return mreq;
}

/* return mreqb back after use it */
static void put_mreqb(struct mreqb* mreq)
{
	struct p4a_mbserial_port *mp; 

	mp = (struct p4a_mbserial_port *)mreq->context;

	BUG_ON(mp == NULL);

	spin_lock(&mp->mreqb_list_lock);
	mreqb_reinit(mreq);
	list_add_tail(&mreq->node, &mp->free_mreqb_list);
	spin_unlock(&(mp->mreqb_list_lock));

	// are there anything data pending when mreqb limited before ??
	// if yes, try to wakeup a thread to continue send data
	if (1 == mp->transfer_pending) {
		mp->transfer_pending = 0;
		schedule_work(&mp->work);
	}
}

/*------------------------------------------------------------------------*/

/**
 * @brief mailbox uart receive function
 *
 * @param[in] line : uart port index
 * @param[in] data : received data buffer
 * @param[in] len : received data length
 *
 * @return 	zero is successful.
 */
static int mbserial_recv_data(int line, void *data, size_t len)
{
	struct p4a_mbserial_port *mp;
	struct uart_port *port;
	struct tty_struct *tty;

	if ((line < 0) || (line >= P4A_MBSERIAL_NUM)) {
		printk("%s receive error uart type \n", __FUNCTION__);
		return -EINVAL;
	}

	mp = allports[line];
	if (NULL == mp) {
		printk("%s mb_serial_port is NULL \n", __FUNCTION__);
		return -EINVAL;
	}

	port = &(mp->u_port);
	
	if (likely(port)) {
		if (len < 0) {
			dev_err(mp->dev, " receive data len is error %d \n", len);
			return -EINVAL;
		}
		
		tty = tty_port_tty_get(&(port->state->port));
		if (NULL != tty) {
			tty_insert_flip_string(tty, (unsigned char *)data, len);
			tty_flip_buffer_push(tty);
			tty_kref_put(tty);

		} else {
			dev_err(mp->dev, "tty_struct is NULL \n");
			return -EINVAL;
		}
	} else {
		dev_err(mp->dev, "%s mailbox uart port is NULL \n", __FUNCTION__);
		return -EINVAL;
	}
	
	return 0;
	
}

static int do_mbserial_request(struct mreqb *reqb, void* priv)
{
	int line;
	void *buf;
	size_t bufsz;

	int ret = -1;


	switch (reqb->subcmd) {
	case MBSERIAL_CMD_DATA_TRANSFER:
	{
		buf = (void*)reqb->extra_data;
		bufsz = (size_t)MREQB_POP_ARG(reqb);
		line = (int)MREQB_POP_ARG(reqb);

		ret = mbserial_recv_data(line, buf, bufsz);
		break;
	}
	case MBSERIAL_CMD_STARTUP:
	case MBSERIAL_CMD_SHUTDOWN:
	default:
		break;
	}

	return ret;
}

/**
 * @brief mailbox uart transfer function
 *
 * @param[in] mb_serial_port: .
 * @param[in] type: mailbox serial type.
 * @param[in] data: data to transfer.
 * @param[in] len: lenth of the data.
 *
 * @return 	actual lenth to tranfer.  negative number is fail
 */
static int p4a_mbserial_transfer(struct p4a_mbserial_port *mp,
							int line, 
							unsigned char *data,
							int len)
{
	 struct mreqb *request;
	 int tmp_len, real_len, act_len = 0;

	 real_len = len;

	 while (real_len > 0) {
	     request = get_mreqb(mp);
		 
		 if (NULL == request) {
			break;
		 }
		 
		 tmp_len = min_t(int, PRE_ALLOC_DATA_SIZE, real_len);
		 memcpy(request->extra_data, data, tmp_len);
	 
		 MREQB_BIND_CMD(request, SERIAL_REQUEST);
		 MREQB_SET_SUBCMD(request, MBSERIAL_CMD_DATA_TRANSFER);

		 MREQB_PUSH_ARG(request, line);
		 MREQB_PUSH_ARG(request, tmp_len);

		 request->context = mp;
	     request->complete = put_mreqb;
 		
    	 mreqb_submit(request);

		 real_len -= tmp_len;
		 act_len += tmp_len;
	 }

	 return act_len;
}

static void p4a_mbserial_tx(struct p4a_mbserial_port *mp)
{	
	int cnt_to_end, count, len;
	struct circ_buf *xmit;
	struct uart_port *port;

	port = &(mp->u_port);
	
	xmit = &port->state->xmit;
	
	if (port->x_char) {	
		p4a_mbserial_transfer(mp, port->line, &port->x_char, 1);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	//transfer data
	len = uart_circ_chars_pending(xmit);
	if (len <= 0) {
		return;
	}
	
	do {
		cnt_to_end = CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE);
		
		count = p4a_mbserial_transfer(mp, port->line,
								xmit->buf + xmit->tail, cnt_to_end);

		//data is not send, mailbox request block is no enough. 
		//wait for free mailbox request block.
		//will send in work queue (function p4a_mbserial_tx_work).
		if (count <= 0) {
			mp->transfer_pending = 1;
			break;
		}
		
		xmit->tail = (xmit->tail + count) & (UART_XMIT_SIZE -1);
		port->icount.tx += count;

		len = uart_circ_chars_pending(xmit);
		
	} while ( len > 0);

	if (len < WAKEUP_CHARS)
		uart_write_wakeup(port);
}


static void p4a_mbserial_tx_work(struct work_struct *work)
{	
	struct p4a_mbserial_port *mp = container_of(work, struct p4a_mbserial_port, work);
	struct uart_port *port = &mp->u_port;

	//lock to protect uart port's resource
	spin_lock(&port->lock);
	
	p4a_mbserial_tx(mp);

	spin_unlock(&port->lock);
}


static void mbserial_start_tx(struct uart_port *port)
{
	struct p4a_mbserial_port *mp = (struct p4a_mbserial_port *)port;
	
	BUG_ON(mp == NULL);
	
	p4a_mbserial_tx(mp);
}


/**
 * @brief report mailbox uart state
 *
 * @param[in] type: mailbox serial type.
 * @param[in] action: mailbox uart action.
 *
 * @return 	zero is successful.
 */
static int mbserial_send_request(int line, int action)
{
	 struct mreqb *request;

     request = mreqb_alloc(0);
 
	 MREQB_BIND_CMD(request, SERIAL_REQUEST);
	 MREQB_SET_SUBCMD(request, action);
	 
	 MREQB_PUSH_ARG(request, line);
	
     request->complete = mreqb_completion_free_mreqb;
 
     mreqb_submit(request);

	 return 0;
}

static void mbserial_stop_tx(struct uart_port *port)
{
}

static void mbserial_stop_rx(struct uart_port *port)
{
}

/* Return TIOCSER_TEMT when transmitter FIFO is empty, otherwise return 0.*/
static unsigned int mbserial_tx_empty(struct uart_port *port)
{
	return TIOCSER_TEMT;
}

static int mbserial_startup(struct uart_port *port)
{
	struct p4a_mbserial_port *mp = (struct p4a_mbserial_port*)port;

	INIT_WORK(&mp->work, p4a_mbserial_tx_work);

	pre_alloc_mreqb_init(mp);
	
	return mbserial_send_request(port->line, MBSERIAL_CMD_STARTUP);	
}

static void mbserial_shutdown(struct uart_port *port)
{
	struct p4a_mbserial_port *mp = (struct p4a_mbserial_port*)port;

	mbserial_send_request(port->line, MBSERIAL_CMD_SHUTDOWN);

	pre_alloc_mreqb_exit(mp);
}

static void mbserial_set_termios(struct uart_port *port, struct ktermios *termios, struct ktermios *old)
{
}

static void mbserial_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static unsigned int mbserial_get_mctrl(struct uart_port *port)
{
	return 0;
}

static void mbserial_enable_ms(struct uart_port *port)
{
}

static void mbserial_release_port(struct uart_port *port)
{
}

static int mbserial_request_port(struct uart_port *port)
{
	return 0;
}

static void mbserial_config_port(struct uart_port *port, int flags)
{
}

struct uart_ops p4a_mbserial_uart_ops = {
	.startup	= mbserial_startup,
	.shutdown	= mbserial_shutdown,
	.tx_empty	= mbserial_tx_empty,
	.stop_tx	= mbserial_stop_tx,
	.start_tx	= mbserial_start_tx,
	.stop_rx	= mbserial_stop_rx,
	.set_termios	= mbserial_set_termios,
	.set_mctrl	= mbserial_set_mctrl,
	.get_mctrl	= mbserial_get_mctrl,
	.enable_ms	= mbserial_enable_ms,
	.type		= NULL,
	.release_port	= mbserial_release_port,
	.request_port	= mbserial_request_port,
	.config_port	= mbserial_config_port,
};


static int p4a_mbserial_probe(struct platform_device *pdev)
{
	struct p4a_mbserial_port *mp;
	int line;
	int ret;

	line = (pdev->id >= 0) ? pdev->id : 0;
	if (line >= P4A_MBSERIAL_NUM)
		return -ENXIO;

	pr_info("mailbox serial: detected port #%d\n", line);
	
	mp = kzalloc(sizeof(struct p4a_mbserial_port), GFP_KERNEL);
	if (unlikely(mp == NULL)) {
		dev_err(&pdev->dev, "Unable to malloc p4a_mbserial_port\n");
		return -ENOMEM;
	}

	mp->dev = &pdev->dev;
	mp->u_port.dev = &pdev->dev;
	mp->u_port.type = PORT_P4A;
	mp->u_port.iotype = UPIO_MEM;
	mp->u_port.fifosize = MBSERIAL_FIFO_SIZE;
	mp->u_port.ops = &p4a_mbserial_uart_ops;
	mp->u_port.line = line;

	platform_set_drvdata(pdev, mp);
	
	allports[line] = mp;

	ret = uart_add_one_port(&p4a_mailbox_uart, &(mp->u_port));
	if (ret != 0) {
		dev_err(&pdev->dev, "Unable to add one uart port\n");
		platform_set_drvdata(pdev, NULL);
		kfree(mp);
		return ret;
	}
	
	return 0;
}

static int __devexit  p4a_mbserial_remove(struct platform_device *pdev)
{
	struct p4a_mbserial_port *mp = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	if (unlikely(NULL != mp)) {

		allports[mp->u_port.line] = NULL;
		uart_remove_one_port(&p4a_mailbox_uart, &(mp->u_port));
		kfree(mp);
	}

	return 0;
}


static struct platform_driver p4a_mbserial_driver = {
	.probe      = p4a_mbserial_probe,
	.remove		= __devexit_p(p4a_mbserial_remove),
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init p4a_mbserial_init(void)
{
	int ret;
	
	printk("P4A Mailbox Serial Driver, (c) 2013 Innofidei Inc.\n");

	ret = uart_register_driver(&p4a_mailbox_uart);
	if (ret != 0) {
		return ret;
	}

	ret = platform_driver_register(&p4a_mbserial_driver);
	if (ret != 0) {
		uart_unregister_driver(&p4a_mailbox_uart);
		return ret;
	}
	
	mreqb_register_cmd_handler(C_SERIAL_REQUEST, do_mbserial_request, NULL);

	return ret;
}


static void __exit p4a_mbserial_exit(void)
{
	platform_driver_unregister(&p4a_mbserial_driver);
	uart_unregister_driver(&p4a_mailbox_uart);
}

module_init(p4a_mbserial_init);
module_exit(p4a_mbserial_exit);

MODULE_AUTHOR("wangxiwei <wangxiwei@innofidei.com>");
MODULE_DESCRIPTION("Innofidei mailbox serial driver");
MODULE_LICENSE("GPL");

