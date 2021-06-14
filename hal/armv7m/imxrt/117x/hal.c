/*
 * Phoenix-RTOS
 *
 * Operating system loader
 *
 * Hardware Abstraction Layer
 *
 * Copyright 2020-2021 Phoenix Systems
 * Author: Hubert Buczynski, Marcin Baran
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <syspage.h>
#include <hal/hal.h>
#include <lib/errno.h>

typedef struct {
	void *data;
	int (*isr)(unsigned int, void *);
} intr_handler_t;


struct{
	intr_handler_t irqs[SIZE_INTERRUPTS];
} hal_common;


/* Linker symbols */
extern void _end(void);
extern void _plo_bss(void);

/* Timer */
extern void timer_init(void);
extern void timer_done(void);

/* Console */
extern void console_init(void);


void hal_init(void)
{
	_imxrt_init();
	timer_init();

	console_init();

	syspage_init();
	syspage_setAddress(SYSPAGE_ADDRESS);

	/* Add entries related to plo image */
	syspage_addEntries((addr_t)_plo_bss, (addr_t)_end - (addr_t)_plo_bss + STACK_SIZE);
}


void hal_done(void)
{
	timer_done();

	_imxrt_cleanDCache();
}


const char *hal_cpuInfo(void)
{
	return "Cortex-M i.MX RT117x";
}


void hal_cpuInvCache(unsigned int type, addr_t addr, size_t sz)
{
	switch (type) {
		case hal_cpuDCache:
			/* TODO */
		case hal_cpuICache:
			/* TODO */
		default:
			break;
	}
}


void hal_cpuInvCacheAll(unsigned int type)
{
	switch (type) {
		case hal_cpuDCache:
			/* TODO */
		case hal_cpuICache:
			/* TODO */
		default:
			break;
	}
}


addr_t hal_kernelGetAddress(addr_t addr)
{
	return addr;
}


int hal_cpuJump(addr_t addr)
{
	time_t start;

	syspage_save();

	/* Give the LPUART transmitters some time */
	start = hal_timerGet();
	while ((hal_timerGet() - start) < 100);

	/* Tidy up */
	hal_done();

	hal_interruptsDisable();
	__asm__ volatile("mov r9, %1; \
		 blx %0"
		 :
		 : "r"(addr), "r"(syspage_getAddress()));
	hal_interruptsEnable();

	return -1;
}


void hal_interruptsDisable(void)
{
	__asm__ volatile("cpsid if");
}


void hal_interruptsEnable(void)
{
	__asm__ volatile("cpsie if");
}


int hal_interruptsSet(unsigned int irq, int (*isr)(unsigned int, void *), void *data)
{
	if (irq >= SIZE_INTERRUPTS)
		return -EINVAL;

	hal_interruptsDisable();
	hal_common.irqs[irq].isr = isr;
	hal_common.irqs[irq].data = data;

	if (isr == NULL) {
		_imxrt_nvicSetIRQ(irq - 0x10, 0);
	}
	else {
		_imxrt_nvicSetPriority(irq - 0x10, 1);
		_imxrt_nvicSetIRQ(irq - 0x10, 1);
	}
	hal_interruptsEnable();

	return EOK;
}


int hal_irqdispatch(unsigned int irq)
{
	if (hal_common.irqs[irq].isr == NULL)
		return -EINTR;

	hal_common.irqs[irq].isr(irq, hal_common.irqs[irq].data);

	return EOK;
}