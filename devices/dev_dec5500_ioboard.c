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
 *  $Id: dev_dec5500_ioboard.c,v 1.3 2004/06/22 22:24:25 debug Exp $
 *  
 *  DEC 5500 "ioboard" device.
 *
 *  TODO:  Find out what kind of device this is :)
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"

#define IOBOARD_DEBUG

struct dec5500_ioboard_data {
	int	dummy;
};


/*
 *  dev_dec5500_ioboard_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_dec5500_ioboard_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	/*  struct dec5500_ioboard_data *d = (struct dec5500_ioboard_data *) extra;  */
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

#ifdef IOBOARD_DEBUG
	if (writeflag == MEM_WRITE)
		debug("[ dec5500_ioboard: write to address 0x%llx, data=0x%016llx ]\n", (long long)relative_addr, (long long)idata);
	else
		debug("[ dec5500_ioboard: read from address 0x%llx ]\n", (long long)relative_addr);
#endif

	switch (relative_addr) {
	case 0:
		if (writeflag == MEM_READ)
			odata = 0xffffffff;		/*  TODO  ?  One of these bits indicate I/O board present  */
		break;

	default:
		if (writeflag == MEM_WRITE)
			debug("[ dec5500_ioboard: unimplemented write to address 0x%llx, data=0x%016llx ]\n", (long long)relative_addr, (long long)idata);
		else
			debug("[ dec5500_ioboard: unimplemented read from address 0x%llx ]\n", (long long)relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_dec5500_ioboard_init():
 */
struct dec5500_ioboard_data *dev_dec5500_ioboard_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr)
{
	struct dec5500_ioboard_data *d = malloc(sizeof(struct dec5500_ioboard_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct dec5500_ioboard_data));

	memory_device_register(mem, "dec5500_ioboard", baseaddr, DEV_DEC5500_IOBOARD_LENGTH, dev_dec5500_ioboard_access, (void *)d);

	return d;
}

