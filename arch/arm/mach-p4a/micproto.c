/*
 * linux/arch/arm/mach-p4a/micproto.c
 *
 *  Copyright (C) 2012 Innofidei Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/cache.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#include <asm/cacheflush.h>
#include <linux/dma-mapping.h>

#include <mach/p4a_mailbox.h>
#include <mach/micproto.h>


#define MICP_VER		"1.0.1"

/*

     |<--- mreserved_memsize--->|

              CPU1                  CPU2
-----|--------------------|--------------------|
...  |               |    |               |    |
-----|--------------------|--------------------|
           ^            ^         ^          ^
       extra pool  mreqb pool  extra pool  mreqb pool
     ^
     mreserved_memphys

*/

#define _ALIGN_UP(x, size)			(((x)+((size)-1)) & (~((size)-1)))
#define _ALIGN_DOWN(x, size)		((x) & (~((size)-1)))

#define WORD_SIZE					sizeof(unsigned long)
#define WORD_ALIGN_UP(x)			_ALIGN_UP(x, WORD_SIZE)
#define WORD_ALIGN_DOWN(x)			_ALIGN_DOWN(x, WORD_SIZE)
#define DWORD_SIZE					(WORD_SIZE << 1)


static struct p4a_mbox *mbox;
static int micproto_ready;

static phys_addr_t mreserved_memphys = CONFIG_MICPROTO_UNCACHED_MEM_PHYS;
static size_t	mreserved_memsize = CONFIG_MICPROTO_UNCACHED_MEM_SIZE;
static void __iomem* mreserved_membase;

/*
 * Pick out the reserved memory size.  We look for mem=size@start,
 * where start and size are "size[KkMm]"
 */
static int __init early_p4a_mreserved_mem(char *p)
{
	unsigned long size, start;
	char *endp;

	start = CONFIG_MICPROTO_UNCACHED_MEM_PHYS;

	size = memparse(p, &endp);
	if (*endp == '@')
		start = memparse(endp + 1, NULL);

	mreserved_memphys = start;
	mreserved_memsize = size;

	return 0;
}

early_param("mbox_mem", early_p4a_mreserved_mem);

/* change reserved memory address from physical to virtual */
static inline void * mreserved_mem_phys_to_virt(phys_addr_t paddr)
{
	BUG_ON(paddr < mreserved_memphys || paddr >= (mreserved_memphys + mreserved_memsize));

	return (void*)((unsigned long)paddr - mreserved_memphys + (unsigned long)mreserved_membase);
}

/* change reserved memory address from virtual to physical */
static inline phys_addr_t mreserved_mem_virt_to_phys(void * vaddr)
{
	BUG_ON(vaddr < mreserved_membase || vaddr >= (mreserved_membase + mreserved_memsize));

	return (phys_addr_t)((unsigned long)vaddr - (unsigned long)mreserved_membase + mreserved_memphys);
}

static inline mbox_msg_t mreqb_to_mbox_msg(struct mreqb* mreqb)
{
	return (mbox_msg_t)(mreserved_mem_virt_to_phys((void*)mreqb));
}

static inline struct mreqb* mbox_msg_to_mreqb(mbox_msg_t msg)
{
	return (struct mreqb *)(mreserved_mem_phys_to_virt((phys_addr_t)msg));
}

#ifdef CONFIG_DEBUG_FS
static struct runtime_statistics {
	unsigned long alloc_count;
	unsigned long free_count;
	unsigned long submit_count;
	unsigned long complete_count;
	unsigned long receive_count;
	unsigned long giveback_count;
} debug_statis;

static inline void inc_mreqb_alloc_count(void) { debug_statis.alloc_count++; }
static inline void inc_mreqb_free_count(void) {	debug_statis.free_count++; }
static inline void inc_mreqb_submit_count(void) { debug_statis.submit_count++; }
static inline void inc_mreqb_complete_count(void) { debug_statis.complete_count++; }
static inline void inc_mreqb_receive_count(void) { debug_statis.receive_count++; }
static inline void inc_mreqb_giveback_count(void) { debug_statis.giveback_count++; }
#else
static inline void inc_mreqb_alloc_count(void){}
static inline void inc_mreqb_free_count(void){}
static inline void inc_mreqb_submit_count(void){}
static inline void inc_mreqb_complete_count(void){}
static inline void inc_mreqb_receive_count(void){}
static inline void inc_mreqb_giveback_count(void){}
#endif

