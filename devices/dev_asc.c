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
 *  $Id: dev_asc.c,v 1.31 2004/06/28 00:55:23 debug Exp $
 *
 *  'asc' SCSI controller for some DECsystems.
 *
 *  Supposed to support SCSI-1 and SCSI-2. I've not yet found any docs
 *  on NCR53C9X, so I'll try to implement this device from LSI53CF92A docs
 *  instead.
 *
 *	NCR53C94 registers	at base + 0
 *	DMA address register	at base + 0x40000
 *	128K SRAM buffer	at base + 0x80000
 *	ROM			at base + 0xc0000
 *
 *
 *  TODO:  This module needs a clean-up, and some testing to see that
 *         it works will all OSes that might use it (NetBSD, OpenBSD,
 *         Ultrix, Linux, Mach(?), OSF/1?, ...)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"
#include "diskimage.h"

#include "ncr53c9xreg.h"


/* #define ASC_DEBUG */

#define	ASC_FIFO_LEN		16
#define	STATE_DISCONNECTED	0
#define	STATE_INITIATOR		1
#define	STATE_TARGET		2

#define	PHASE_DATA_OUT		0
#define	PHASE_DATA_IN		1
#define	PHASE_COMMAND		2
#define	PHASE_STATUS		3
#define	PHASE_MSG_OUT		6
#define	PHASE_MSG_IN		7

extern int quiet_mode;
extern int instruction_trace;


/*  The controller's SCSI id:  */
#define	ASC_SCSI_ID		7

struct asc_data {
	int		irq_nr;
	int		irq_caused_last_time;

	/*  Current state and transfer:  */
	int		cur_state;
	int		cur_phase;
	struct scsi_transfer *xferp;

	/*  FIFO:  */
	unsigned char	fifo[ASC_FIFO_LEN];
	int		fifo_in;
	int		fifo_out;
	int		n_bytes_in_fifo;		/*  cached  */

	/*  ATN signal:  */
	int		atn;

	/*  Incoming dma data:  */
	unsigned char	*incoming_data;
	int		incoming_len;
	int		incoming_data_addr;

	/*  DMA:  */
	uint32_t	dma_address_reg;
	unsigned char	dma[128 * 1024];

	/*  Read registers and write registers:  */
	uint32_t	reg_ro[0x10];
	uint32_t	reg_wo[0x10];
};

/*  (READ/WRITE name, if split)  */
char *asc_reg_names[0x10] = {
	"NCR_TCL", "NCR_TCM", "NCR_FIFO", "NCR_CMD",
	"NCR_STAT/NCR_SELID", "NCR_INTR/NCR_TIMEOUT", "NCR_STEP/NCR_SYNCTP", "NCR_FFLAG/NCR_SYNCOFF",
	"NCR_CFG1", "NCR_CCF", "NCR_TEST", "NCR_CFG2",
	"NCR_CFG3", "reg_0xd", "NCR_TCH", "reg_0xf"
};


/*  This is referenced below.  */
int dev_asc_select(struct asc_data *d, int from_id, int to_id, int dmaflag, int n_messagebytes);


/*
 *  dev_asc_tick():
 *
 *  This function is called "every now and then" from the CPU
 *  main loop.
 */
void dev_asc_tick(struct cpu *cpu, void *extra)
{
	struct asc_data *d = extra;

	if (d->reg_ro[NCR_STAT] & NCRSTAT_INT)
		cpu_interrupt(cpu, d->irq_nr);
}


/*
 *  dev_asc_fifo_flush():
 *
 *  Flush the fifo.
 */
void dev_asc_fifo_flush(struct asc_data *d)
{
	d->fifo[0] = 0x00;
	d->fifo_in = 0;
	d->fifo_out = 0;
	d->n_bytes_in_fifo = 0;
}


/*
 *  dev_asc_reset():
 *
 *  Reset the state of the asc.
 */
