/*
 *  Copyright (C) 2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: dev_wdc.c,v 1.5 2004/04/02 05:48:17 debug Exp $
 *  
 *  Standard IDE controller.
 *
 *  TODO:  Most read/write related stuff and interrupts.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"

#include "diskimage.h"
#include "wdcreg.h"

#define	WDC_INBUF_SIZE		4096


struct wdc_data {
	int		irq_nr;
	int		base_drive;

	unsigned char	identify_struct[512];

	unsigned char	inbuf[WDC_INBUF_SIZE];
	int		inbuf_head;
	int		inbuf_tail;

	int		error;
	int		precomp;
	int		seccnt;
	int		sector;
	int		cyl_lo;
	int		cyl_hi;
	int		sectorsize;
	int		lba;
	int		drive;
	int		head;
	int		cur_command;
};


/*
 *  wdc_addtoinbuf():
 *
 *  Write to the inbuf at its head, read at its tail.
 */
void wdc_addtoinbuf(struct wdc_data *d, int c)
{
	d->inbuf[d->inbuf_head] = c;

	d->inbuf_head = (d->inbuf_head + 1) % WDC_INBUF_SIZE;
	if (d->inbuf_head == d->inbuf_tail)
		fatal("WARNING! wdc inbuf overrun\n");
}


/*
 *  wdc_get_inbuf():
 *
 *  Read from the tail of inbuf.
 */
uint64_t wdc_get_inbuf(struct wdc_data *d)
{
	int c = d->inbuf[d->inbuf_tail];

	if (d->inbuf_head == d->inbuf_tail) {
		fatal("WARNING! someone is reading too much from the wdc inbuf!\n");
		return -1;
	}

	d->inbuf_tail = (d->inbuf_tail + 1) % WDC_INBUF_SIZE;
	return c;
}


/*
 *  wdc_initialize_identify_struct(d):
 */
void wdc_initialize_identify_struct(struct wdc_data *d)
{
	uint64_t total_size, cyls;

	total_size = diskimage_getsize(d->drive + d->base_drive);

	memset(d->identify_struct, 0, sizeof(d->identify_struct));

	cyls = total_size / (63 * 16 * 512);
	if (cyls * 63*16*512 < total_size)
		cyls ++;

	/*  Offsets are in 16-bit WORDS!  High byte, then low.  */

	/*  0: general flags  */
	d->identify_struct[2 * 0 + 0] = 0;
	d->identify_struct[2 * 0 + 1] = 1 << 6;

	/*  1: nr of cylinders  */
	d->identify_struct[2 * 1 + 0] = (cyls >> 8);
	d->identify_struct[2 * 1 + 1] = cyls & 255;

	/*  3: nr of heads  */
	d->identify_struct[2 * 3 + 0] = 0;
	d->identify_struct[2 * 3 + 1] = 16;

	/*  6: sectors per track  */
	d->identify_struct[2 * 6 + 0] = 0;
	d->identify_struct[2 * 6 + 1] = 63;

	/*  10-19: Serial number  */
	memcpy(&d->identify_struct[2 * 10], "S/N 1234-5678       ", 20);

	/*  23-26: Firmware version  */
	memcpy(&d->identify_struct[2 * 23], "VER 1.0 ", 8);

	/*  27-46: Model number  */
	memcpy(&d->identify_struct[2 * 27], "Fake mips64emul disk                    ", 40);
	/*  TODO:  Use the diskimage's filename instead?  */

	/*  47: max sectors per multitransfer  */
	d->identify_struct[2 * 47 + 0] = 0x80;
	d->identify_struct[2 * 47 + 1] = 1;	/*  1 or 16?  */

	/*  57-58: current capacity in sectors  */
	d->identify_struct[2 * 57 + 0] = ((total_size / 512) >> 24) % 255;
	d->identify_struct[2 * 57 + 1] = ((total_size / 512) >> 16) % 255;
	d->identify_struct[2 * 58 + 0] = ((total_size / 512) >> 8) % 255;
	d->identify_struct[2 * 58 + 1] = (total_size / 512) & 255;

	/*  60-61: total nr of addresable sectors  */
	d->identify_struct[2 * 60 + 0] = ((total_size / 512) >> 24) % 255;
	d->identify_struct[2 * 60 + 1] = ((total_size / 512) >> 16) % 255;
	d->identify_struct[2 * 61 + 0] = ((total_size / 512) >> 8) % 255;
	d->identify_struct[2 * 61 + 1] = (total_size / 512) & 255;

}


