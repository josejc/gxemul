/*
 *  Copyright (C) 2003-2004 by Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE   
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 *   
 *
 *  $Id: dev_ssc.c,v 1.9 2004/03/10 02:08:55 debug Exp $
 *  
 *  Serial controller on DECsystem 5400 and 5800.
 *  Known as System Support Chip on VAX 3600 (KA650).
 *
 *  Described around page 80 in the kn210tm1.pdf.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"


#define	RX_INT_ENABLE	0x40
#define	RX_AVAIL	0x80
#define	TX_INT_ENABLE	0x40
#define	TX_READY	0x80


/*
 *  _TXRX is for debugging putchar/getchar. The other
 *  one is more general.
 */
/*  #define SSC_DEBUG_TXRX  */
#define SSC_DEBUG

struct ssc_data {
	int		irq_nr;
	int		use_fb;

	int		rx_ctl;
	int		tx_ctl;

	uint32_t	*csrp;
};


/*
 *  dev_ssc_tick():
 */
void dev_ssc_tick(struct cpu *cpu, void *extra)
{
	struct ssc_data *d = extra;

	d->tx_ctl |= TX_READY;	/*  transmitter always ready  */

	d->rx_ctl &= ~RX_AVAIL;
	if (console_charavail())
		d->rx_ctl |= RX_AVAIL;

	/*  rx interrupts enabled, and char avail?  */
	if (d->rx_ctl & RX_INT_ENABLE && d->rx_ctl & RX_AVAIL) {
		/*  TODO:  This is for 5800 only!  */

		if (d->csrp != NULL) {
			unsigned char txvector = 0xf8;
			(*d->csrp) |= 0x10000000;
			memory_rw(cpu, cpu->mem, 0x40000050, &txvector, 1, MEM_WRITE, NO_EXCEPTIONS | PHYSICAL);
			cpu_interrupt(cpu, 2);
		}
	}

	/*  tx interrupts enabled?  */
	if (d->tx_ctl & TX_INT_ENABLE) {
		/*  TODO:  This is for 5800 only!  */

		if (d->csrp != NULL) {
			unsigned char txvector = 0xfc;
			(*d->csrp) |= 0x10000000;
			memory_rw(cpu, cpu->mem, 0x40000050, &txvector, 1, MEM_WRITE, NO_EXCEPTIONS | PHYSICAL);
			cpu_interrupt(cpu, 2);
		}
	}
}


/*
 *  dev_ssc_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_ssc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	struct ssc_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	dev_ssc_tick(cpu, extra);

	switch (relative_addr) {
	case 0x0080:	/*  receive status  */
		if (writeflag==MEM_READ) {
			odata = d->rx_ctl;
#ifdef SSC_DEBUG_TXRX
			debug("[ ssc: read from 0x%08lx: 0x%02x ]\n", (long)relative_addr, odata);
#endif
		} else {
			d->rx_ctl = idata;

			/*  TODO:  This only works for 5800  */
			if (d->csrp != NULL) {
				(*d->csrp) &= ~0x10000000;
				cpu_interrupt_ack(cpu, 2);
			}
#ifdef SSC_DEBUG_TXRX
			debug("[ ssc: write to  0x%08lx: 0x%02x ]\n", (long)relative_addr, idata);
#endif
		}

		break;
	case 0x0084:	/*  receive data  */
		if (writeflag==MEM_READ) {
#ifdef SSC_DEBUG_TXRX
			debug("[ ssc: read from 0x%08lx ]\n", (long)relative_addr);
#endif
			if (console_charavail())
				odata = console_readchar();
		} else {
#ifdef SSC_DEBUG_TXRX
			debug("[ ssc: write to 0x%08lx: 0x%02x ]\n", (long)relative_addr, idata);
#endif
		}

		break;
	case 0x0088:	/*  transmit status  */
		if (writeflag==MEM_READ) {
			odata = d->tx_ctl;
#ifdef SSC_DEBUG_TXRX
			debug("[ ssc: read from 0x%08lx: 0x%04x ]\n", (long)relative_addr, odata);
#endif
		} else {
			d->tx_ctl = idata;

			/*  TODO:  This only works for 5800  */
			if (d->csrp != NULL) {
				(*d->csrp) &= ~0x10000000;
				cpu_interrupt_ack(cpu, 2);
			}
#ifdef SSC_DEBUG_TXRX
			debug("[ ssc: write to  0x%08lx: 0x%02x ]\n", (long)relative_addr, idata);
#endif
		}

		break;
	case 0x008c:	/*  transmit data  */
		if (writeflag==MEM_READ) {
			debug("[ ssc: read from 0x%08lx ]\n", (long)relative_addr);
		} else {
			/*  debug("[ ssc: write to 0x%08lx: 0x%02x ]\n", (long)relative_addr, idata);  */
			console_putchar(idata);
		}

		break;
	case 0x0100:
		if (writeflag==MEM_READ) {
			odata = 128;
#ifdef SSC_DEBUG_TXRX
			debug("[ ssc: read from 0x%08lx: 0x%08lx ]\n", (long)relative_addr, (long)odata);
#endif
		} else {
#ifdef SSC_DEBUG_TXRX
			debug("[ ssc: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);
#endif
		}

		break;
	case 0x0108:
		if (writeflag==MEM_READ) {
			debug("[ ssc: read from 0x%08lx ]\n", (long)relative_addr);
		} else {
#ifdef SSC_DEBUG
			debug("[ ssc: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);
#endif
		}

		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ ssc: read from 0x%08lx ]\n", (long)relative_addr);
		} else {
			debug("[ ssc: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);
		}
	}

	dev_ssc_tick(cpu, extra);

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_ssc_init():
 */
void dev_ssc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int use_fb, uint32_t *csrp)
{
	struct ssc_data *d;

	d = malloc(sizeof(struct ssc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct ssc_data));
	d->irq_nr = irq_nr;
	d->use_fb = use_fb;
	d->csrp   = csrp;

	memory_device_register(mem, "ssc", baseaddr, DEV_SSC_LENGTH, dev_ssc_access, d);
	cpu_add_tickfunction(cpu, dev_ssc_tick, d, 12);
}

