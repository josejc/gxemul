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
 *  $Id: dec_prom.c,v 1.10 2004/07/01 11:46:03 debug Exp $
 *
 *  DECstation PROM emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "diskimage.h"
#include "memory.h"
#include "misc.h"
#include "console.h"

#include "dec_5100.h"
#include "dec_kn01.h"
#include "dec_kn02.h"
#include "dec_kn03.h"


extern int machine;
extern int register_dump;
extern int instruction_trace;
extern int show_nr_of_instructions;
extern int quiet_mode;
extern int use_x11;


/*
 *  decstation_prom_emul():
 *
 *  DECstation PROM emulation.
 *
 *	0x0c	strcmp()
 *	0x14	strlen()
 *	0x24	getchar()
 *	0x30	printf()
 *	0x54	bootinit()
 *	0x58	bootread()
 *	0x64	getenv()
 *	0x6c	slot_address()
 *	0x7c	clear_cache()
 *	0x80	getsysid()
 *	0x84	getbitmap()
 *	0x9c	halt()
 *	0xa4	gettcinfo()
 *	0xac	rex()
 */
void decstation_prom_emul(struct cpu *cpu)
{
	int i, j, ch, argreg, argdata;
	int vector = cpu->pc & 0xffff;
	unsigned char buf[100];
	unsigned char ch1, ch2;
	uint64_t slot_base = 0x10000000, slot_size = 0;

	switch (vector) {
	case 0x0c:		/*  strcmp():  */
		i = j = 0;
		do {
			ch1 = read_char_from_memory(cpu, GPR_A0, i++);
			ch2 = read_char_from_memory(cpu, GPR_A1, j++);
		} while (ch1 == ch2 && ch1 != '\0');

		/*  If ch1=='\0', then strings are equal.  */
		if (ch1 == '\0')
			cpu->gpr[GPR_V0] = 0;
		if ((signed char)ch1 > (signed char)ch2)
			cpu->gpr[GPR_V0] = 1;
		if ((signed char)ch1 < (signed char)ch2)
			cpu->gpr[GPR_V0] = -1;
		break;
	case 0x14:		/*  strlen():  */
		i = 0;
		do {
			ch2 = read_char_from_memory(cpu, GPR_A0, i++);
		} while (ch2 != 0);
		cpu->gpr[GPR_V0] = i - 1;
		break;
	case 0x24:		/*  getchar()  */
		/*  debug("[ DEC PROM getchar() ]\n");  */
		cpu->gpr[GPR_V0] = console_readchar();
		break;
	case 0x30:		/*  printf()  */
		if (register_dump || instruction_trace)
			debug("PROM printf(0x%08lx): \n", (long)cpu->gpr[GPR_A0]);

		i = 0; ch = -1; argreg = GPR_A1;
		while (ch != '\0') {
			ch = read_char_from_memory(cpu, GPR_A0, i++);
			switch (ch) {
			case '%':
				ch = read_char_from_memory(cpu, GPR_A0, i++);
				switch (ch) {
				case '%':
					printf("%%");
					break;
				case 'c':
				case 'd':
				case 's':
				case 'x':
					/*  Get argument:  */
					if (argreg > GPR_A3) {
						printf("[ decstation_prom_emul(): too many arguments ]");
						argreg = GPR_A3;	/*  This reuses the last argument,
								which is utterly incorrect. (TODO)  */
					}
					ch2 = argdata = cpu->gpr[argreg];

					switch (ch) {
					case 'c':
						printf("%c", ch2);
						break;
					case 'd':
						printf("%d", argdata);
						break;
					case 'x':
						printf("%x", argdata);
						break;
					case 's':
						/*  Print a "%s" string.  */
						while (ch2) {
							ch2 = read_char_from_memory(cpu, argreg, i++);
							if (ch2)
								printf("%c", ch2);
						}
						/*  TODO:  without this newline, output looks ugly  */
						printf("\n");
						break;
					}
					argreg ++;
					break;
				default:
					printf("[ unknown printf format char '%c' ]", ch);
				}
				break;
			case '\0':
				break;
			default:
				printf("%c", ch);
			}
		}
		if (register_dump || instruction_trace)
			debug("\n");
		fflush(stdout);
		break;
	case 0x54:		/*  bootinit()  */
		/*  debug("[ DEC PROM bootinit(0x%08x): TODO ]\n", (int)cpu->gpr[GPR_A0]);  */
		cpu->gpr[GPR_V0] = 0;
		break;
	case 0x58:		/*  bootread(int b, void *buffer, int n)  */
		/*
		 *  Read data from the boot device.
		 *  b is a sector number (512 bytes per sector),
		 *  buffer is the destination address, and n
		 *  is the number of _bytes_ to read.
		 *
		 *  TODO: Return value? NetBSD thinks that 0 is ok.
		 */
		/*  debug("[ DEC PROM bootread(0x%x, 0x%08x, 0x%x) ]\n",
		    (int)cpu->gpr[GPR_A0], (int)cpu->gpr[GPR_A1], (int)cpu->gpr[GPR_A2]);  */

		cpu->gpr[GPR_V0] = 0;

		if ((int32_t)cpu->gpr[GPR_A2] > 0) {
			int disk_id = diskimage_bootdev();
			int res;
			unsigned char *tmp_buf;

			tmp_buf = malloc(cpu->gpr[GPR_A2]);
			if (tmp_buf == NULL) {
				fprintf(stderr, "[ ***  Out of memory in dec_prom.c, allocating %i bytes ]\n", (int)cpu->gpr[GPR_A2]);
				break;
			}

			res = diskimage_access(disk_id, 0, cpu->gpr[GPR_A0] * 512, tmp_buf, cpu->gpr[GPR_A2]);

			/*  If the transfer was successful, transfer the data to emulated memory:  */
			if (res) {
				store_buf(cpu->gpr[GPR_A1], tmp_buf, cpu->gpr[GPR_A2]);
				cpu->gpr[GPR_V0] = cpu->gpr[GPR_A2];
			}

			free(tmp_buf);
		}
		break;
	case 0x64:		/*  getenv()  */
		/*  Find the environment variable given by a0:  */
		for (i=0; i<sizeof(buf); i++)
			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0] + i, &buf[i], sizeof(char), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
		buf[sizeof(buf)-1] = '\0';
		debug("[ DEC PROM getenv(\"%s\") ]\n", buf);
		for (i=0; i<0x1000; i++) {
			/*  Matching string at offset i?  */
			int nmatches = 0;
			for (j=0; j<strlen((char *)buf); j++) {
				memory_rw(cpu, cpu->mem, (uint64_t)(DEC_PROM_STRINGS + i + j), &ch2, sizeof(char), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
				if (ch2 == buf[j])
					nmatches++;
			}
			memory_rw(cpu, cpu->mem, (uint64_t)(DEC_PROM_STRINGS + i + strlen((char *)buf)), &ch2, sizeof(char), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
			if (nmatches == strlen((char *)buf) && ch2 == '=') {
				cpu->gpr[GPR_V0] = DEC_PROM_STRINGS + i + strlen((char *)buf) + 1;
				return;
			}
		}
		/*  Return NULL if string wasn't found.  */
		fatal("[ DEC PROM getenv(\"%s\"): WARNING: Not in environment! ]\n", buf);
		cpu->gpr[GPR_V0] = 0;
		break;
	case 0x6c:		/*  ulong slot_address(int sn)  */
		debug("[ DEC PROM slot_address(%i) ]\n", (int)cpu->gpr[GPR_A0]);
		/*  TODO:  This is too hardcoded.  */
		/*  TODO 2:  Should these be physical or virtual addresses?  */
		switch (machine) {
		case MACHINE_3MAX_5000:
			slot_base = KN02_PHYS_TC_0_START;	/*  0x1e000000  */
			slot_size = 4*1048576;		/*  4 MB  */
			break;
		case MACHINE_3MIN_5000:
			slot_base = 0x10000000;
			slot_size = 0x4000000;		/*  64 MB  */
			break;
		case MACHINE_3MAXPLUS_5000:
			slot_base = 0x1e000000;
			slot_size = 0x800000;		/*  8 MB  */
			break;
		case MACHINE_MAXINE_5000:
			slot_base = 0x10000000;
			slot_size = 0x4000000;		/*  64 MB  */
			break;
		default:
			fatal("warning: DEC PROM slot_address() unimplemented for this machine type\n");
		}
		cpu->gpr[GPR_V0] = 0x80000000 + slot_base + slot_size * cpu->gpr[GPR_A0];
		break;
	case 0x7c:		/*  clear_cache(addr, len)  */
		debug("[ DEC PROM clear_cache(0x%x,%i) ]\n", (uint32_t)cpu->gpr[GPR_A0], (int)cpu->gpr[GPR_A1]);
		/*  TODO  */
		cpu->gpr[GPR_V0] = 0;	/*  ?  */
		break;
	case 0x80:		/*  getsysid()  */
		/*  debug("[ DEC PROM getsysid() ]\n");  */
		/*  TODO:  why did I add the 0x82 stuff???  */
		cpu->gpr[GPR_V0] = (0x82 << 24) + (machine << 16) + (0x3 << 8);
		break;
	case 0x84:		/*  getbitmap()  */
		debug("[ DEC PROM getbitmap(0x%08x) ]\n", (int)cpu->gpr[GPR_A0]);
		{
			unsigned char buf[sizeof(memmap)];
			memory_rw(cpu, cpu->mem, DEC_MEMMAP_ADDR, buf, sizeof(buf), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0], buf, sizeof(buf), MEM_WRITE, CACHE_NONE | NO_EXCEPTIONS);
		}
		cpu->gpr[GPR_V0] = sizeof((memmap.bitmap));
		break;
	case 0x9c:		/*  halt()  */
		debug("[ DEC PROM halt() ]\n");
		cpu->running = 0;
		break;
	case 0xa4:		/*  gettcinfo()  */
		/*  These are just bogus values...  TODO  */
		store_32bit_word(DEC_PROM_TCINFO +  0, 0);	/*  revision  */
		store_32bit_word(DEC_PROM_TCINFO +  4, 50);	/*  clock period in nano seconds  */
		store_32bit_word(DEC_PROM_TCINFO +  8, 4);	/*  slot size in megabytes  TODO: not same for all models!!  */
		store_32bit_word(DEC_PROM_TCINFO + 12, 10);	/*  I/O timeout in cycles  */
		store_32bit_word(DEC_PROM_TCINFO + 16, 1);	/*  DMA address range in megabytes  */
		store_32bit_word(DEC_PROM_TCINFO + 20, 100);	/*  maximum DMA burst length  */
		store_32bit_word(DEC_PROM_TCINFO + 24, 0);	/*  turbochannel parity (yes = 1)  */
		store_32bit_word(DEC_PROM_TCINFO + 28, 0);	/*  reserved  */
		cpu->gpr[GPR_V0] = DEC_PROM_TCINFO;
		break;
	case 0xac:		/*  rex()  */
		debug("[ DEC PROM rex('%c') ]\n", (int)cpu->gpr[GPR_A0]);
		switch (cpu->gpr[GPR_A0]) {
		case 'h':
			debug("DEC PROM: rex('h') ==> halt\n");
			cpu->running = 0;
			break;
		case 'b':
			fatal("DEC PROM: rex('b') ==> reboot: TODO (halting CPU instead)\n");
			cpu->running = 0;
			break;
		default:
			fatal("DEC prom emulation: unknown rex() a0=0x%llx ('%c')\n",
			    (long long)cpu->gpr[GPR_A0], (char)cpu->gpr[GPR_A0]);
			exit(1);
		}
		break;
	default:
		cpu_register_dump(cpu);
		printf("a0 points to: ");
		for (i=0; i<40; i++) {
			unsigned char ch = '\0';
			memory_rw(cpu, cpu->mem, cpu->gpr[GPR_A0] + i, &ch, sizeof(ch), MEM_READ, CACHE_NONE | NO_EXCEPTIONS);
			if (ch >= ' ' && ch < 126)
				printf("%c", ch);
			else
				printf("[%02x]", ch);
		}
		printf("\n");
		fatal("PROM emulation: unimplemented callback vector 0x%x\n", vector);
		cpu->running = 0;
	}
}

