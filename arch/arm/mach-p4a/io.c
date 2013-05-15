/* arch/arm/mach-p4a/io.c
 *
 * Copyright (C) 2013 innofidei Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/memblock.h>

#include <mach/hardware.h>
#include <asm/page.h>
#include <asm/mach/map.h>
#include <mach/p4a-regs.h>


static unsigned long p4a_cpu1mem_phys = P4A_CPU1MEM_PHYS;
static unsigned long p4a_cpu1mem_size;

static struct map_desc p4a_cpu1mem_io_desc[] __initdata = {
	{
        .virtual    = (unsigned long)P4A_CPU1MEM_BASE,
        .pfn        = __phys_to_pfn(P4A_CPU1MEM_PHYS),
        .length     = SZ_64M,
        .type       = MT_MEMORY,
	},
};

/*      
 * P4A internal register mapping.
 */   
static struct map_desc p4a_io_desc[] __initdata = {
    {
        .virtual    = (unsigned long)P4A_PERI_BASE,
        .pfn        = __phys_to_pfn(P4A_PERI_PHYS),
        .length     = P4A_PERI_SIZE,
        .type       = MT_DEVICE,
    },
    {
        .virtual    = (unsigned long)P4A_AIC_BASE,
        .pfn        = __phys_to_pfn(P4A_AIC_PHYS),
        .length     = P4A_AIC_SIZE,
        .type       = MT_DEVICE,
    },
    {
        .virtual    = (unsigned long)P4A_GLOBAL_BASE,
        .pfn        = __phys_to_pfn(P4A_GLOBAL_PHYS),
        .length     = P4A_GLOBAL_SIZE,
        .type       = MT_DEVICE,
    },
    {
        .virtual    = (unsigned long)P4A_PMU_BASE,
        .pfn        = __phys_to_pfn(P4A_PMU_PHYS),
        .length     = P4A_PMU_SIZE,
        .type       = MT_DEVICE,
    },
    {
        .virtual    = (unsigned long)P4A_UART4W_BASE,
        .pfn        = __phys_to_pfn(P4A_UART4W_PHYS),
        .length     = P4A_UART4W_SIZE,
        .type       = MT_DEVICE,
    },
    {
        .virtual    = (unsigned long)P4A_P4TIMER_BASE,
        .pfn        = __phys_to_pfn(P4A_P4TIMER_PHYS),
        .length     = P4A_P4TIMER_SIZE,
        .type       = MT_DEVICE,
    },
};

void __init p4a_map_io(void)
{
    /* Map peripherals */
    iotable_init(p4a_io_desc, ARRAY_SIZE(p4a_io_desc));

#ifdef CONFIG_P4A_MAILBOX
	if (p4a_cpu1mem_size != 0) {
		p4a_cpu1mem_io_desc[0].pfn = __phys_to_pfn(p4a_cpu1mem_phys);
		p4a_cpu1mem_io_desc[0].length = p4a_cpu1mem_size;
	}

	iotable_init(p4a_cpu1mem_io_desc, ARRAY_SIZE(p4a_cpu1mem_io_desc));

	printk(KERN_INFO "CPU1MEM: Mapped pa 0x%08lx to va 0x%08lx size: 0x%lx\n",
				__pfn_to_phys(p4a_cpu1mem_io_desc[0].pfn),
				p4a_cpu1mem_io_desc[0].virtual,
				p4a_cpu1mem_io_desc[0].length);
#endif
}

unsigned long p4a_cpu1_mem_v2p(void *address)
{
	return (unsigned long)address - (unsigned long)P4A_CPU1MEM_BASE + p4a_cpu1mem_phys; 
}

void * p4a_cpu1_mem_p2v(unsigned long address)
{
	return (void*) (address - p4a_cpu1mem_phys + (unsigned long)P4A_CPU1MEM_BASE);
}



/*
 * Pick out the reserved memory size.  We look for mem=size@start,
 * where start and size are "size[KkMm]"
 */
static int __init early_p4a_cpu1_mem(char *p)
{
	unsigned long size, start;
	char *endp;

	start = P4A_CPU1MEM_PHYS;

	size = memparse(p, &endp);
	if (*endp == '@')
		start = memparse(endp + 1, NULL);

	p4a_cpu1mem_phys = start;
	p4a_cpu1mem_size = size;

	return 0;
}

early_param("cpu1_mem", early_p4a_cpu1_mem);

