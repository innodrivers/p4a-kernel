/*
 * linux/arch/arm/mach-p4a/mailbox.c
 *
 *  modify from omap2 mailbox
 *
 *  Copyright (C) 2013 Innofidei Inc.
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/kfifo.h>


#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/p4a-regs.h>
#include <mach/p4a_mailbox.h>

#define MESSAGE_TYPE_EVENT		(0x00)
#define MESSAGE_TYPE_MSG		(0x01)
#define MESSAGE_TYPE_MSK		(0x01 << 31)

#define CHIP_TYPE_DSP			(0x00)
#define CHIP_TYPE_ARM_M			(0x01)
#define CHIP_TYPE_ARM_S			(0x02)
#define CHIP_TYPE_MSK			(0x03 << 29)

#define MEMERY_TYPE_DDR		(0x01)
#define MEMERY_TYPE_OTH		(0x00)
#define MEMERY_TYPE_MSK		(0x01 << 28)

#define MESSAGE_HEAD_MSK		(0x0F << 28)

#ifdef CONFIG_P4A_CPU1
#define MESSAGE_HEAD			((MESSAGE_TYPE_MSG << 31) | (CHIP_TYPE_ARM_M << 29))
#elif defined(CONFIG_P4A_CPU2)
#define MESSAGE_HEAD			((MESSAGE_TYPE_MSG << 31) | (CHIP_TYPE_ARM_S << 29))
#endif

#define CONVER_TO_ADDR(msg)		(((msg) & (~MESSAGE_HEAD_MSK)) + 0x40000000)
#define CONVER_TO_MSG(addr)		(((addr) & (~MESSAGE_HEAD_MSK)) + MESSAGE_HEAD)

static inline mbox_msg_t out_msg_fixup(mbox_msg_t msg)
{
	//return CONVER_TO_MSG(msg);
	return msg;
}

static inline mbox_msg_t in_msg_fixup(mbox_msg_t msg)
{
	return msg;
	//return CONVER_TO_ADDR(msg);
}

/*-------------------------------------------------------------*/
static struct workqueue_struct *mboxd;
static struct p4a_mbox **mboxes;
static unsigned int mbox_kfifo_size = 512;

static struct class p4a_mbox_class = {
	.name = "mbox",
};

static inline mbox_msg_t mbox_fifo_read(struct p4a_mbox *mbox)
{
	return mbox->ops->fifo_read(mbox);
}

static inline void mbox_fifo_write(struct p4a_mbox *mbox, mbox_msg_t msg)
{
	mbox->ops->fifo_write(mbox, msg);
}

static inline int mbox_fifo_empty(struct p4a_mbox *mbox)
{
	return mbox->ops->fifo_empty(mbox);
}

static inline int mbox_fifo_full(struct p4a_mbox *mbox)
{
	return mbox->ops->fifo_full(mbox);
}

static inline void mbox_ack_irq(struct p4a_mbox *mbox, p4a_mbox_irq_t irq)
{
	if (mbox->ops->ack_irq)
		mbox->ops->ack_irq(mbox, irq);
}

static inline void mbox_enable_irq(struct p4a_mbox *mbox, p4a_mbox_irq_t irq)
{
	if (mbox->ops->enable_irq)
		mbox->ops->enable_irq(mbox, irq);
}

static inline void mbox_disable_irq(struct p4a_mbox *mbox, p4a_mbox_irq_t irq)
{
	if (mbox->ops->disable_irq)
		mbox->ops->disable_irq(mbox, irq);
}

static inline int mbox_is_irq(struct p4a_mbox *mbox, p4a_mbox_irq_t irq)
{
	return mbox->ops->is_irq(mbox, irq);
}

static void __mbox_rx_interrupt(struct p4a_mbox *mbox)
{
	struct p4a_mbox_queue *mq = mbox->rxq;
	mbox_msg_t msg;
	int len;

	while (!mbox_fifo_empty(mbox)) {
		if (unlikely(kfifo_avail(&mq->fifo) < sizeof(msg))) {
			mbox_disable_irq(mbox, IRQ_RX);
			mq->full = 1;
			goto nomem;
		}

		msg = in_msg_fixup(mbox_fifo_read(mbox));

		len = kfifo_in(&mq->fifo, (unsigned char*)&msg, sizeof(msg));
		WARN_ON(len != sizeof(msg));
	}

	mbox_ack_irq(mbox, IRQ_RX);
nomem:
	queue_work(mboxd, &mbox->rxq->work);
}

static void __mbox_tx_interrupt(struct p4a_mbox* mbox)
{
	mbox_disable_irq(mbox, IRQ_TX);
	mbox_ack_irq(mbox, IRQ_TX);
	tasklet_schedule(&mbox->txq->tasklet);
}