static int _wait_event(wait_queue_head_t *wq, unsigned int timeout)
{
	DEFINE_WAIT(wait);
	int ret = 0;

	if (timeout) {
		long __timeout = msecs_to_jiffies(timeout);
		prepare_to_wait(wq, &wait, TASK_UNINTERRUPTIBLE);
		ret = schedule_timeout(__timeout);
		finish_wait(wq, &wait);
	} else {
		prepare_to_wait(wq, &wait, TASK_UNINTERRUPTIBLE);
		schedule();
		finish_wait(wq, &wait);
	}

	return ret;
}

/*---------noncached buffer pool (used to alloc mreqb extra data) ---------------*/
#ifndef CONFIG_GENERIC_ALLOCATOR

#define LIST_NODE_SIZE				WORD_ALIGN_UP(sizeof(struct list_head))
#define LIST_NODE_ALIGN(nSize)		(((nSize) + LIST_NODE_SIZE - 1) & ~(LIST_NODE_SIZE - 1))
#define IS_FREE(nSize)				(((nSize) & (WORD_SIZE - 1)) == 0)
#define GET_CHUNK_SIZE(chunk)		((chunk)->curr_size & ~(WORD_SIZE - 1))
#define GET_PREVCHUNK_SIZE(chunk)	((chunk)->prev_size & ~(WORD_SIZE - 1))

struct mem_chunk {
	unsigned int prev_size;
	unsigned int curr_size;
	struct list_head node;
};

struct mem_pool {
	void *base;
	unsigned int len;
	struct list_head free_list;
	spinlock_t	lock;
	wait_queue_head_t wq;
};

static struct mem_pool thepool;

static inline struct mem_chunk *get_successor_chunk(struct mem_chunk *p)
{
	return (struct mem_chunk *)((unsigned long)p + DWORD_SIZE + GET_CHUNK_SIZE(p));
}

static inline struct mem_chunk *get_predeccessor_chunk(struct mem_chunk *p)
{
	return (struct mem_chunk *)((unsigned long)p - GET_PREVCHUNK_SIZE(p) - DWORD_SIZE);
}

static inline void set_chunk_size(struct mem_chunk *p, unsigned long size)
{
	struct mem_chunk *succ;

	p->curr_size = size;

	succ = get_successor_chunk(p);
	succ->prev_size = size;
}

static int extra_data_pool_init(void *base, unsigned long len)
{
	unsigned long start, end;
	struct mem_chunk *pFirst, *pTail;

	printk("nocached buffer range : [%08lx, %08lx)\n", (unsigned long)base, (unsigned long)base + len);

	thepool.base = base;
	thepool.len = len;
	INIT_LIST_HEAD(&thepool.free_list);
	spin_lock_init(&thepool.lock);
	init_waitqueue_head(&thepool.wq);

	spin_lock(&thepool.lock);
	start = WORD_ALIGN_UP((unsigned long)base);
	end   = WORD_ALIGN_DOWN((unsigned long)base + len);

	pFirst = (struct mem_chunk *)start;
	pTail  = (struct mem_chunk *)(end - DWORD_SIZE);

	pFirst->prev_size = 1;
	pFirst->curr_size = (unsigned long)pTail - (unsigned long)pFirst - DWORD_SIZE;

	pTail->prev_size = pFirst->curr_size;
	pTail->curr_size = 1;

	list_add_tail(&pFirst->node, &thepool.free_list);
	spin_unlock(&thepool.lock);

	return 0;
}

static void *extra_data_pool_alloc(unsigned long size)
{
	void *p = NULL;
	unsigned long alloc_size, rest_size;
	struct mem_chunk *curr, *succ;

	spin_lock(&thepool.lock);
	alloc_size = LIST_NODE_ALIGN(size);
	list_for_each_entry(curr, &thepool.free_list, node) {
		if (curr->curr_size >= alloc_size)
			goto do_alloc;
	}
	spin_unlock(&thepool.lock);

	return NULL;

do_alloc:
	list_del(&curr->node);

	rest_size = curr->curr_size - alloc_size;

	if (rest_size < sizeof(struct mem_chunk)) {
		set_chunk_size(curr, curr->curr_size | 1);
	} else {
		set_chunk_size(curr, alloc_size | 1);

		succ = get_successor_chunk(curr);
		set_chunk_size(succ, rest_size - DWORD_SIZE);

		list_add_tail(&succ->node, &thepool.free_list);
	}

	p = &curr->node;
	spin_unlock(&thepool.lock);

	return p;
}