void dev_asc_reset(struct asc_data *d)
{
	d->cur_state = STATE_DISCONNECTED;
	d->atn = 0;

	if (d->xferp != NULL)
		scsi_transfer_free(d->xferp);
	d->xferp = NULL;

	dev_asc_fifo_flush(d);

	/*  According to table 4.1 in the LSI53CF92A manual:  */
	memset(d->reg_wo, 0, sizeof(d->reg_wo));
	d->reg_wo[NCR_TCH] = 0x94;
	d->reg_wo[NCR_CCF] = 2;
	memcpy(d->reg_ro, d->reg_wo, sizeof(d->reg_ro));
	d->reg_wo[NCR_SYNCTP] = 5;
}


/*
 *  dev_asc_fifo_read():
 *
 *  Read a byte from the asc FIFO.
 */
int dev_asc_fifo_read(struct asc_data *d)
{
	int res = d->fifo[d->fifo_out];

	if (d->fifo_in == d->fifo_out)
		fatal("dev_asc: WARNING! FIFO overrun!\n");

	d->fifo_out = (d->fifo_out + 1) % ASC_FIFO_LEN;
	d->n_bytes_in_fifo --;

	return res;
}


/*
 *  dev_asc_fifo_write():
 *
 *  Write a byte to the asc FIFO.
 */
void dev_asc_fifo_write(struct asc_data *d, unsigned char data)
{
	d->fifo[d->fifo_in] = data;
	d->fifo_in = (d->fifo_in + 1) % ASC_FIFO_LEN;
	d->n_bytes_in_fifo ++;

	if (d->fifo_in == d->fifo_out)
		fatal("dev_asc: WARNING! FIFO overrun on write!\n");
}


/*
 *  dev_asc_newxfer():
 *
 *  Allocate memory for a new transfer.
 */
void dev_asc_newxfer(struct asc_data *d)
{
	if (d->xferp != NULL) {
		printf("WARNING! dev_asc_newxfer(): freeing previous"
		    " transfer\n");
		scsi_transfer_free(d->xferp);
		d->xferp = NULL;
	}

	d->xferp = scsi_transfer_alloc();
#if 0
	d->xferp->get_data_out = dev_asc_get_data_out;
	d->xferp->gdo_extra = (void *) d;
#endif
}