static irqreturn_t mbox_interrupt(int irq, void *p)
{
	struct p4a_mbox *mbox = p;

	if (mbox_is_irq(mbox, IRQ_RX)) {
		__mbox_rx_interrupt(mbox);
	}

	if (mbox_is_irq(mbox, IRQ_TX)) {
		__mbox_tx_interrupt(mbox);
	}

	return IRQ_HANDLED;
}

static void mbox_rx_work(struct work_struct *work)
{
	struct p4a_mbox_queue *mq = container_of(work, struct p4a_mbox_queue, work);
	mbox_msg_t msg;
	int len;

	while (kfifo_len(&mq->fifo) >= sizeof(msg)) {
		len = kfifo_out(&mq->fifo, (unsigned char*)&msg, sizeof(msg));
		WARN_ON(len != sizeof(msg));

		blocking_notifier_call_chain(&mq->mbox->notifier, len, (void*)msg);

		if (mq->full) {
			spin_lock_irq(&mq->lock);

			mq->full = 0;
			mbox_enable_irq(mq->mbox, IRQ_RX);

			spin_unlock_irq(&mq->lock);
		}
	}
}

static void mbox_tx_tasklet(unsigned long data)
{
	struct p4a_mbox *mbox = (struct p4a_mbox*)data;
	struct p4a_mbox_queue *mq = mbox->txq;
	mbox_msg_t msg;
	int ret;

	while (kfifo_len(&mq->fifo)) {
		if (mbox_fifo_full(mbox)) {
			mbox_enable_irq(mbox, IRQ_TX);
			break;
		}

		ret = kfifo_out(&mq->fifo, (unsigned char*)&msg, sizeof(msg));
		WARN_ON(ret != sizeof(msg));

		mbox_fifo_write(mbox, msg);
	}
}

static struct p4a_mbox_queue *mbox_queue_alloc(struct p4a_mbox *mbox,
			void (*work)(struct work_struct *),
			void (*tasklet)(unsigned long))
{
	struct p4a_mbox_queue *mq;

	mq = kzalloc(sizeof(struct p4a_mbox_queue), GFP_KERNEL);
	if (!mq)
		return NULL;

	spin_lock_init(&mq->lock);
	if (kfifo_alloc(&mq->fifo, mbox_kfifo_size, GFP_KERNEL))
		goto error;

	if (work)
		INIT_WORK(&mq->work, work);

	if (tasklet)
		tasklet_init(&mq->tasklet, tasklet, (unsigned long)mbox);

	return mq;

error:
	kfree(mq);
	return NULL;
}

static void mbox_queue_free(struct p4a_mbox_queue *q)
{
	kfifo_free(&q->fifo);
	kfree(q);
}

static int p4a_mbox_startup(struct p4a_mbox *mbox)
{
	int ret = 0;
	struct p4a_mbox_queue *mq;


	if (!mbox->use_count++) {
		clk_enable(mbox->clk);

		if (mbox->ops->startup) {
			ret = mbox->ops->startup(mbox);
			if (ret) {
				goto fail_ops_startup;
			}
		}

		ret = request_irq(mbox->irq, mbox_interrupt, IRQF_SHARED, "mailbox", mbox);
		if (ret) {
			printk(KERN_ERR "failed to register mailbox interrupt:%d\n", ret);
			goto fail_request_irq;
		}

		mq = mbox_queue_alloc(mbox, NULL, mbox_tx_tasklet);
		if (!mq) {
			ret = -ENOMEM;
			goto fail_alloc_txq;
		}
		mbox->txq = mq;

		mq = mbox_queue_alloc(mbox, mbox_rx_work, NULL);
		if (!mq) {
			ret = -ENOMEM;
			goto fail_alloc_rxq;
		}
		mbox->rxq = mq;

		mq->mbox = mbox;

		mbox_enable_irq(mbox, IRQ_RX);
	}

	return 0;

fail_alloc_rxq:
	mbox_queue_free(mbox->txq);
fail_alloc_txq:
	free_irq(mbox->irq, mbox);
fail_request_irq:
	if (mbox->ops->shutdown)
		mbox->ops->shutdown(mbox);
fail_ops_startup:
	mbox->use_count--;

	return ret;
}

static void p4a_mbox_shutdown(struct p4a_mbox *mbox)
{
	if (!--mbox->use_count) {
		mbox_disable_irq(mbox, IRQ_RX);

		free_irq(mbox->irq, mbox);
		tasklet_kill(&mbox->txq->tasklet);
		flush_work(&mbox->rxq->work);
		mbox_queue_free(mbox->txq);
		mbox_queue_free(mbox->rxq);

		if (mbox->ops->shutdown) {
			mbox->ops->shutdown(mbox);
		}

		clk_disable(mbox->clk);
	}
}