static void extra_data_pool_free(void *p, int size)
{
	struct mem_chunk *curr, *succ;

	spin_lock(&thepool.lock);

	curr = (struct mem_chunk *)((unsigned long)p - DWORD_SIZE);
	succ = get_successor_chunk(curr);

	if (IS_FREE(succ->curr_size)) {
		set_chunk_size(curr, GET_CHUNK_SIZE(curr) + succ->curr_size + DWORD_SIZE);
		list_del(&succ->node);
	} else {
		set_chunk_size(curr, GET_CHUNK_SIZE(curr));
	}

	if (IS_FREE(curr->prev_size)){
		struct mem_chunk *prev;

		prev = get_predeccessor_chunk(curr);
		set_chunk_size(prev, prev->curr_size + curr->curr_size + DWORD_SIZE);
	} else {
		list_add_tail(&curr->node, &thepool.free_list);
	}

	wake_up(&thepool.wq);

	spin_unlock(&thepool.lock);
}

static int extra_data_pool_wait_space(unsigned int timeout)
{
	return _wait_event(&thepool.wq, timeout);
}
#else
#include <linux/genalloc.h>
static struct extra_buf_heap {
	struct gen_pool *pool;
	void *base;
	size_t	size;
	wait_queue_head_t wq;

} eheap;

static int extra_data_pool_init(void *base, unsigned long len)
{
	int ret;

	printk("nocached buffer range : [%08lx, %08lx)\n", (unsigned long)base, (unsigned long)base + len);

	init_waitqueue_head(&eheap.wq);

	eheap.pool = gen_pool_create(2, -1);
	if (!eheap.pool) {
		return -ENOMEM;
	}

	eheap.base = base;
	eheap.size = len;

	ret = gen_pool_add(eheap.pool, (unsigned long)eheap.base, eheap.size, 0);

	return ret;
}

static void *extra_data_pool_alloc(unsigned long size)
{
	unsigned long offset;

	offset = gen_pool_alloc(eheap.pool, size);

	return (void*)offset;
}

static void extra_data_pool_free(void *p, int size)
{
	gen_pool_free(eheap.pool, (unsigned long)p, size);

	wake_up(&eheap.wq);
}

static int extra_data_pool_wait_space(unsigned int timeout)
{
	return _wait_event(&eheap.wq, timeout);
}
#endif


/*------- MREQB POOL (used to  alloc mreqb structure ) -----------------*/
struct mreqb_memnode {
	struct mreqb_memnode *next;
};

static struct mreqb_memnode* mreqb_memnode_free;
static spinlock_t mreqb_pool_lock;
static wait_queue_head_t mreqb_pool_free_event;

static struct mreqb *__mreqb_alloc(void)
{
	struct mreqb_memnode *m;
	void *mem = NULL;

	m = mreqb_memnode_free;
	if (m != NULL) {
		mreqb_memnode_free = m->next;
		m->next = NULL;

		mem = (unsigned char *)m + sizeof(struct mreqb_memnode);
	}

	return (struct mreqb *)mem;
}

static void __mreqb_free(struct mreqb *mreqb)
{
	struct mreqb_memnode *m;

	m = (struct mreqb_memnode*)((unsigned char *)mreqb - sizeof(struct mreqb_memnode));

	m->next = mreqb_memnode_free;
	mreqb_memnode_free = m;

	if (m->next == NULL) {
		wake_up(&mreqb_pool_free_event);
	}
}

static int mreqb_pool_init(void *base, unsigned long len)
{
	unsigned char *mreqb_memnode_memory;
	unsigned short mreqb_memnode_num;
	struct mreqb_memnode *m, *chunk;
	unsigned short size;
	unsigned long flags;
	int i;

	printk("mreqb pool range : [%08lx, %08lx)\n", (unsigned long)base, (unsigned long)base + len);
	spin_lock_init(&mreqb_pool_lock);
	init_waitqueue_head(&mreqb_pool_free_event);

	size = WORD_ALIGN_UP(sizeof(struct mreqb) + sizeof(struct mreqb_memnode));
	mreqb_memnode_memory = (unsigned char *)base;
	mreqb_memnode_num = len / size;

	printk("mreqb_memnode_memory = %p, mreqb_memnode_num = %d\n",
			mreqb_memnode_memory, mreqb_memnode_num);

	spin_lock_irqsave(&mreqb_pool_lock, flags);

	chunk = (struct mreqb_memnode *)mreqb_memnode_memory;
	mreqb_memnode_free = chunk;
	m = chunk;

	for (i = 0; i < mreqb_memnode_num; i++) {
		m->next = (struct mreqb_memnode *)((unsigned char *)m + size);
		chunk = m;
		m = m->next;
	}
	chunk->next = NULL;

	spin_unlock_irqrestore(&mreqb_pool_lock, flags);

	return 0;
}