/*
 *  dev_asc_transfer():
 *
 *  Transfer data from a SCSI device to the controller (or vice versa),
 *  depending on the current phase.
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_asc_transfer(struct asc_data *d, int dmaflag)
{
	int res = 1, all_done = 1;
	int len, i, ch;

	debug(" { TRANSFER to/from id %i: ", d->reg_wo[NCR_SELID] & 7);

	if (d->cur_phase == PHASE_DATA_IN) {
		/*  Data coming into the controller from external device:  */
		if (!dmaflag) {
			if (d->xferp->data_in == NULL) {
				fatal("no incoming data?\n");
				res = 0;
			} else {
/*  TODO  */
fatal("TODO..............\n");
				len = d->reg_wo[NCR_TCL] + d->reg_wo[NCR_TCM] * 256;

				len--;
				ch = d->incoming_data[d->incoming_data_addr];
				debug(" %02x", ch);

				d->incoming_data_addr ++;
				dev_asc_fifo_write(d, ch);

				if (len == 0) {
					free(d->incoming_data);
					d->incoming_data = NULL;
				}

				d->reg_ro[NCR_TCL] = len & 255;
				d->reg_ro[NCR_TCM] = (len >> 8) & 255;
			}
		} else {
			/*  Copy from the incoming data into dma memory:  */
			if (d->xferp->data_in == NULL) {
				fatal("no incoming DMA data?\n");
				res = 0;
			} else {
				int len = d->xferp->data_in_len;
				int len2 = d->reg_wo[NCR_TCL] + d->reg_wo[NCR_TCM] * 256;
				if (len2 == 0)
					len2 = 65536;

                                if (len < len2) {
                                        fatal("{ asc: data in, len=%i len2=%i }\n", len, len2);
                                }

				/*  TODO: check len2 in a similar way?  */
				if (len + (d->dma_address_reg & ((sizeof(d->dma)-1))) > sizeof(d->dma))
					len = sizeof(d->dma) - (d->dma_address_reg & ((sizeof(d->dma)-1)));

				if (len2 > len)
					memset(d->dma + (d->dma_address_reg & ((sizeof(d->dma)-1))), 0, len2);

#ifdef ASC_DEBUG
				if (!quiet_mode) {
					int i;
					for (i=0; i<len; i++)
						debug(" %02x", d->xferp->data_in[i]);
				}
#endif

				memcpy(d->dma + (d->dma_address_reg & ((sizeof(d->dma)-1))), d->xferp->data_in, len2);

				if (d->xferp->data_in_len > len2) {
					unsigned char *n;

					all_done = 0;
					/*  fatal("{ asc: multi-transfer data_in, len=%i len2=%i }\n", len, len2);  */

					d->xferp->data_in_len -= len2;
					n = malloc(d->xferp->data_in_len);
					if (n == NULL) {
						fprintf(stderr, "out of memory in dev_asc\n");
						exit(1);
					}
					memcpy(n, d->xferp->data_in + len2, d->xferp->data_in_len);
					free(d->xferp->data_in);
					d->xferp->data_in = n;

					len = len2;
				}

				len = 0;

				d->reg_ro[NCR_TCL] = len & 255;
				d->reg_ro[NCR_TCM] = (len >> 8) & 255;

				/*  Successful DMA transfer:  */
				d->reg_ro[NCR_STAT] |= NCRSTAT_TC;
			}
		}
	} else if (d->cur_phase == PHASE_DATA_OUT) {
		/*  Data going from the controller to an external device:  */
		if (!dmaflag) {
fatal("TODO.......asdgasin\n");
		} else {
			/*  Copy data from DMA to data_out:  */
			int len = d->xferp->data_out_len;
			int len2 = d->reg_wo[NCR_TCL] + d->reg_wo[NCR_TCM] * 256;
			if (len2 == 0)
				len2 = 65536;

			if (len == 0) {
				fprintf(stderr, "d->xferp->data_out_len == 0 ?\n");
				exit(1);
			}

			/*  TODO: Make sure that len2 doesn't go outside of the dma memory?  */

			/*  fatal("    data out offset=%5i len=%5i\n", d->xferp->data_out_offset, len2);  */

			if (d->xferp->data_out == NULL) {
				scsi_transfer_allocbuf(&d->xferp->data_out_len, &d->xferp->data_out, len);
				memcpy(d->xferp->data_out, d->dma + (d->dma_address_reg & ((sizeof(d->dma)-1))), len2);
				d->xferp->data_out_offset = len2;
			} else {
				/*  Continuing a multi-transfer:  */
				memcpy(d->xferp->data_out + d->xferp->data_out_offset, d->dma + (d->dma_address_reg & ((sizeof(d->dma)-1))), len2);
				d->xferp->data_out_offset += len2;
			}

			/*  If the disk wants more than we're DMAing, then this is a multitransfer:  */
			if (d->xferp->data_out_offset != d->xferp->data_out_len) {
				debug("[ asc: data_out, multitransfer len = %i, len2 = %i ]\n", len, len2);
				if (d->xferp->data_out_offset > d->xferp->data_out_len)
					fatal("[ asc data_out dma: too much? ]\n");
				else
					all_done = 0;
			}

#ifdef ASC_DEBUG
			if (!quiet_mode) {
				int i;
				for (i=0; i<len; i++)
					debug(" %02x", d->xferp->data_out[i]);
			}
#endif
			len = 0;

			d->reg_ro[NCR_TCL] = len & 255;
			d->reg_ro[NCR_TCM] = (len >> 8) & 255;

			/*  Successful DMA transfer:  */
			d->reg_ro[NCR_STAT] |= NCRSTAT_TC;
		}
	} else if (d->cur_phase == PHASE_MSG_OUT) {
		debug("MSG OUT: ");
		/*  Data going from the controller to an external device:  */
		if (!dmaflag) {
			/*  There should already be one byte in msg_out, so we
			    just extend the message:  */
			int oldlen = d->xferp->msg_out_len;
			int newlen;

			if (oldlen != 1) {
				fatal(" (PHASE OUT MSG len == %i, should be 1)\n",
				    oldlen);
			}

			newlen = oldlen + d->n_bytes_in_fifo;
			d->xferp->msg_out = realloc(d->xferp->msg_out, newlen);
			d->xferp->msg_out_len = newlen;
			if (d->xferp->msg_out == NULL) {
				fprintf(stderr, "out of memory realloc'ing msg_out\n");
				exit(1);
			}

			i = oldlen;
			while (d->fifo_in != d->fifo_out) {
				ch = dev_asc_fifo_read(d);
				d->xferp->msg_out[i++] = ch;
#ifdef ASC_DEBUG
				debug("%02x ", ch);
#endif
			}
		} else {
			/*  Copy data from DMA to msg_out:  */

			/*  TODO  */
			res = 0;
		}
	} else if (d->cur_phase == PHASE_COMMAND) {
		debug(" COMMAND ==> select ");
		res = dev_asc_select(d, d->reg_ro[NCR_CFG1] & 7, d->reg_wo[NCR_SELID] & 7, dmaflag, 0);
		return res;
	} else {
		fatal("!!! TODO: unknown/unimplemented phase in transfer: %i\n", d->cur_phase);
	}

	/*  Redo the command if data was just send using DATA_OUT:  */
	if (d->cur_phase == PHASE_DATA_OUT) {
		res = diskimage_scsicommand(d->reg_wo[NCR_SELID] & 7, d->xferp);
	}

	if (all_done) {
		if (d->cur_phase == PHASE_MSG_OUT)
			d->cur_phase = PHASE_COMMAND;
		else
			d->cur_phase = PHASE_STATUS;
	}

	/*  Cause an interrupt after the transfer:  */
	d->reg_ro[NCR_STAT] |= NCRSTAT_INT;
	d->reg_ro[NCR_INTR] |= NCRINTR_FC;
	d->reg_ro[NCR_INTR] |= NCRINTR_BS;
	d->reg_ro[NCR_STAT] = (d->reg_ro[NCR_STAT] & ~7) | d->cur_phase;
	d->reg_ro[NCR_STEP] = (d->reg_ro[NCR_STEP] & ~7) | 4;	/*  4?  */

	debug("}");
	return res;
}