/*
 *  dev_wdc_altstatus_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_wdc_altstatus_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct wdc_data *d = extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	/*  Same as the normal status byte?  */
	odata = 0;
	if (diskimage_exist(d->drive + d->base_drive))
		odata |= WDCS_DRDY;
	if (d->inbuf_head != d->inbuf_tail)
		odata |= WDCS_DRQ;

	if (writeflag==MEM_READ)
		debug("[ wdc: read from ALTSTATUS: 0x%02x ]\n", (int)relative_addr, odata);
	else
		debug("[ wdc: write to ALT. CTRL: 0x%02x ]\n", (int)relative_addr, idata);

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_wdc_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_wdc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct wdc_data *d = extra;
	uint64_t idata = 0, odata = 0;
	int i;

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {

	case wd_data:	/*  0: data  */
		if (writeflag==MEM_READ) {
			odata = 0;

#if 1
			if (len == 4) {
				odata += (wdc_get_inbuf(d) << 24);
				odata += (wdc_get_inbuf(d) << 16);
			}
			if (len >= 2)
				odata += (wdc_get_inbuf(d) << 8);
			odata += wdc_get_inbuf(d);
#else
			switch (len) {
			case 4:
			case 2:	odata += (wdc_get_inbuf(d) << 8);
			default:odata += wdc_get_inbuf(d);
			}
			if (len == 4) {
				odata += (wdc_get_inbuf(d) << 24);
				odata += (wdc_get_inbuf(d) << 16);
			}
#endif

			debug("[ wdc: read from DATA: 0x%04x ]\n", odata);
		} else {
			debug("[ wdc: write to DATA: 0x%04x ]\n", idata);
			/*  TODO  */
		}
		break;

	case wd_error:	/*  1: error (r), precomp (w)  */
		if (writeflag==MEM_READ) {
			odata = d->error;
			debug("[ wdc: read from ERROR: 0x%02x ]\n", odata);
			/*  TODO:  is the error value cleared on read?  */
			d->error = 0;
		} else {
			d->precomp = idata;
			debug("[ wdc: write to PRECOMP: 0x%02x ]\n", idata);
		}
		break;

	case wd_seccnt:	/*  2: sector count  */
		if (writeflag==MEM_READ) {
			odata = d->seccnt;
			debug("[ wdc: read from SECCNT: 0x%02x ]\n", odata);
		} else {
			d->seccnt = idata;
			debug("[ wdc: write to SECCNT: 0x%02x ]\n", idata);
		}
		break;

	case wd_sector:	/*  3: first sector  */
		if (writeflag==MEM_READ) {
			odata = d->sector;
			debug("[ wdc: read from SECTOR: 0x%02x ]\n", odata);
		} else {
			d->sector = idata;
			debug("[ wdc: write to SECTOR: 0x%02x ]\n", idata);
		}
		break;

	case wd_cyl_lo:	/*  4: cylinder low  */
		if (writeflag==MEM_READ) {
			odata = d->cyl_lo;
			debug("[ wdc: read from CYL_LO: 0x%02x ]\n", odata);
		} else {
			d->cyl_lo = idata;
			debug("[ wdc: write to CYL_LO: 0x%02x ]\n", idata);
		}
		break;

	case wd_cyl_hi:	/*  5: cylinder low  */
		if (writeflag==MEM_READ) {
			odata = d->cyl_hi;
			debug("[ wdc: read from CYL_HI: 0x%02x ]\n", odata);
		} else {
			d->cyl_hi = idata;
			debug("[ wdc: write to CYL_HI: 0x%02x ]\n", idata);
		}
		break;

	case wd_sdh:	/*  6: sectorsize/drive/head  */
		if (writeflag==MEM_READ) {
			odata = (d->sectorsize << 6) + (d->lba << 5) +
			    (d->drive << 4) + (d->head);
			debug("[ wdc: read from SDH: 0x%02x (sectorsize %i, lba=%i, drive %i, head %i) ]\n",
			    odata, d->sectorsize, d->lba, d->drive, d->head);
		} else {
			d->sectorsize = (idata >> 6) & 3;
			d->lba   = (idata >> 5) & 1;
			d->drive = (idata >> 4) & 1;
			d->head  = idata & 0xf;
			debug("[ wdc: write to SDH: 0x%02x (sectorsize %i, lba=%i, drive %i, head %i) ]\n",
			    idata, d->sectorsize, d->lba, d->drive, d->head);
		}
		break;

	case wd_command:	/*  7: command or status  */
		if (writeflag==MEM_READ) {
			odata = 0;
			if (diskimage_exist(d->drive + d->base_drive))
				odata |= WDCS_DRDY;
			if (d->inbuf_head != d->inbuf_tail)
				odata |= WDCS_DRQ;
			if (d->error)
				odata |= WDCS_ERR;

			/*  TODO:  Is this correct behaviour?  */
			if (!diskimage_exist(d->drive + d->base_drive))
				odata = 0xff;

			debug("[ wdc: read from STATUS: 0x%02x ]\n", odata);
#if 0
/* ?? */		if (cpu->coproc[0]->reg[COP0_STATUS] & (1 << (d->irq_nr + 8)))
				cpu_interrupt_ack(cpu, d->irq_nr);
#endif
		} else {
			debug("[ wdc: write to COMMAND: 0x%02x ]\n", idata);
			d->cur_command = idata;

			/*  TODO:  Is this correct behaviour?  */
			if (!diskimage_exist(d->drive + d->base_drive))
				break;

			/*  Handle the command:  */
			switch (d->cur_command) {
			case WDCC_READ:
				debug("[ wdc: READ from drive %i, head %i, cylinder %i, sector %i, nsecs %i ]\n",
				    d->drive, d->head, d->cyl_hi*256+d->cyl_lo, d->sector, d->seccnt);
				/*  TODO:  HAHA! This should be removed quickly  */
				{
					unsigned char buf[512*256];
					int cyl = d->cyl_hi * 256+ d->cyl_lo;
					int count = d->seccnt? d->seccnt : 256;
					uint64_t offset = 512 * (d->sector - 1 + d->head * 63 + 16*63*cyl);
					diskimage_access(d->drive + d->base_drive, 0, offset, buf, 512 * count);
					/*  TODO: result code  */
					for (i=0; i<512 * count; i++)
						wdc_addtoinbuf(d, buf[i]);
				}
				cpu_interrupt(cpu, d->irq_nr);
				break;
			case WDCC_IDP:	/*  Initialize drive parameters  */
				debug("[ wdc: IDP drive %i (TODO) ]\n", d->drive);
				/*  TODO  */
				cpu_interrupt(cpu, d->irq_nr);
				break;
			case SET_FEATURES:
				fatal("[ wdc: SET_FEATURES drive %i (TODO), feature 0x%02x ]\n", d->drive, d->precomp);
				/*  TODO  */
				switch (d->precomp) {
				case WDSF_SET_MODE:
					fatal("[ wdc: WDSF_SET_MODE drive %i, pio/dma flags 0x%02x ]\n", d->drive, d->seccnt);
					break;
				default:
					d->error |= WDCE_ABRT;
				}
				/*  TODO: always interrupt?  */
				cpu_interrupt(cpu, d->irq_nr);
				break;
			case WDCC_RECAL:
				debug("[ wdc: RECAL drive %i ]\n", d->drive);
				cpu_interrupt(cpu, d->irq_nr);
				break;
			case WDCC_IDENTIFY:
				debug("[ wdc: IDENTIFY drive %i ]\n", d->drive);
				wdc_initialize_identify_struct(d);
				/*  The IDENTIFY data block is sent out in low/high byte order:  */
				for (i=0; i<sizeof(d->identify_struct); i+=2) {
					wdc_addtoinbuf(d, d->identify_struct[i+0]);
					wdc_addtoinbuf(d, d->identify_struct[i+1]);
				}

				cpu_interrupt(cpu, d->irq_nr);
				break;
			default:
				fatal("[ wdc: unknown command 0x%02x (drive %i, head %i, cylinder %i, sector %i, nsecs %i) ]\n",
				    d->cur_command, d->drive, d->head, d->cyl_hi*256+d->cyl_lo, d->sector, d->seccnt);
			}
		}
		break;

	default:
		if (writeflag==MEM_READ)
			debug("[ wdc: read from 0x%02x ]\n", (int)relative_addr);
		else
			debug("[ wdc: write to  0x%02x: 0x%02x ]\n", (int)relative_addr, idata);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_wdc_init():
 *
 *  base_drive should be 0 for the primary device, and
 *  2 for the secondary.
 */
void dev_wdc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int base_drive)
{
	struct wdc_data *d;

	d = malloc(sizeof(struct wdc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct wdc_data));
	d->irq_nr     = irq_nr;
	d->base_drive = base_drive;

	memory_device_register(mem, "wdc_altstatus", baseaddr + 0x206, 1, dev_wdc_altstatus_access, d);
	memory_device_register(mem, "wdc", baseaddr, DEV_WDC_LENGTH, dev_wdc_access, d);

/*	cpu_add_tickfunction(cpu, dev_wdc_tick, d, 10);  */
}

