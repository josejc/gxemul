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
 *  $Id: dev_fdc.c,v 1.3 2004/01/06 01:59:51 debug Exp $
 *  
 *  Floppy controller.
 *
 *  TODO!  (This is just a dummy skeleton right now.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"


struct fdc_data {
	unsigned char	reg[DEV_FDC_LENGTH];
	int		irqnr;
};


/*
 *  dev_fdc_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_fdc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	int i;
	struct fdc_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	/*  TODO:  this is 100% dummy  */

	switch (relative_addr) {
	case 0x04:
		/*  no debug warning  */
		if (writeflag==MEM_READ) {
			odata = d->reg[relative_addr];
		} else
			d->reg[relative_addr] = idata;
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ fdc: read from reg %i ]\n", (int)relative_addr);
			odata = d->reg[relative_addr];
		} else {
			debug("[ fdc: write to reg %i:", (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
			d->reg[relative_addr] = idata;
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_fdc_init():
 */
void dev_fdc_init(struct memory *mem, uint64_t baseaddr, int irq_nr)
{
	struct fdc_data *d;

	d = malloc(sizeof(struct fdc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct fdc_data));
	d->irqnr = irq_nr;

	memory_device_register(mem, "fdc", baseaddr, DEV_FDC_LENGTH, dev_fdc_access, d);
}

