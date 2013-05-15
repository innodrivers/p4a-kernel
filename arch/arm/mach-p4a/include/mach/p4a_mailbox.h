#ifndef _P4A_MAILBOX_H
#define _P4A_MAILBOX_H
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/kfifo.h>

typedef u32 mbox_msg_t;
struct p4a_mbox;

typedef int __bitwise p4a_mbox_irq_t;
#define IRQ_TX		((__force p4a_mbox_irq_t) 1)
#define IRQ_RX		((__force p4a_mbox_irq_t) 2)

struct p4a_mbox_ops {
	int (*startup)(struct p4a_mbox *);
	int (*shutdown)(struct p4a_mbox *);

	mbox_msg_t (*fifo_read)(struct p4a_mbox *);
	void (*fifo_write)(struct p4a_mbox *, mbox_msg_t);
	int (*fifo_empty)(struct p4a_mbox *);
	int (*fifo_full)(struct p4a_mbox *);

	void (*enable_irq)(struct p4a_mbox *, p4a_mbox_irq_t);
	void (*disable_irq)(struct p4a_mbox *, p4a_mbox_irq_t);
	void (*ack_irq)(struct p4a_mbox *, p4a_mbox_irq_t);
	int (*is_irq)(struct p4a_mbox *, p4a_mbox_irq_t);
};

struct p4a_mbox_queue {
	spinlock_t	lock;
	struct kfifo	fifo;
	struct work_struct work;
	struct tasklet_struct tasklet;
	struct p4a_mbox	*mbox;
	int full;
};

struct p4a_mbox_fifo {
	unsigned long msg;
	unsigned long fifo_stat;
	unsigned long msg_stat;
};

struct p4a_mbox_priv {
	struct p4a_mbox_fifo tx_fifo;
	struct p4a_mbox_fifo rx_fifo;
	unsigned long threshold;
	unsigned long int_stat;
	unsigned long int_mask;
	unsigned long rawint_stat;

	u32 notfull_bit;
	u32 newmsg_bit;
};

struct p4a_mbox {
	char *name;

	int irq;
	void __iomem *base;
	struct clk* clk;
	struct device *dev;

	struct p4a_mbox_ops	*ops;	
	struct p4a_mbox_queue *txq, *rxq;
	void *priv;
	int use_count;
	struct blocking_notifier_head	notifier;
};

extern int p4a_mbox_msg_send(struct p4a_mbox *mbox, mbox_msg_t msg);
extern struct p4a_mbox * p4a_mbox_get(const char* name, struct notifier_block *nb);
extern void p4a_mbox_put(struct p4a_mbox *mbox, struct notifier_block *nb);
extern int p4a_mbox_register(struct device *parent, struct p4a_mbox **list);

#endif	/* _P4A_MAILBOX_H */