/*------------------------- MREQB ALLOC/FREE MANAGEMENT ---------------------------------*/

/**
 * @brief allocate a mailbox request block
 *
 * @param[in] extra_data_size : if > 0, allocate more data
 *
 * @return : a valid mreqb pointer
 */
struct mreqb *mreqb_alloc(int extra_data_size)
{
	struct mreqb *req;
	unsigned long flags;

	BUG_ON(!micproto_ready);

__retry_alloc_mreqb:
	spin_lock_irqsave(&mreqb_pool_lock, flags);
	req = __mreqb_alloc();
	spin_unlock_irqrestore(&mreqb_pool_lock, flags);
	if (req == NULL) {
		printk("%s alloc mreqb need wait!\n", __FUNCTION__);
		_wait_event(&mreqb_pool_free_event, 0);

		goto __retry_alloc_mreqb;
	}

	memset((void *)req, 0, sizeof(struct mreqb));

_retry_alloc_extra_data:
	if (extra_data_size > 0) {
		req->extra_data = extra_data_pool_alloc(extra_data_size);
		if (req->extra_data == NULL) {
			extra_data_pool_wait_space(0);

			goto _retry_alloc_extra_data;
		}

		req->extra_data_phys = mreserved_mem_virt_to_phys(req->extra_data);
		req->extra_data_size = extra_data_size;
	}

	req->magic = MREQB_MAGIC;
	INIT_LIST_HEAD(&req->node);

	inc_mreqb_alloc_count();

	return req;
}

void mreqb_reinit(struct mreqb* mreqb)
{
	void *extra_data = mreqb->extra_data;
	phys_addr_t extra_data_phys = mreqb->extra_data_phys;
	unsigned int extra_data_size = mreqb->extra_data_size;

	memset((void *)mreqb, 0, sizeof(struct mreqb));

	mreqb->extra_data = extra_data;
	mreqb->extra_data_phys = extra_data_phys;
	mreqb->extra_data_size = extra_data_size;

	mreqb->magic = MREQB_MAGIC;
	INIT_LIST_HEAD(&mreqb->node);
}

/**
 * @brief free the memory of mailbox request block
 *
 * @param[in] mreqb : the mailbox requet block to be free
 *
 * @return : void
 */
void mreqb_free(struct mreqb *mreqb)
{
	unsigned long flags;

	spin_lock_irqsave(&mreqb_pool_lock, flags);

	if (mreqb->extra_data_size > 0 && mreqb->extra_data != NULL) {
		extra_data_pool_free(mreqb->extra_data, mreqb->extra_data_size);
		mreqb->extra_data = NULL;
		mreqb->extra_data_phys = 0;
		mreqb->extra_data_size = 0;
	}

	__mreqb_free(mreqb);

	spin_unlock_irqrestore(&mreqb_pool_lock, flags);

	inc_mreqb_free_count();
}

/*------------------------- MREQB TRANSFER MANAGEMENT ---------------------------------*/

/**
 * @brief submit a mailbox request
 *
 * @param[in] mreqb : mailbox request block
 *
 * @return : if success return 0, otherwise return a negative error code
 */
int mreqb_submit(struct mreqb *mreqb)
{
	int ret;

	if (mreqb->magic != MREQB_MAGIC) {
		printk(KERN_ERR "%s: Invalid Message!, mreqb %p magic %x\n", \
					__FUNCTION__, mreqb, mreqb->magic);
		return -EINVAL;
	}

	BUG_ON(mreqb_is_response(mreqb));		// response send back by mreqb_giveback().


	ret = p4a_mbox_msg_send(mbox, mreqb_to_mbox_msg(mreqb));
	if (ret)
		return ret;
	
	inc_mreqb_submit_count();

	return 0;
}


struct __sync_mreqb_context {
	struct completion done;
	int status;
};

static void __mreqb_blocking_completion(struct mreqb *mreqb)
{
	struct __sync_mreqb_context *ctx = mreqb->context;

	ctx->status = mreqb->result;
	complete(&ctx->done);
}

