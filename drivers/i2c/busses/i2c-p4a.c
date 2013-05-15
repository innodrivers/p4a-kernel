/*
 * I2C master driver for Innofidei P4A.
 *
 * Copyright (C) 2013 Innofidei Inc. All Rights Reserved.
 *
 *This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/

/*#define DEBUG */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <asm/irq.h>
#include <mach/p4a_i2c.h>

#define DRV_NAME	"p4a-i2c"


/* I2C Registers */
#define IIC_CTRL			(0x00)
#define IIC_CMD				(0x04)
#define IIC_TXR				(0x08)
#define IIC_RXR				(0x0c)
#define IIC_PRESCALE		(0x10)
#define IIC_STATUS			(0x14)
#define IIC_ARBLOS			(0x18)

/* I2C Register bit fields */
#define IICCTRL_RESET		(1<<0)
#define IICCTRL_EN		(1<<1)

#define IICCMD_BSTART	(1<<0)
#define IICCMD_BSTOP	(1<<1)
#define IICCMD_DIRTX		(1<<2)
#define IICCMD_DIRRX		(1<<3)
#define IICCMD_RDNACK	(1<<4)

#define IICSTA_ARBLOST	(1<<0)
#define IICSTA_OPDONE	(1<<1)
#define IICSTA_BUSY		(1<<2)
#define IICSTA_ACK		(1<<3)

/*----------------------------------------------------------*/

#define WAIT_BUSFREE_CNT	(100)	//wait busfree time loop count
#define WAITBUSYMS			(2)		//wait busfree msleep this ms.
#define P4AI2C_RETRIES		(2)		//i2c transfer retries count, when transfer fail.
#define IIC_ADDRMASK 		(0x7f)	//i2c 7bit addr mask
#define IIC_DIR_READ 		(1)		//i2c read flag, at the LSB. of the addr. 
#define TXSETUPDELAY		(1)		//ndelay()

enum p4a_i2c_state {
    STATE_IDLE = 0,		//i2c in bus idle state.
    STATE_ADDR,			//i2c in send a addr state
    STATE_READ,			//i2c in read data state.
    STATE_WRITE,		//i2c in write data state.
};

struct p4a_i2c {
	wait_queue_head_t	 wait_queue;	//wait queue head , block at p4a_i2c_transfer_data. waking up while data transfer over.
	int	errcode;
	spinlock_t		lock;				//spin lock, protect i2c req op. Mutex with isr.
	struct i2c_msg		*i2c_op_req; 	//point to i2c_msg, which pass from i2c_driver to i2c_adapter.
	unsigned int		buf_idx;		//index the  req->buf  content.
	unsigned int		delay_us;		//usec delay time.

	enum p4a_i2c_state	state;

	u32					speed;			// speed of bus in KHz
	struct device		*dev;
	void __iomem		*base;			//virtual register base
	unsigned int		irq;
	struct clk			*clk;

	struct i2c_adapter	adapter;
};

/*--------------------------------------------------------------*/

#ifdef P4A_I2C_DEBUG
static int  p4a_i2c_dump_reg(struct p4a_i2c *i2c_adap)
{
	printk("IIC_CTRL       : 0x%x\n", p4a_i2c_read_reg(i2c_adap,  IIC_CTRL));
	printk("IIC_CMD        : 0x%x\n", p4a_i2c_read_reg(i2c_adap, IIC_CMD));
	printk("IIC_PRESCALE   : 0x%x\n", p4a_i2c_read_reg(i2c_adap, IIC_PRESCALE));
	printk("IIC_STATUS     : 0x%x\n", p4a_i2c_read_reg(i2c_adap, IIC_STATUS));
	printk("IIC_ARBLOS     : 0x%x\n", p4a_i2c_read_reg(i2c_adap, IIC_ARBLOS));

	return 0;
}
#else
static inline int  p4a_i2c_dump_reg(struct p4a_i2c *i2c_adap) { return 0;}
#endif


static inline void p4a_i2c_write_reg(struct p4a_i2c *i2c_adap, int reg, u32 val)
{
	writel(val, i2c_adap->base + reg);
}