/*
 *  dev_asc_select():
 *
 *  Select a SCSI device, send msg bytes (if any), and send command bytes.
 *  (Call diskimage_scsicommand() to handle the command.)
 *
 *  Return value: 1 if ok, 0 on error.
 */
int dev_asc_select(struct asc_data *d, int from_id, int to_id,
		int dmaflag, int n_messagebytes)
{
	int ok, len, i, ch;

	debug(" { SELECT id %i: ", to_id);

	/*
	 *  Message bytes, if any:
	 */
	debug("msg:");
	if (n_messagebytes > 0) {
		scsi_transfer_allocbuf(&d->xferp->msg_out_len,
		    &d->xferp->msg_out, n_messagebytes);

		i = 0;
		while (n_messagebytes-- > 0) {
			int ch = dev_asc_fifo_read(d);
			debug(" %02x", ch);
			d->xferp->msg_out[i++] = ch;
		}

		if ((d->xferp->msg_out[0] & 0x7) != 0x00) {
			debug(" (LUNs not implemented yet: 0x%02x) }",
			    d->xferp->msg_out[0]);
			return 0;
		}

		if ((d->xferp->msg_out[0] & ~0x7) != 0xc0) {
			fatal(" (Unimplemented msg out: 0x%02x) }",
			    d->xferp->msg_out[0]);
			return 0;
		}

		if (d->xferp->msg_out_len > 1) {
			fatal(" (Long msg out, not implemented yet;"
			    " len=%i) }", d->xferp->msg_out_len);
			return 0;
		}
	} else {
		debug(" none");
	}

	/*  Special case: SELATNS (with STOP sequence):  */
	if (d->cur_phase == PHASE_MSG_OUT) {
		debug(" MSG OUT DEBUG");
		if (d->xferp->msg_out_len != 1) {
			fatal(" (SELATNS: msg out len == %i, should be 1)",
			    d->xferp->msg_out_len);
			return 0;
		}

/* d->cur_phase = PHASE_COMMAND; */

		/*  According to the LSI manual:  */
		d->reg_ro[NCR_STAT] |= NCRSTAT_INT;
		d->reg_ro[NCR_INTR] |= NCRINTR_FC;
		d->reg_ro[NCR_INTR] |= NCRINTR_BS;
		d->reg_ro[NCR_STAT] = (d->reg_ro[NCR_STAT] & ~7) | d->cur_phase;
		d->reg_ro[NCR_STEP] = (d->reg_ro[NCR_STEP] & ~7) | 1;

		debug("}");
		return 1;
	}

	/*
	 *  Command bytes:
	 */
	debug(", cmd: ");
	if (!dmaflag) {
		debug("[non-DMA] ");

		scsi_transfer_allocbuf(&d->xferp->cmd_len,
		    &d->xferp->cmd, d->n_bytes_in_fifo);

		i = 0;
		while (d->fifo_in != d->fifo_out) {
			ch = dev_asc_fifo_read(d);
			d->xferp->cmd[i++] = ch;
			debug("%02x ", ch);
		}
	} else {
		debug("[DMA] ");
		len = d->reg_wo[NCR_TCL] + d->reg_wo[NCR_TCM] * 256;
		if (len == 0)
			len = 65536;

		scsi_transfer_allocbuf(&d->xferp->cmd_len,
		    &d->xferp->cmd, len);

		for (i=0; i<len; i++) {
			int ofs = d->dma_address_reg + i;
			ch = d->dma[ofs & (sizeof(d->dma)-1)];
			d->xferp->cmd[i] = ch;
			debug("%02x ", ch);
		}

		d->reg_ro[NCR_TCL] = len & 255;
		d->reg_ro[NCR_TCM] = (len >> 8) & 255;

		d->reg_ro[NCR_STAT] |= NCRSTAT_TC;
	}

	/*
	 *  Call the SCSI device to perform the command:
	 */
	ok = diskimage_scsicommand(to_id, d->xferp);


	/*  Cause an interrupt:  */
	d->reg_ro[NCR_STAT] |= NCRSTAT_INT;
	d->reg_ro[NCR_INTR] |= NCRINTR_FC;
	d->reg_ro[NCR_INTR] |= NCRINTR_BS;

	if (ok == 2)
		d->cur_phase = PHASE_DATA_OUT;
	else if (d->xferp->data_in != NULL)
		d->cur_phase = PHASE_DATA_IN;
	else
		d->cur_phase = PHASE_STATUS;

	d->reg_ro[NCR_STAT] = (d->reg_ro[NCR_STAT] & ~7) | d->cur_phase;
	d->reg_ro[NCR_STEP] = (d->reg_ro[NCR_STEP] & ~7) | 4;	/*  DONE (?)  */

	debug("}");

	return ok;
}