/**
 * @brief send a message to mailbox
 *
 * @param[in] mbox : mailbox instance
 * @param[in] msg : the message to be sent
 *
 * @return : if success return 0, otherwise return a negative error code. 
 */
int p4a_mbox_msg_send(struct p4a_mbox *mbox, mbox_msg_t msg)
{
	struct p4a_mbox_queue *mq = mbox->txq;
	unsigned long flags;
	int ret = 0;
	int len;

	spin_lock_irqsave(&mq->lock, flags);

	/*
	 * kernel FIFO full, return a fail
	 */
	if (kfifo_avail(&mq->fifo) < sizeof(msg)) {
		ret = -ENOMEM;
		WARN(1, "kfifo full, msg cannot be sent!\n");
		goto out;
	}

	msg = out_msg_fixup(msg);

	/* 
	 * no msg pending in kernel FIFO and mailbox FIFO no full,
	 * in this case, write msg into mailbox FIFO directly.
	 */
	if (kfifo_is_empty(&mq->fifo) && !mbox_fifo_full(mbox)) {
		mbox_fifo_write(mbox, msg);
		goto out;
	}

	len = kfifo_in(&mq->fifo, (unsigned char*)&msg, sizeof(msg));
	WARN_ON(len != sizeof(msg));

	tasklet_schedule(&mq->tasklet);

out:
	spin_unlock_irqrestore(&mq->lock, flags);

	return ret;
}

/**
 * @brief : get a mailbox instance, and register a notifier_block (callback)
 *
 * @param[in] name : the mailbox name to get
 * @param[in] nb : the callback want to register
 *
 * @return : if success return a valid mailbox instance.
 */
struct p4a_mbox * p4a_mbox_get(const char* name, struct notifier_block *nb)
{
	struct p4a_mbox *mbox;
	int ret;

	if (!mboxes)
		return ERR_PTR(-EINVAL);

	for (mbox = *mboxes; mbox; mbox++) {
		if (!strcmp(mbox->name, name))
			break;
	}

	if (!mbox)
		return ERR_PTR(-ENOENT);

	if (nb)
		blocking_notifier_chain_register(&mbox->notifier, nb);

	ret = p4a_mbox_startup(mbox);
	if (ret) {
		blocking_notifier_chain_unregister(&mbox->notifier, nb);
		return ERR_PTR(-ENODEV);
	}

	return mbox;
}

/**
 * @brief release a mailbox 
 *
 * @param[in] mbox : the mailbox to release
 * @param[in] nb : the registered notifier_block before
 *
 * @return : void
 */
void p4a_mbox_put(struct p4a_mbox *mbox, struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&mbox->notifier, nb);
	p4a_mbox_shutdown(mbox);
}


/**
 * @brief register mailbox
 *
 * @param[in] parent : mailbox parent device
 * @param[in] list	: mailbox devices to be register
 *
 * @return : if success return 0, otherwise return a negative error code.
 */
int p4a_mbox_register(struct device *parent, struct p4a_mbox **list)
{
	int ret;
	int i;

	mboxes = list;
	if (!mboxes)
		return -EINVAL;

	for (i=0; mboxes[i]; i++) {
		struct p4a_mbox *mbox = mboxes[i];

		mbox->dev = device_create(&p4a_mbox_class, parent, 0, mbox, "%s", mbox->name);
		if (IS_ERR(mbox->dev)) {
			ret = PTR_ERR(mbox->dev);
			goto error;
		}
		BLOCKING_INIT_NOTIFIER_HEAD(&mbox->notifier);
	}

	return 0;

error:
	while (i--)
		device_unregister(mboxes[i]->dev);

	return ret;
}

EXPORT_SYMBOL(p4a_mbox_msg_send);
EXPORT_SYMBOL(p4a_mbox_get);
EXPORT_SYMBOL(p4a_mbox_put);
EXPORT_SYMBOL(p4a_mbox_register);


static int __init p4a_mbox_init(void)
{
	int ret;

	ret = class_register(&p4a_mbox_class);
	if (ret)
		return ret;

	mboxd = create_workqueue("mboxd");
	if (!mboxd)
		return -ENOMEM;

	return 0;
}

/* mailbox class register needs to be done before 
   p4a_mbox_register to be call, so p4a_mbox_init is a core_initcall.
 */
core_initcall(p4a_mbox_init);