static inline u32 p4a_i2c_read_reg(struct p4a_i2c *i2c_adap, int reg)
{
	return readl(i2c_adap->base + reg);
}

/*-----------------------------------------------------------------*/
/*
 * set i2c clk.
 */
static int  p4a_i2c_set_clk(struct p4a_i2c *i2c_adap)
{
	unsigned long scale;

	scale = clk_get_rate(i2c_adap->clk) / (4 * i2c_adap->speed * 1000);

	p4a_i2c_write_reg(i2c_adap, IIC_PRESCALE, scale);

	p4a_i2c_write_reg(i2c_adap, IIC_ARBLOS, (p4a_i2c_read_reg(i2c_adap, IIC_PRESCALE) << 1));

	return 0;
}

/*
 * check i2c bus free
 */
static int p4a_i2c_wait_busfree(struct p4a_i2c *i2c_adap)
{
	int waitcnt;
	int cntval;
	int state;

	waitcnt = WAIT_BUSFREE_CNT;

	for (cntval = 0; cntval < waitcnt; cntval++) {
		state = p4a_i2c_read_reg(i2c_adap, IIC_STATUS);

		if (state & IICSTA_BUSY) {
			p4a_i2c_dump_reg(i2c_adap);
			msleep(WAITBUSYMS);
		} else {
			break;
		}
	}

	if (cntval >= waitcnt) {
		return -EBUSY;
	}

	return 0;
}

/*
 *	generate a start bit.
 */
static int p4a_i2c_generate_startbit(struct p4a_i2c *i2c_adap )
{
	unsigned int addr;
	unsigned int cmd;

	/*set the transfer dir,other flag define in i2c_msg->flag is not addr care*/

	addr = (i2c_adap->i2c_op_req->addr & IIC_ADDRMASK) << 1;

	/*config  direction is read*/
	if (i2c_adap->i2c_op_req->flags & I2C_M_RD) {
		addr |= IIC_DIR_READ;
	}

	cmd = IICCMD_BSTART;
	cmd |= IICCMD_DIRTX;
	p4a_i2c_write_reg(i2c_adap, IIC_TXR, addr);
	p4a_i2c_write_reg(i2c_adap, IIC_CMD, cmd);
	udelay(i2c_adap->delay_us);

	return 0;
}


/*
 * generate a stop bit.
 */
static int p4a_i2c_generate_stopbit(struct p4a_i2c *i2c_adap)
{
	unsigned int cmd;

	cmd = IICCMD_BSTOP;
	p4a_i2c_write_reg(i2c_adap, IIC_CMD, cmd);
	return 0;
}

/*
 * wake up blocked process
 */
static void  p4a_i2c_wakeup_waitqueue(struct p4a_i2c *i2c_adap, int err)
{
	if (err < 0) {
		dev_err(i2c_adap->dev, "%s %d   err=%d\r\n", __func__, __LINE__, err);
		p4a_i2c_dump_reg(i2c_adap);
	}


	i2c_adap->buf_idx = 0;
	i2c_adap->i2c_op_req = NULL;

	i2c_adap->state = STATE_IDLE;
	i2c_adap->errcode = err;

	/*wakeup wait queue*/
	wake_up(&i2c_adap->wait_queue);

	return;
}

/*
 * reset i2c controller
 */
static int p4a_i2c_reset(struct p4a_i2c *i2c_adap, int genstop)
{
	/*generate  a stop bit.*/
	if (genstop) {
		p4a_i2c_write_reg(i2c_adap, IIC_CMD, IICCMD_BSTOP);
	}

	/*reset i2c controller*/
	p4a_i2c_write_reg(i2c_adap, IIC_CTRL, IICCTRL_RESET);
	udelay(10);
	p4a_i2c_write_reg(i2c_adap, IIC_CTRL, IICCTRL_EN);

	return 0;
}

/*
 * disable i2c controller.
 */
static void p4a_i2c_disable(struct p4a_i2c *i2c_adap)
{
	p4a_i2c_write_reg(i2c_adap, IIC_CTRL, 0); //disable i2c controller.
}

