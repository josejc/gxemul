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
 *  $Id: dev_sgec.c,v 1.2 2004/06/22 22:24:54 debug Exp $
 *  
 *  SGEC (ethernet) used in DECstations.  (Called "ne" in Ultrix.)
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"

#define SGEC_DEBUG

struct sgec_data {
	int	irq_nr;
};


/*
 *  dev_sgec_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_sgec_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	/*  struct sgec_data *d = (struct sgec_data *) extra;  */
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

#ifdef SGEC_DEBUG
	if (writeflag == MEM_WRITE)
		debug("[ sgec: write to address 0x%llx, data=0x%016llx ]\n", (long long)relative_addr, (long long)idata);
	else
		debug("[ sgec: read from address 0x%llx ]\n", (long long)relative_addr);
#endif

	switch (relative_addr) {
	case 0x14:
		if (writeflag == MEM_READ)
			odata = 0x80000000;
		break;

	default:
		if (writeflag == MEM_WRITE)
			debug("[ sgec: unimplemented write to address 0x%llx, data=0x%016llx ]\n", (long long)relative_addr, (long long)idata);
		else
			debug("[ sgec: unimplemented read from address 0x%llx ]\n", (long long)relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgec_init():
 */
void dev_sgec_init(struct memory *mem, uint64_t baseaddr, int irq_nr)
{
	struct sgec_data *d = malloc(sizeof(struct sgec_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sgec_data));
	d->irq_nr = irq_nr;

	memory_device_register(mem, "sgec", baseaddr, DEV_SGEC_LENGTH, dev_sgec_access, (void *)d);
}

