/*
 * Phoenix-RTOS
 *
 * plo - operating system loader
 *
 * Hardware Abstraction Layer
 *
 * Copyright 2021 Phoenix Systems
 * Author: Hubert Buczynski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include "zynq.h"
#include "cmd-board.h"
#include "interrupts.h"
#include "peripherals.h"
#include "phfs-serial.h"
#include "cdc-client.h"
#include "phfs-usb.h"
#include "gpio.h"

#include "../hal.h"
#include "../plostd.h"
#include "../serial.h"
#include "../timer.h"
#include "../syspage.h"


struct{
	u16 timeout;
	u32 kernel_entry;
} hal_common;


/* Board command definitions */
const cmd_t board_cmds[] = {
	{ cmd_bitstream, "bitstream", "- loads bitstream into PL, usage:\n            bitstream [<boot device>] [<name>]" },
	{ NULL, NULL, NULL }
};


const cmd_device_t devices[] = {
	{ "com1", PDN_COM1 },
	{ "usb0", PDN_ACM0 },
	{ NULL, NULL }
};


/* Initialization functions */

void hal_init(void)
{
	_zynq_init();
	interrupts_init();
	timer_init();
	gpio_init();

	syspage_init();
	syspage_setAddress((void *)SYSPAGE_ADDRESS);

	hal_common.timeout = 3;
	hal_common.kernel_entry = 0;
}


void hal_done(void)
{
	phfs_serialDeinit();
	phfs_usbDeinit();
	timer_done();
}


void hal_initphfs(phfs_handler_t *handlers)
{
	handlers[PDN_COM1].open = phfs_serialOpen;
	handlers[PDN_COM1].read = phfs_serialRead;
	handlers[PDN_COM1].write = phfs_serialWrite;
	handlers[PDN_COM1].close = phfs_serialClose;
	handlers[PDN_COM1].dn = PHFS_SERIAL_LOADER_ID;
	phfs_serialInit();

	handlers[PDN_ACM0].open = phfs_usbOpen;
	handlers[PDN_ACM0].read = phfs_usbRead;
	handlers[PDN_ACM0].write = phfs_usbWrite;
	handlers[PDN_ACM0].close = phfs_usbClose;
	handlers[PDN_ACM0].dn = endpt_bulk_acm0;
	phfs_usbInit();
}


void hal_initdevs(cmd_device_t **devs)
{
	*devs = (cmd_device_t *)devices;
}


void hal_appendcmds(cmd_t *cmds)
{
	int i = 0;

	/* Find the last declared cmd */
	while (cmds[i++].cmd != NULL);

	if ((MAX_COMMANDS_NB - --i) < (sizeof(board_cmds) / sizeof(cmd_t)))
		return;

	hal_memcpy(&cmds[i], board_cmds, sizeof(board_cmds));
}



/* Setters and getters for common data */

void hal_setDefaultIMAP(char *imap)
{
	hal_memcpy(imap, "ddr", 4);
}


void hal_setDefaultDMAP(char *dmap)
{
	hal_memcpy(dmap, "ddr", 4);
}


void hal_setKernelEntry(u32 addr)
{
	u32 offs;

	if ((u32)VADDR_KERNEL_INIT != (u32)ADDR_DDR) {
		offs = addr - VADDR_KERNEL_INIT;
		addr = ADDR_DDR + offs;
	}

	hal_common.kernel_entry = addr;
}


void hal_setLaunchTimeout(u32 timeout)
{
	hal_common.timeout = timeout;
}


u32 hal_getLaunchTimeout(void)
{
	return hal_common.timeout;
}


addr_t hal_vm2phym(addr_t addr)
{
	u32 offs;
	if ((u32)VADDR_KERNEL_INIT != (u32)ADDR_DDR) {
		offs = addr - VADDR_KERNEL_INIT;
		addr = ADDR_DDR + offs;
	}

	return addr;
}


void hal_memcpy(void *dst, const void *src, unsigned int l)
{
	asm volatile(" \
		orr r3, %0, %1; \
		ands r3, #3; \
		bne 2f; \
	1: \
		cmp %2, #4; \
		ittt hs; \
		ldrhs r3, [%1], #4; \
		strhs r3, [%0], #4; \
		subshs %2, #4; \
		bhs 1b; \
	2: \
		cmp %2, #0; \
		ittt ne; \
		ldrbne r3, [%1], #1; \
		strbne r3, [%0], #1; \
		subsne %2, #1; \
		bne 2b"
	: "+l" (dst), "+l" (src), "+l" (l)
	:
	: "r3", "memory", "cc");
}


int hal_launch(void)
{
	syspage_save();

	/* Give the LPUART transmitters some time */
	timer_wait(100, TIMER_EXPIRE, NULL, 0);

	/* Tidy up */
	hal_done();

	hal_cli();
	asm("mov r9, %1; \
		 blx %0"
		 :
		 : "r"(hal_common.kernel_entry), "r"(syspage_getAddress()));

	hal_sti();

	return -1;
}



/* Opeartions on interrupts */

void hal_cli(void)
{
	asm("cpsid if");
}


void hal_sti(void)
{
	asm("cpsie if");
}


int low_irqdispatch(u16 irq)
{
	return 0;
}


void hal_maskirq(u16 n, u8 v)
{
	//TODO
}


int hal_irqinst(u16 irq, int (*isr)(u16, void *), void *data)
{
	hal_cli();
	interrupts_setHandler(irq, isr, data);
	hal_sti();

	return 0;
}


int hal_irquninst(u16 irq)
{
	hal_cli();
	interrupts_deleteHandler(irq);
	hal_sti();

	return 0;
}


/* Communication functions */

void hal_setattr(char attr)
{
	switch (attr) {
	case ATTR_DEBUG:
		serial_safewrite(UART_CONSOLE, (u8 *)"\033[0m\033[32m", 9);
		break;
	case ATTR_USER:
		serial_safewrite(UART_CONSOLE, (u8 *)"\033[0m", 4);
		break;
	case ATTR_INIT:
		serial_safewrite(UART_CONSOLE, (u8 *)"\033[0m\033[35m", 9);
		break;
	case ATTR_LOADER:
		serial_safewrite(UART_CONSOLE, (u8 *)"\033[0m\033[1m", 8);
		break;
	case ATTR_ERROR:
		serial_safewrite(UART_CONSOLE, (u8 *)"\033[0m\033[31m", 9);
		break;
	}

	return;
}


void hal_putc(const char ch)
{
	serial_write(UART_CONSOLE, (u8 *)&ch, 1);
}


void hal_getc(char *c, char *sc)
{
	while (serial_read(UART_CONSOLE, (u8 *)c, 1, 500) <= 0)
		;
	*sc = 0;

	/* Translate backspace */
	if (*c == 127)
		*c = 8;

	/* Simple parser for VT100 commands */
	else if (*c == 27) {
		while (serial_read(UART_CONSOLE, (u8 *)c, 1, 500) <= 0)
			;

		switch (*c) {
		case 91:
			while (serial_read(UART_CONSOLE, (u8 *)c, 1, 500) <= 0)
				;

			switch (*c) {
			case 'A':             /* UP */
				*sc = 72;
				break;
			case 'B':             /* DOWN */
				*sc = 80;
				break;
			}
			break;
		}
		*c = 0;
	}
}


int hal_keypressed(void)
{
	return !serial_rxEmpty(UART_CONSOLE);
}