/*
* @brief -
* @param[in] adap: an i2c_adapter instance
* @param[in]msgs: i2c_msg  array
* @param[in]num:  i2c_msgs count.
*
* @return
*	if transfer sucsess return the sucsess msgs count, else a neg. val to indicate an err.
*/
static int p4a_i2c_transfer_data(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct p4a_i2c *i2c_adap;
	int ret;
	unsigned long flags;
	unsigned long timeout;
	int ncnt;
	int msgcnt;


	timeout = 0;
	i2c_adap = (struct p4a_i2c *)adap->algo_data;
	if (unlikely(NULL == i2c_adap)) {
		return -ENODEV;
	}

	/*not support msg.len is zero as I2C read op,so we check it*/
	for (ncnt = 0; ncnt < num; ncnt++) {
		if ((msgs[ncnt].flags & I2C_M_RD) && (0 == msgs[ncnt].len)) {
			return -ENXIO;
		}
	}

	/*chk bus free*/
	ret = p4a_i2c_wait_busfree(i2c_adap);
	if (ret < 0) {
		dev_warn(i2c_adap->dev, "%s %d wait busfree timeout.\r\n", __func__, __LINE__);
		p4a_i2c_reset(i2c_adap, 0);
		return ret;
	}

	/*transfer num msgs' loop*/
	for (msgcnt = 0; msgcnt < num; msgcnt++) {
		spin_lock_irqsave(&i2c_adap->lock, flags);
		i2c_adap->i2c_op_req = &msgs[msgcnt];
		i2c_adap->buf_idx = 0;
		i2c_adap->errcode = 0;
		i2c_adap->state = STATE_ADDR;
		spin_unlock_irqrestore(&i2c_adap->lock, flags);

		p4a_i2c_generate_startbit(i2c_adap);

		/*wait transfer complete.*/
		timeout = wait_event_timeout(i2c_adap->wait_queue, i2c_adap->state == STATE_IDLE, HZ * 5);

		if (i2c_adap->errcode < 0) {
			dev_err(i2c_adap->dev, "%s %d a tansfer err Happened ,errcode=%d msgcnt=%d\r\n ", __func__, __LINE__, i2c_adap->errcode, msgcnt);
			p4a_i2c_reset(i2c_adap, 1);
			break;
		}

		if (timeout  ==  0) {
			dev_err(i2c_adap->dev, "%s %d wait transfer TIMEOUT timeout=%ld\r\n ", __func__, __LINE__, timeout);
			p4a_i2c_reset(i2c_adap, 1);
			break;
		}

		if (msgcnt == (num - 1)) {
			p4a_i2c_generate_stopbit(i2c_adap);
		}
	}


	ret = p4a_i2c_wait_busfree(i2c_adap);

	if (ret < 0) {
		dev_warn(i2c_adap->dev, "%s %d wait busfree timeout.\r\n", __func__, __LINE__);
		p4a_i2c_reset(i2c_adap, 0);
		return ret;
	}

	if ((0 == i2c_adap->errcode) && timeout) {
		return num;
	}

	if (i2c_adap->errcode < 0) {
		return i2c_adap->errcode;
	}

	return -ENXIO;

}

/*
 * adap: an i2c_adapter instance
 * return the func of this controller driver support.
 */
static unsigned int p4a_i2c_report_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_PROTOCOL_MANGLING;
}

static const struct i2c_algorithm p4a_i2c_algorithm = {
	.master_xfer =  p4a_i2c_transfer_data,
	.functionality =  p4a_i2c_report_func,
};

/*
 *  i2c init :  config  clk, io pinmux config,reset i2c bus.
 */
static int p4a_i2c_init(struct p4a_i2c *i2c_adap)
{

	/*reset i2c and enable it*/
	p4a_i2c_write_reg(i2c_adap, IIC_CTRL, IICCTRL_RESET);
	msleep(1);
	p4a_i2c_write_reg(i2c_adap, IIC_CTRL, IICCTRL_EN);


	p4a_i2c_set_clk(i2c_adap);

	return 0;
}


/*
 * if this is the last data to read into i2c_op_req->buf
 */