/**
 * @brief send and also wait mailbox request to complete
 *
 * @param[in] mreqb : mailbox request block
 * @param[in] timeout : maximum time to wait complete, unit:milliseconds
 *
 * @return : if timeout return -ETIMEDOUT, otherwise return mailbox request handler result.
 */
int mreqb_submit_and_wait(struct mreqb *mreqb, int timeout)
{
	struct __sync_mreqb_context ctx;
	int ret;

	init_completion(&ctx.done);

	mreqb->complete = __mreqb_blocking_completion;
	mreqb->context = &ctx;

	ret = mreqb_submit(mreqb);

	if (timeout) {
		unsigned long __timeout = msecs_to_jiffies(timeout);
		ret = wait_for_completion_timeout(&ctx.done, __timeout);
		if (ret == 0)
			return -ETIMEDOUT;
	} else {
		wait_for_completion(&ctx.done);
	}

	return ctx.status;
}

void mreqb_completion_free_mreqb(struct mreqb *mreqb)
{
	mreqb_free(mreqb);
}

/**
 * @brief after handle the mailbox request, call this function to complete
 *
 * @param[in] mreqb : mailbox request block to complete
 * @param[in] status : mailbox request handle result to return
 *
 * @return : if success return 0, otherwise return a negative error code.
 *
 */
int mreqb_giveback(struct mreqb *mreqb, int status)
{
	int ret;

	mreqb->result = status;
	mreqb->cmd |= RESPONSE_BIT;

	ret = p4a_mbox_msg_send(mbox, mreqb_to_mbox_msg(mreqb));
	if (ret)
		return ret;

	inc_mreqb_giveback_count();

	return 0;
}

/*------------------------- MREQB HANDLE MANAGEMENT ---------------------------------*/
#undef MICP_SCALLM_CMD
#undef MICP_MCALLS_CMD
#include <mach/micp_cmd.h>


