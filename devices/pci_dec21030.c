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
 *  $Id: pci_dec21030.c,v 1.5 2004/03/22 00:55:29 debug Exp $
 *
 *  DEC 21030 "tga" graphics.
 *
 *  Resolutions that seem to be possible:  640x480, 1024x768, 1280x1024.
 *  8 bits, perhaps others? (24 bit?)
 *
 *  NetBSD should say something like this:
 *
 *	tga0 at pci0 dev 12 function 0: TGA2 pass 2, board type T8-02
 *	tga0: 1280 x 1024, 8bpp, Bt485 RAMDAC
 *
 *  See netbsd/src/sys/dev/pci/tga.c for more info.
 *	tga_rop_vtov() for video-to-video copy (scrolling and fast
 *			erasing)
 *
 *  TODO: This device is far from complete.
 *        The RAMDAC is non-existant.
 *
 *  TODO:  all fb device writes with direct writes to the framebuffer
 *  memory, and update the x1,y1,x2,y2 coordinates instead.
 *  That will give better performance.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"
#include "bus_pci.h"
#include "tgareg.h"

#define	MAX_XSIZE	2048

#if 1
int dec21030_default_xsize = 640;
int dec21030_default_ysize = 480;
#else
int dec21030_default_xsize = 1024;
int dec21030_default_ysize = 768;
#endif


/*  TODO:  Ugly hack:  this causes the framebuffer to be in memory  */
#define	FRAMEBUFFER_PADDR	0x4000000000
#define	FRAMEBUFFER_BASE	0x201000


struct dec21030_data {
	int		graphics_mode;
	uint32_t	pixel_mask;
	uint32_t	copy_source;
	uint32_t	color;
	struct vfb_data *vfb_data;
};


/*
 *  pci_dec21030_rr():
 *
 *  See http://mail-index.netbsd.org/port-arc/2001/08/13/0000.html
 *  for more info.
 */
uint32_t pci_dec21030_rr(int reg)
{
	switch (reg) {
	case 0x00:
		return PCI_VENDOR_DEC + (PCI_PRODUCT_DEC_21030 << 16);
	case 0x04:
		return 0x02800087;
	case 0x08:
		return 0x03800003;
		/*  return PCI_CLASS_CODE(PCI_CLASS_DISPLAY, PCI_SUBCLASS_DISPLAY_VGA, 0) + 0x03;  */
	case 0x0c:
		return 0x0000ff00;
	case 0x10:
		return 0x00000000 + 8;		/*  address  (8=prefetchable)  */
	case 0x30:
		return 0x08000001;
	case 0x3c:
		return 0x00000100;	/*  interrupt pin ?  */
	default:
		return 0;
	}
}