static inline int is_lastdata(struct p4a_i2c *i2c_adap)
{
	return i2c_adap->buf_idx == (i2c_adap->i2c_op_req->len - 1);
}

/*
 * if the i2c_op_req->buf  's data send/recv over.
 */
static inline int is_msgbuf_end(struct p4a_i2c *i2c_adap)
{
	return i2c_adap->buf_idx >= i2c_adap->i2c_op_req->len;
}

/*
 * state machine in state addr
 */
static int p4a_i2c_in_state_addr(struct p4a_i2c *i2c_adap, unsigned int iicstat)
{
	unsigned cmd;
	unsigned char byte;

	cmd = 0;

	/* ack was not received... */
	if ((!(iicstat & IICSTA_ACK)) &&
	        (!(i2c_adap->i2c_op_req->flags & I2C_M_IGNORE_NAK))) {
		dev_err(i2c_adap->dev, "ack was not received when send addr ...i2c_adap->state=0x%x,i2c_adap->i2c_op_req->flags=0x%x\r\n", \
		        i2c_adap->state, i2c_adap->i2c_op_req->flags);
		p4a_i2c_wakeup_waitqueue(i2c_adap, -ENXIO);
		return -ENXIO;
	}

	/* terminate the transfer if there is nothing to do
	 * as this is used by the i2c probe to find devices. */
	if (i2c_adap->i2c_op_req->len == 0)  {
		p4a_i2c_wakeup_waitqueue(i2c_adap, 0);
		return 0;
	}

	if (i2c_adap->i2c_op_req->flags & I2C_M_RD) {
		i2c_adap->state = STATE_READ;
	} else {
		i2c_adap->state = STATE_WRITE;
	}


	if (i2c_adap->state == STATE_READ) {
		if (is_lastdata(i2c_adap)) { // last byte to be read to buffer ,which need to send NAK signal to device.
			cmd  = IICCMD_DIRRX;
			cmd |= IICCMD_RDNACK;
		} else {
			cmd  = IICCMD_DIRRX;
		}

		p4a_i2c_write_reg(i2c_adap, IIC_CMD, cmd);

	} else { /*i2c_adap->state in STATE_WRITE.*/
		byte = i2c_adap->i2c_op_req->buf[i2c_adap->buf_idx];
		i2c_adap->buf_idx++;
		p4a_i2c_write_reg(i2c_adap, IIC_TXR, byte);
		cmd = IICCMD_DIRTX;
		p4a_i2c_write_reg(i2c_adap, IIC_CMD, cmd);
	}

	return 0;

}

/*
 * state machine in state read
 */
static int p4a_i2c_in_state_read(struct p4a_i2c *i2c_adap, unsigned int iicstat)
{
	unsigned char byte;
	unsigned int cmd;

	/* we have a byte of data in the data register, do
	 * something with it, and then work out wether we are
	 * going to do any more read/write
	 */

	byte = readb(i2c_adap->base + IIC_RXR);
	i2c_adap->i2c_op_req->buf[i2c_adap->buf_idx] = byte;
	i2c_adap->buf_idx++;

	if (is_msgbuf_end(i2c_adap)) {
		p4a_i2c_wakeup_waitqueue(i2c_adap, 0);

	} else if (is_lastdata(i2c_adap)) {	/* the last date to be read,send the nak signal.*/
		cmd  = IICCMD_DIRRX;
		cmd |= IICCMD_RDNACK;
		p4a_i2c_write_reg(i2c_adap, IIC_CMD, cmd);

	}  else {  /*go on trigger next byte 's read op.*/
		cmd  = IICCMD_DIRRX;
		p4a_i2c_write_reg(i2c_adap, IIC_CMD, cmd);
	}

	return 0;

}

/*
 * state machine in state write
 */
