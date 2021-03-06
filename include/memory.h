#ifndef	MEMORY_H
#define	MEMORY_H

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
 *  $Id: memory.h,v 1.8 2004/06/28 23:00:04 debug Exp $
 *
 *  Memory controller related functions.
 */

#include <sys/types.h>
#include <inttypes.h>

#include "misc.h"


/*  memory.c:  */
uint64_t memory_readmax64(struct cpu *cpu, unsigned char *buf, int len);
void memory_writemax64(struct cpu *cpu, unsigned char *buf, int len, uint64_t data);

struct memory *memory_new(int bits_per_pagetable, int bits_per_memblock, uint64_t physical_max, int max_bits);

int memory_points_to_string(struct cpu *cpu, struct memory *mem, uint64_t addr, int min_string_length);
char *memory_conv_to_string(struct cpu *cpu, struct memory *mem, uint64_t addr, char *buf, int bufsize);

int memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr, unsigned char *data, size_t len, int writeflag, int cache);
#define	MEMORY_ACCESS_FAILED	0
#define	MEMORY_ACCESS_OK	1
#define	INSTR_BINTRANS		2

void memory_device_register(struct memory *mem, const char *, uint64_t baseaddr, uint64_t len, int (*f)(
	struct cpu *,struct memory *,uint64_t,unsigned char *,size_t,int,void *), void *);


#endif	/*  MEMORY_H  */