#define MICP_MCALLS_CMD(symbol, func) \
	[C_##symbol] = {func, NULL},

#define MICP_SCALLM_CMD(symbol, func) \
	[C_##symbol] = {0},

#define MICP_BOTHCALL_CMD(symbol, func) \
	[C_##symbol] = {func, NULL},

static struct cmd_entry {
	cmd_func_t func;
	void *data;
} micp_cmd_info[MICP_CMD_COUNT] = {
	[C_UNKNOWN] = {0},
#include <mach/micp_cmd.h>
};
#undef MICP_SCALLM_CMD
#undef MICP_MCALLS_CMD
#undef MICP_BOTHCALL_CMD

/**
 * @brief : register a mailbox request command handler
 *
 * @param[in] : the command ID to register
 * @param[in] : registered command handle function
 * @param[in] : private data pass to command handle function when execute
 *
 * @return : if registered successful return 0, otherwise return a negative error code.
 */
int mreqb_register_cmd_handler(int cmd, cmd_func_t func, void *data)
{
	if (cmd >= 0 && cmd < MICP_CMD_COUNT) {
		micp_cmd_info[cmd].func = func;
		micp_cmd_info[cmd].data = data;
		return 0;
	}

	return -EINVAL;
}

static int mbox_rx_notifier(struct notifier_block *nb, unsigned long len, void *msg)
{
	struct mreqb *mreqb;
	struct cmd_entry *handler;
	int ret;

	mreqb = mbox_msg_to_mreqb((mbox_msg_t)msg);

	// sanity check
	if (mreqb->magic != MREQB_MAGIC) {
		printk(KERN_ERR "%s: Receive Invalid Message!, mreqb %p magic %x\n", \
					__FUNCTION__, mreqb, mreqb->magic);
		return -EINVAL;
	}

	if (mreqb_is_response(mreqb)) {
		pr_debug("%s: recved request is a response, to complete it!\n", __FUNCTION__);

		inc_mreqb_complete_count();

		if (mreqb->complete != NULL)
			mreqb->complete(mreqb);

		return 0;
	}
	
	inc_mreqb_receive_count();
	
	if ((mreqb->cmd < 0) || (mreqb->cmd >= MICP_CMD_COUNT)) {
		ret = -EINVAL;
		goto error_nohandler;
	}

	handler = &micp_cmd_info[mreqb->cmd]; 

	if (handler->func) {
		if (mreqb->extra_data_phys != 0)
			mreqb->extra_data = mreserved_mem_phys_to_virt(mreqb->extra_data_phys);

		ret = handler->func(mreqb, handler->data);

		if (MREQB_RET_PENDING != ret)
			mreqb_giveback(mreqb, ret);

	} else {
		ret = -ENODEV;
		goto error_nohandler;
	}

	return 0;

error_nohandler:
	mreqb_giveback(mreqb, MREQB_RET_NOHANDLE);

	return ret;
}


#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>

static struct dentry *debugfs_root;

static int statistics_show(struct seq_file *s, void *unused)
{
	seq_printf(s, "micproto statistics:\n\n");

	seq_printf(s, "mreqb alloc count :\t%lu\n", debug_statis.alloc_count);
	seq_printf(s, "mreqb free count :\t%lu\n\n", debug_statis.free_count);

	seq_printf(s, "mreqb submit count :\t%lu\n", debug_statis.submit_count);
	seq_printf(s, "mreqb complete count :\t%lu\n\n", debug_statis.complete_count);

	seq_printf(s, "mreqb receive count :\t%lu\n", debug_statis.receive_count);
	seq_printf(s, "mreqb give back count :\t%lu\n\n", debug_statis.giveback_count);
	return 0;
}

static int statistics_open(struct inode *inode, struct file *file)
{
	return single_open(file, statistics_show, inode->i_private);
}

static const struct file_operations statistics_fops = {
	.open           = statistics_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int __init micproto_debugfs_init(void)
{
	struct dentry       *root;
	struct dentry       *file;
	int ret;

	root = debugfs_create_dir("micproto", NULL);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto err0;
	}


	file = debugfs_create_file("statistics", S_IRUSR, root, NULL, &statistics_fops);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto err1;
	}

	debugfs_root = root;
	return 0;

err1:
	debugfs_remove_recursive(root);
err0:
	return ret;

}
#else
static inline int __init micproto_debugfs_init(void) {return 0;}
#endif	/* CONFIG_DEBUG_FS */


static struct notifier_block mbox_nb = {
	.notifier_call	= mbox_rx_notifier,
};

static int micproto_mreqb_buffer_init(void)
{
	phys_addr_t	mreqb_pool_start, extra_data_pool_start;
	void *mreqb_pool_base, *extra_data_pool_base;
	size_t mreqb_pool_size, extra_data_pool_size;
	int ret;

	if (mreserved_memphys == 0) {
		printk(KERN_ERR "mailbox reserved memory region not specified!\n");
		return -ENOMEM;
	}

	printk(KERN_INFO "mailbox reserved memory :  [%x, %x)\n", mreserved_memphys, mreserved_memphys + mreserved_memsize);

	mreserved_membase = ioremap(mreserved_memphys, mreserved_memsize);
	if (mreserved_membase == NULL)
		return -ENOMEM;

	/* the bottom half memory for our used */
	extra_data_pool_start = mreserved_memphys + (mreserved_memsize >> 1);
	extra_data_pool_size = ((mreserved_memsize >> 1) * 3 / 4);

	mreqb_pool_start = extra_data_pool_start + extra_data_pool_size;
	mreqb_pool_size = (mreserved_memsize >> 1) - extra_data_pool_size;

	extra_data_pool_base = mreserved_mem_phys_to_virt(extra_data_pool_start);
	mreqb_pool_base = mreserved_mem_phys_to_virt(mreqb_pool_start);

	/* init mreqb extra data buffer manage */
	ret = extra_data_pool_init(extra_data_pool_base, extra_data_pool_size);
	if (ret)
		return ret;

	/* init mreqb buffer manage */
	ret = mreqb_pool_init(mreqb_pool_base, mreqb_pool_size);
	
	return ret;
}


int __init micproto_init(void)
{
	int ret;

	printk("Mailbox Protocol, v%s\n", MICP_VER);

	mbox = p4a_mbox_get("CPU1", &mbox_nb); 
	if (IS_ERR(mbox)) {
		ret = PTR_ERR(mbox);
		mbox = NULL;
		goto error;
	}

	ret = micproto_mreqb_buffer_init();
	if (ret) {
		goto error;
	}

	micproto_debugfs_init();

	micproto_ready = 1;

	return 0;

error:
	if (mbox) {
		p4a_mbox_put(mbox, &mbox_nb);
		mbox = NULL;
	}

	return ret;
}

/* micproto needs to be init before 
   machine_init function, and after p4a_mbox_register,
   so micproto_init is a postcore_initcall_sync.
 */
postcore_initcall_sync(micproto_init);