static int p4a_i2c_in_state_write(struct p4a_i2c *i2c_adap, unsigned int iicstat)
{
	unsigned char byte;
	unsigned int cmd;

	/* we are writing data to the device... check for the
	 * end of the message, and if so, work out what to do
	*/

	if (!(i2c_adap->i2c_op_req->flags & I2C_M_IGNORE_NAK)) {
		if (!(iicstat & IICSTA_ACK)) {
			dev_err(i2c_adap->dev, "WRITE: No Ack err!   i2c_adap->state=0x%x,i2c_adap->i2c_op_req->flags=0x%x\r\n", \
			        i2c_adap->state, i2c_adap->i2c_op_req->flags);
			p4a_i2c_wakeup_waitqueue(i2c_adap, -ENXIO);
			return -ENXIO;
		}
	}

	if (is_msgbuf_end(i2c_adap)) {  /*current msg's data send over*/
		p4a_i2c_wakeup_waitqueue(i2c_adap, 0);

	} else { 		/*not reach i2c_op_req end,continue write  data*/
		byte = i2c_adap->i2c_op_req->buf[i2c_adap->buf_idx];
		i2c_adap->buf_idx++;
		p4a_i2c_write_reg(i2c_adap, IIC_TXR, byte);
		cmd = IICCMD_DIRTX;
		p4a_i2c_write_reg(i2c_adap, IIC_CMD, cmd);
	}

	return 0;
}

/*
 * interrupt handle.
 */
static irqreturn_t p4a_i2c_isr(int this_irq, void *dev_id)
{
	unsigned int status;
	struct p4a_i2c *i2c_adap;

	i2c_adap = (struct p4a_i2c *) dev_id;

	status = p4a_i2c_read_reg(i2c_adap, IIC_STATUS);

	if (unlikely(NULL == i2c_adap->i2c_op_req)) {
		dev_err(i2c_adap->dev, "%s:%d i2c msg req is NULL\n", __func__, __LINE__);
		p4a_i2c_wakeup_waitqueue(i2c_adap, -ENOMEM);
		return IRQ_HANDLED;
	}

	if (status & IICSTA_ARBLOST) {
		dev_err(i2c_adap->dev, "%s %d   lost arbi !!!! i2c_adap->state=0x%x ,flags=0x%x\r\n",
		        __func__, __LINE__, i2c_adap->state, i2c_adap->i2c_op_req->flags);
		p4a_i2c_wakeup_waitqueue(i2c_adap, -EIO);
		return IRQ_HANDLED;
	}

	switch (i2c_adap->state) {
	case STATE_ADDR:
		p4a_i2c_in_state_addr(i2c_adap, status);
		break;

	case STATE_WRITE:
		p4a_i2c_in_state_write(i2c_adap, status);
		break;

	case STATE_READ:
		p4a_i2c_in_state_read(i2c_adap, status);
		break;

	case STATE_IDLE:
	default:
		break;
	}

	return IRQ_HANDLED;
}