/*
 *  dev_dec21030_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_dec21030_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct dec21030_data *d = extra;
	uint64_t idata, odata = 0;
	int reg, r, i, white = 255, black = 0;
	int newlen;
	unsigned char buf2[MAX_XSIZE];

	/*  Read/write to the framebuffer:  */
	if (relative_addr >= FRAMEBUFFER_BASE) {
		/*  TODO:  Perhaps this isn't graphics mode (GMOR), but GOPR (operation) specific:  */

		switch (d->graphics_mode) {
		case 1:		/*  Bitmap write:  */
			/*  Copy from data into buf2:  */
			for (i=0; i<len; i++) {
				buf2[i*8 + 0] = data[i]&1? white : black;
				buf2[i*8 + 1] = data[i]&2? white : black;
				buf2[i*8 + 2] = data[i]&4? white : black;
				buf2[i*8 + 3] = data[i]&8? white : black;
				buf2[i*8 + 4] = data[i]&16? white : black;
				buf2[i*8 + 5] = data[i]&32? white : black;
				buf2[i*8 + 6] = data[i]&64? white : black;
				buf2[i*8 + 7] = data[i]&128? white : black;
			}

			newlen = 0;
			for (i=0; i<32; i++)
				if (d->pixel_mask & (1 << i))
					newlen ++;

			if (newlen > len * 8)
				newlen = len * 8;

			r = dev_fb_access(cpu, mem, relative_addr - FRAMEBUFFER_BASE, buf2, newlen, writeflag, d->vfb_data);
			break;
		case 0x2d:	/*  Block fill:  */
			/*  data is nr of pixels to fill minus one  */
			newlen = memory_readmax64(cpu, data, len) + 1;
			/*  debug("YO addr=0x%08x, newlen=%i\n", relative_addr, newlen);  */
			if (newlen > MAX_XSIZE)
				newlen = MAX_XSIZE;
			memset(buf2, d->color, newlen);
			r = dev_fb_access(cpu, mem, relative_addr - FRAMEBUFFER_BASE, buf2, newlen, MEM_WRITE, d->vfb_data);
			break;
		default:
			r = dev_fb_access(cpu, mem, relative_addr - FRAMEBUFFER_BASE, data, len, writeflag, d->vfb_data);
		}
		return r;
	}

	idata = memory_readmax64(cpu, data, len);

	/*  Read from/write to the dec21030's registers:  */
	reg = ((relative_addr - TGA_MEM_CREGS) & (TGA_CREGS_ALIAS - 1)) / sizeof(uint32_t);
        switch (reg) {

	/*  Color?  (there are 8 of these, 2 used in 8-bit mode, 8 in 24-bit mode)  */
	case TGA_REG_GBCR0:
		if (writeflag == MEM_WRITE)
			d->color = idata;
		else
			odata = d->color;
		break;

	/*  Board revision  */
/*	case TGA_MEM_CREGS + sizeof(uint32_t) * TGA_REG_GREV:  */
	case TGA_REG_GREV:
		odata = 0x04;		/*  01,02,03,04 (rev0) and 20,21,22 (rev1) are allowed  */
		break;

	/*  Graphics Mode:  */
	case TGA_REG_GMOR:
		if (writeflag == MEM_WRITE)
			d->graphics_mode = idata;
		else
			odata = d->graphics_mode;
		break;

	/*  Pixel mask:  */
	case TGA_REG_GPXR_S:		/*  "one-shot"  */
	case TGA_REG_GPXR_P:		/*  persistant  */
		if (writeflag == MEM_WRITE)
			d->pixel_mask = idata;
		else
			odata = d->pixel_mask;
		break;

	/*  Horizonsal size:  */
	case TGA_REG_VHCR:
		odata = dec21030_default_xsize / 4;	/*  lowest 9 bits  */
		break;

	/*  Vertical size:  */
	case TGA_REG_VVCR:
		odata = dec21030_default_ysize;		/*  lowest 11 bits  */
		break;

	/*  Block copy source:  */
	case TGA_REG_GCSR:
		d->copy_source = idata;
		debug("[ dec21030: block copy source = 0x%08x ]\n", idata);
		break;

	/*  Block copy destination:  */
	case TGA_REG_GCDR:
		debug("[ dec21030: block copy destination = 0x%08x ]\n", idata);
		newlen = 64;
		/*  Both source and destination are raw framebuffer addresses, offset by 0x1000.  */
		dev_fb_access(cpu, mem, d->copy_source - 0x1000, buf2, newlen, MEM_READ,  d->vfb_data);
		dev_fb_access(cpu, mem, idata          - 0x1000, buf2, newlen, MEM_WRITE, d->vfb_data);
		break;

	default:
		if (writeflag == MEM_WRITE)
			debug("[ dec21030: unimplemented write to address 0x%x (=reg 0x%x), data=0x%02x ]\n", relative_addr, reg, idata);
		else
			debug("[ dec21030: unimplemented read from address 0x%x (=reg 0x%x) ]\n", relative_addr, reg);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  pci_dec21030_init():
 */
void pci_dec21030_init(struct cpu *cpu, struct memory *mem)
{
	struct dec21030_data *d;

	d = malloc(sizeof(struct dec21030_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct dec21030_data));

	/*  TODO:  this address is based on what NetBSD/arc uses...  fix this  */
	memory_device_register(mem, "dec21030", 0x100000000000, 128*1048576, dev_dec21030_access, d);

	/*  TODO:  I have no idea about how/where this framebuffer should be in relation to the pci device  */
	d->vfb_data = dev_fb_init(cpu, mem, FRAMEBUFFER_PADDR, VFB_GENERIC,
	    dec21030_default_xsize, dec21030_default_ysize,
	    dec21030_default_xsize, dec21030_default_ysize, 8, "TGA");
}