/*
 *  dev_asc_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_asc_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	int regnr;
	struct asc_data *d = extra;
	int target_exists;
	int n_messagebytes = 0;
	uint64_t idata = 0, odata = 0;


	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / 4;

	/*  Controller's ID is fixed:  */
	d->reg_ro[NCR_CFG1] = (d->reg_ro[NCR_CFG1] & ~7) | ASC_SCSI_ID;
	d->reg_ro[NCR_CFG3] = NCRF9XCFG3_CDB;

	d->reg_ro[NCR_FFLAG] = ((d->reg_ro[NCR_STEP] & 0x7) << 5)
	    + d->n_bytes_in_fifo;

	if (regnr < 0x10) {
		if (regnr == NCR_FIFO) {
			if (writeflag == MEM_WRITE)
				dev_asc_fifo_write(d, idata);
			else
				odata = dev_asc_fifo_read(d);
		} else {
			if (writeflag==MEM_WRITE)
				d->reg_wo[regnr] = idata;
			else
				odata = d->reg_ro[regnr];
		}

		if (writeflag==MEM_READ) {
			debug("[ asc: read from %s: 0x%02x",
			    asc_reg_names[regnr], (int)odata);
		} else {
			debug("[ asc: write to  %s: 0x%02x",
			    asc_reg_names[regnr], (int)idata);
		}
	} else if (relative_addr == 0x40000) {
		if (writeflag==MEM_READ) {
			odata = d->dma_address_reg;
			debug("[ asc: read from DMA address reg: 0x%08x",
			    (int)odata);
		} else {
			d->dma_address_reg = idata;
			debug("[ asc: write to  DMA address reg: 0x%08x",
			    (int)idata);
		}
	} else if (relative_addr >= 0x80000 && relative_addr+len-1 <= 0x9ffff) {
		if (writeflag==MEM_READ) {
			memcpy(data, d->dma + (relative_addr - 0x80000), len);
#ifdef ASC_DEBUG
			{
				int i;
				debug("[ asc: read from DMA addr 0x%05x:",
				    relative_addr - 0x80000);
				for (i=0; i<len; i++)
					debug(" %02x", data[i]);
				debug(" ]\n");
			}
#endif

			/*  Don't return the common way, as that would overwrite data.  */
			return 1;
		} else {
			memcpy(d->dma + (relative_addr - 0x80000), data, len);
#ifdef ASC_DEBUG
			{
				int i;
				debug("[ asc: write to  DMA addr 0x%05x:",
				    relative_addr - 0x80000);
				for (i=0; i<len; i++)
					debug(" %02x", data[i]);
				debug(" ]\n");
			}
#endif

			/*  Quick return.  */
			return 1;
		}
	} else {
		if (writeflag==MEM_READ) {
			debug("[ asc: read from 0x%04x: 0x%02x",
			    relative_addr, (int)odata);
		} else {
			debug("[ asc: write to  0x%04x: 0x%02x",
			    relative_addr, (int)idata);
		}
	}

	/*
	 *  Some registers are read/write. Copy contents of
	 *  reg_wo to reg_ro:
	 */