static int __devinit p4a_i2c_probe(struct platform_device *pdev)
{
	struct p4a_i2c *i2c_adap;
	struct p4a_i2c_platform_data *pdata;
	struct resource *mem, *irq;
	struct resource *ioarea;
	struct clk* clk;
	int ret = 0;

	pdata = (struct p4a_i2c_platform_data *)pdev->dev.platform_data;

	if (!pdata) {
		dev_err(&pdev->dev, "no platform data\n");
		return -EINVAL;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "no mem resource?\n");
		return -ENODEV;
	}

	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq) {
		dev_err(&pdev->dev, "no irq resource ?\n");
		return -ENODEV;
	}

	ioarea = request_mem_region(mem->start, resource_size(mem),
	                        pdev->name);
	if (!ioarea) {
		dev_err(&pdev->dev, "I2C region already claimed!\n");
		return -EBUSY;
	}

	clk = clk_get(&pdev->dev, pdata->clk_name);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "no i2c clock?\n");
		ret = PTR_ERR(clk);
		goto err_release_region;
	}

	i2c_adap = kzalloc(sizeof(struct p4a_i2c), GFP_KERNEL);
	if (!i2c_adap) {
		dev_err(&pdev->dev, "no memory for p4a_i2c\n");
		ret = -ENOMEM;
		goto err_put_clock;
	}


	i2c_adap->clk = clk;
	i2c_adap->dev = &pdev->dev;
	i2c_adap->irq = irq->start;
	i2c_adap->base = ioremap(mem->start, resource_size(mem));
	if (i2c_adap->base == NULL) {
		dev_err(&pdev->dev, "cannot map IO\n");
		ret = -ENOMEM;
		goto err_free_mem;
	}
	i2c_adap->speed = pdata->clkrate;

	platform_set_drvdata(pdev, i2c_adap);

	strlcpy(i2c_adap->adapter.name, "p4a-i2c", sizeof(i2c_adap->adapter.name));
	i2c_adap->adapter.owner   = THIS_MODULE;
	i2c_adap->adapter.algo    = &p4a_i2c_algorithm;
	i2c_adap->adapter.retries = P4AI2C_RETRIES;
	i2c_adap->adapter.class   = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	i2c_adap->adapter.algo_data = i2c_adap;
	i2c_adap->adapter.dev.parent = &pdev->dev;
	i2c_adap->adapter.nr = (pdev->id > 0) ? pdev->id : 0;

	i2c_adap->delay_us = TXSETUPDELAY;
	spin_lock_init(&i2c_adap->lock);
	init_waitqueue_head(&i2c_adap->wait_queue);

	/* initialise the i2c controller */
	ret = p4a_i2c_init(i2c_adap);
	if (ret != 0) {
		goto err_iounmap;
	}

	ret = request_irq(i2c_adap->irq, p4a_i2c_isr, 0, dev_name(&pdev->dev), i2c_adap);
	if (ret != 0) {
		dev_err(&pdev->dev, "cannot claim IRQ %d\n", i2c_adap->irq);
		goto err_i2c_uninit;
	}

	ret = i2c_add_numbered_adapter(&i2c_adap->adapter);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add bus to i2c core\n");
		goto err_free_irq;
	}


	return 0;

err_free_irq:
	free_irq(i2c_adap->irq, i2c_adap);

err_i2c_uninit:
	p4a_i2c_disable(i2c_adap);

err_iounmap:
	iounmap(i2c_adap->base);

err_free_mem:
	platform_set_drvdata(pdev, NULL);
	kfree(i2c_adap);

err_put_clock:
	clk_put(clk);

err_release_region:
	release_mem_region(mem->start, resource_size(mem));

	return ret;
}


static int p4a_i2c_remove(struct platform_device *pdev)
{
	struct p4a_i2c *i2c_adap = platform_get_drvdata(pdev);
	struct resource *mem;

	platform_set_drvdata(pdev, NULL);

	p4a_i2c_disable(i2c_adap);
	i2c_del_adapter(&i2c_adap->adapter);
	free_irq(i2c_adap->irq, i2c_adap);

	iounmap(i2c_adap->base);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(mem->start, resource_size(mem));
	clk_put(i2c_adap->clk);

	kfree(i2c_adap);

	return 0;
}

#ifdef CONFIG_PM
static int p4a_i2c_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct p4a_i2c *i2c = platform_get_drvdata(pdev);

	clk_disable(i2c->clk);

	return 0;
}

static int p4a_i2c_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct p4a_i2c *i2c = platform_get_drvdata(pdev);

	clk_enable(i2c->clk);

	return 0;
}

static struct dev_pm_ops p4a_i2c_pm_ops = {
	.suspend = p4a_i2c_suspend,
	.resume = p4a_i2c_resume,
};

#define P4A_I2C_PM_OPS (&p4a_i2c_pm_ops)
#else
#define P4A_I2C_PM_OPS NULL
#endif

static struct platform_driver p4a_i2c_driver = {
	.probe		= p4a_i2c_probe,
	.remove		= p4a_i2c_remove,
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
		.pm		= P4A_I2C_PM_OPS,
	},
};

static int __init  p4a_i2c_init_driver(void)
{
	return platform_driver_register(&p4a_i2c_driver);
}

static void __exit  p4a_i2c_exit_driver(void)
{
	platform_driver_unregister(&p4a_i2c_driver);
}

subsys_initcall(p4a_i2c_init_driver);
module_exit(p4a_i2c_exit_driver);

MODULE_AUTHOR("liuge@innofidei.com");
MODULE_DESCRIPTION("P4A I2C bus adapter");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:p4a_i2c");
