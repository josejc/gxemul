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
 *  $Id: dev_bt455.c,v 1.1 2004/04/24 22:38:43 debug Exp $
 *  
 *  Brooktree 455, used by TURBOchannel graphics cards.
 *
 *  TODO:  This is hardcoded to only use 16 grayscales, using only the
 *  green component of the palette.  Perhaps some other graphics card uses
 *  the BT455 as well; if so then this device must be re-hacked.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"



struct bt455_data {
	unsigned char	addr_cmap;
	unsigned char	addr_cmap_data;
	unsigned char	addr_clr;
	unsigned char	addr_ovly;

	int		cur_rgb_offset;

	struct vfb_data *vfb_data;
	unsigned char	*rgb_palette;		/*  ptr to 256 * 3 (r,g,b)  */
};


/*
 *  dev_bt455_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_bt455_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct bt455_data *d = (struct bt455_data *) extra;
	uint64_t idata = 0, odata = 0;
	int i, modified;

	idata = memory_readmax64(cpu, data, len);

	/*  Read from/write to the bt455:  */
	switch (relative_addr) {
	case 0x00:		/*  addr_cmap  */
		if (writeflag == MEM_WRITE) {
			debug("[ bt455: write to addr_cmap, 0x%02x ]\n", idata);
			d->addr_cmap = idata;
			d->cur_rgb_offset = (d->addr_cmap & 0xf) * 3;
		} else {
			odata = d->addr_cmap;
			debug("[ bt455: read from addr_cmap: 0x%0x ]\n", odata);
		}
		break;
	case 0x04:		/*  addr_cmap_data  */
		if (writeflag == MEM_WRITE) {
			debug("[ bt455: write to addr_cmap_data, 0x%02x ]\n", idata);
			d->addr_cmap_data = idata;

			modified = 0;

			/*  Only write on updates to the Green value:  */
			if ((d->cur_rgb_offset % 3) == 1) {
				/*  Update 16 copies:  */
				for (i=0; i<16; i++) {
					int addr = (d->cur_rgb_offset + 16*i) % 0x300;
					int newvalue = idata * 0x11;

					if (d->rgb_palette[(addr / 3) * 3 + 0] != newvalue ||
					    d->rgb_palette[(addr / 3) * 3 + 1] != newvalue ||
					    d->rgb_palette[(addr / 3) * 3 + 2] != newvalue)
						modified = 1;

					d->rgb_palette[(addr / 3) * 3 + 0] = newvalue;
					d->rgb_palette[(addr / 3) * 3 + 1] = newvalue;
					d->rgb_palette[(addr / 3) * 3 + 2] = newvalue;
				}
			}

			if (modified) {
				d->vfb_data->update_x1 = 0;
				d->vfb_data->update_x2 = d->vfb_data->xsize - 1;
				d->vfb_data->update_y1 = 0;
				d->vfb_data->update_y2 = d->vfb_data->ysize - 1;
			}

			/*  Advance to next palette byte:  */
			d->cur_rgb_offset ++;
		} else {
			odata = d->addr_cmap_data;
			debug("[ bt455: read from addr_cmap_data: 0x%0x ]\n", odata);
		}
		break;
	case 0x08:		/*  addr_clr  */
		if (writeflag == MEM_WRITE) {
			debug("[ bt455: write to addr_clr, value 0x%02x ]\n", idata);
			d->addr_clr = idata;
		} else {
			odata = d->addr_clr;
			debug("[ bt455: read from addr_clr: value 0x%02x ]\n", odata);
		}
		break;
	case 0x0c:		/*  addr_ovly  */
		if (writeflag == MEM_WRITE) {
			debug("[ bt455: write to addr_ovly, value 0x%02x ]\n", idata);
			d->addr_ovly = idata;
		} else {
			odata = d->addr_ovly;
			debug("[ bt455: read from addr_ovly: value 0x%02x ]\n", odata);
		}
		break;
	default:
		if (writeflag == MEM_WRITE) {
			debug("[ bt455: unimplemented write to address 0x%x, data=0x%02x ]\n", relative_addr, idata);
		} else {
			debug("[ bt455: unimplemented read from address 0x%x ]\n", relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_bt455_init():
 */
void dev_bt455_init(struct memory *mem, uint64_t baseaddr, struct vfb_data *vfb_data)
{
	struct bt455_data *d = malloc(sizeof(struct bt455_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct bt455_data));
	d->vfb_data     = vfb_data;
	d->rgb_palette  = vfb_data->rgb_palette;

	memory_device_register(mem, "bt455", baseaddr, DEV_BT455_LENGTH, dev_bt455_access, (void *)d);
}