#if 0
	d->reg_ro[ 0] = d->reg_wo[0];	/*  Transfer count lo and  */
	d->reg_ro[ 1] = d->reg_wo[1];	/*  middle  */
#endif
	d->reg_ro[ 2] = d->reg_wo[2];
	d->reg_ro[ 3] = d->reg_wo[3];
	d->reg_ro[ 8] = d->reg_wo[8];
	d->reg_ro[ 9] = d->reg_wo[9];
	d->reg_ro[10] = d->reg_wo[10];
	d->reg_ro[11] = d->reg_wo[11];
	d->reg_ro[12] = d->reg_wo[12];

	if (regnr == NCR_CMD && writeflag == MEM_WRITE) {
		debug(" ");

		if (idata & NCRCMD_DMA) {
			debug("[DMA] ");

			/*
			 *  DMA commands load the transfer count from the
			 *  write-only registers to the read-only ones, and
			 *  the Terminal Count bit is cleared.
			 */
			d->reg_ro[NCR_TCL] = d->reg_wo[NCR_TCL];
			d->reg_ro[NCR_TCM] = d->reg_wo[NCR_TCM];
			d->reg_ro[NCR_TCH] = d->reg_wo[NCR_TCH];
			d->reg_ro[NCR_STAT] &= ~NCRSTAT_TC;
		}

		switch (idata & ~NCRCMD_DMA) {

		case NCRCMD_NOP:
			debug("NOP");
			break;

		case NCRCMD_FLUSH:
			debug("FLUSH");
			/*  Flush the FIFO:  */
			dev_asc_fifo_flush(d);
			break;

		case NCRCMD_RSTCHIP:
			debug("RSTCHIP");
			/*  Hardware reset.  */
			dev_asc_reset(d);
			break;

		case NCRCMD_RSTSCSI:
			debug("RSTSCSI");
			/*  No interrupt if interrupts are disabled.  */
			if (!(d->reg_wo[NCR_CFG1] & NCRCFG1_SRR))
				d->reg_ro[NCR_STAT] |= NCRSTAT_INT;
			d->reg_ro[NCR_INTR] |= NCRINTR_SBR;
			d->reg_ro[NCR_INTR] |= NCRINTR_FC;
			d->cur_state = STATE_DISCONNECTED;
			break;

		case NCRCMD_ENSEL:
			debug("ENSEL");
			/*  TODO  */
			break;

		case NCRCMD_ICCS:
			debug("ICCS");
			/*  Reveice a status byte + a message byte.  */

			/*  TODO: how about other status and message bytes?  */
			dev_asc_fifo_write(d, d->xferp->status[0]);
			dev_asc_fifo_write(d, d->xferp->msg_in[0]);

			d->reg_ro[NCR_STAT] |= NCRSTAT_INT;
			d->reg_ro[NCR_INTR] |= NCRINTR_FC;
/*			d->reg_ro[NCR_INTR] |= NCRINTR_BS; */
			d->reg_ro[NCR_STAT] = (d->reg_ro[NCR_STAT] & ~7) | 7;	/*  ? probably 7  */
			d->reg_ro[NCR_STEP] = (d->reg_ro[NCR_STEP] & ~7) | 4;	/*  ?  */
			break;

		case NCRCMD_MSGOK:
			debug("MSGOK");
			/*  Message is being Rejected if ATN is set, otherwise Accepted.  */
			if (d->atn)
				debug("; Rejecting message");
			else
				debug("; Accepting message");
			d->reg_ro[NCR_STAT] |= NCRSTAT_INT;
/*			d->reg_ro[NCR_INTR] |= NCRINTR_FC; */
			d->reg_ro[NCR_INTR] |= NCRINTR_DIS;

			d->reg_ro[NCR_STAT] = (d->reg_ro[NCR_STAT] & ~7) | 6;	/*  ? probably 0  */
			d->reg_ro[NCR_STEP] = (d->reg_ro[NCR_STEP] & ~7) | 4;	/*  ?  */

			d->cur_state = STATE_DISCONNECTED;

			scsi_transfer_free(d->xferp);
			d->xferp = NULL;
			break;

		case NCRCMD_SETATN:
			debug("SETATN");
			d->atn = 1;
			break;

		case NCRCMD_RSTATN:
			debug("RSTATN");
			d->atn = 0;
			break;

		case NCRCMD_SELNATN:
		case NCRCMD_SELATN:
		case NCRCMD_SELATNS:
		case NCRCMD_SELATN3:
			d->cur_phase = PHASE_COMMAND;
			switch (idata & ~NCRCMD_DMA) {
			case NCRCMD_SELATN:
			case NCRCMD_SELATNS:
				if ((idata & ~NCRCMD_DMA) == NCRCMD_SELATNS) {
					debug("SELATNS: select with atn and stop, id %i", d->reg_wo[NCR_SELID] & 7);
					d->cur_phase = PHASE_MSG_OUT;
				} else {
					debug("SELATN: select with atn, id %i", d->reg_wo[NCR_SELID] & 7);
				}
				n_messagebytes = 1;
				break;
			case NCRCMD_SELATN3:
				debug("SELNATN: select with atn3, id %i", d->reg_wo[NCR_SELID] & 7);
				n_messagebytes = 3;
				break;
			case NCRCMD_SELNATN:
				debug("SELNATN: select without atn, id %i", d->reg_wo[NCR_SELID] & 7);
				n_messagebytes = 0;
			}

			/*  TODO: not just disk, but some generic SCSI device  */
			target_exists = diskimage_exist(d->reg_wo[NCR_SELID] & 7);

			if (target_exists) {
				/*
				 *  Select a SCSI device, send message bytes
				 *  (if any) and command bytes to the target.
				 */
				int ok;

				dev_asc_newxfer(d);

				ok = dev_asc_select(d,
				    d->reg_ro[NCR_CFG1] & 7,
				    d->reg_wo[NCR_SELID] & 7,
				    idata & NCRCMD_DMA? 1 : 0,
				    n_messagebytes);

				if (ok)
					d->cur_state = STATE_INITIATOR;
				else {
					d->cur_state = STATE_DISCONNECTED;
					d->reg_ro[NCR_INTR] |= NCRINTR_DIS;
					d->reg_ro[NCR_STAT] |= NCRSTAT_INT;
					d->reg_ro[NCR_STEP] = (d->reg_ro[NCR_STEP] & ~7) | 0;
					scsi_transfer_free(d->xferp);
					d->xferp = NULL;
				}
			} else {
				/*
				 *  Selection failed, non-existant scsi ID:
				 *
				 *  This is good enough to fool Ultrix, NetBSD,
				 *  OpenBSD and Linux to continue detection of
				 *  other IDs, without giving any warnings.
				 */
				d->reg_ro[NCR_STAT] |= NCRSTAT_INT;
				d->reg_ro[NCR_INTR] |= NCRINTR_DIS;
				d->reg_ro[NCR_STEP] &= ~7;
				d->reg_ro[NCR_STEP] |= 0;
				dev_asc_fifo_flush(d);
				d->cur_state = STATE_DISCONNECTED;
			}
			break;

		case NCRCMD_TRPAD:
			debug("TRPAD");

			if (d->cur_state == STATE_DISCONNECTED) {
				fatal("[ dev_asc: TRPAD: bad state ]\n");
				break;
			}

			d->reg_ro[NCR_STAT] |= NCRSTAT_INT;
			d->reg_ro[NCR_INTR] |= NCRINTR_BS;
			d->reg_ro[NCR_INTR] |= NCRINTR_FC;
			d->reg_ro[NCR_STAT] |= NCRSTAT_TC;

			d->reg_ro[NCR_TCL] = 0;
			d->reg_ro[NCR_TCM] = 0;

			d->reg_ro[NCR_STEP] &= ~7;
#if 0
			d->reg_ro[NCR_STEP] |= 0;
			dev_asc_fifo_flush(d);
#else
			d->reg_ro[NCR_STEP] |= 4;
#endif
			break;

		case NCRCMD_TRANS:
			debug("TRANS");

			if (d->cur_state == STATE_DISCONNECTED) {
				fatal("[ dev_asc: TRANS: bad state ]\n");
				break;
			}

			{
				int ok;

				ok = dev_asc_transfer(d, idata & NCRCMD_DMA? 1 : 0);
				if (!ok) {
					d->cur_state = STATE_DISCONNECTED;
					d->reg_ro[NCR_INTR] |= NCRINTR_DIS;
					d->reg_ro[NCR_STAT] |= NCRSTAT_INT;
					d->reg_ro[NCR_STEP] = (d->reg_ro[NCR_STEP] & ~7) | 0;
					scsi_transfer_free(d->xferp);
					d->xferp = NULL;
				}
			}
			break;

		default:
			fatal("(unimplemented asc cmd 0x%02x)", (int)idata);
			d->reg_ro[NCR_STAT] |= NCRSTAT_INT;
			d->reg_ro[NCR_INTR] |= NCRINTR_ILL;
			/*
			 *  TODO:  exit or continue with Illegal command
			 *  interrupt?
			 */
			exit(1);
		}
	}

	if (regnr == NCR_INTR && writeflag == MEM_READ) {
		/*
		 *  Reading the interrupt register de-asserts the
		 *  interrupt pin.  Also, INTR, STEP, and STAT are all
		 *  cleared, according to page 64 of the LSI53CF92A manual,
		 *  if "interrupt output is true".
		 */
		if (d->reg_ro[NCR_STAT] & NCRSTAT_INT) {
			d->reg_ro[NCR_INTR] = 0;
			d->reg_ro[NCR_STEP] = 0;
			d->reg_ro[NCR_STAT] = 0;
		}

		cpu_interrupt_ack(cpu, d->irq_nr);
	}

	if (regnr == NCR_CFG1) {
		/*  TODO: other bits  */
		debug(" parity %s,", d->reg_ro[regnr] & NCRCFG1_PARENB? "enabled" : "disabled");
		debug(" scsi_id %i", d->reg_ro[regnr] & 0x7);
	}

	debug(" ]\n");

	dev_asc_tick(cpu, extra);

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_asc_init():
 *
 *  Register an 'asc' device.
 */
void dev_asc_init(struct cpu *cpu, struct memory *mem,
	uint64_t baseaddr, int irq_nr)
{
	struct asc_data *d;

	d = malloc(sizeof(struct asc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct asc_data));
	d->irq_nr = irq_nr;

	memory_device_register(mem, "asc", baseaddr, DEV_ASC_LENGTH,
	    dev_asc_access, d);

	cpu_add_tickfunction(cpu, dev_asc_tick, d, 16);
}

