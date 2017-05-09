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
 *  $Id: test_common.c,v 1.1 2004/03/15 16:08:30 debug Exp $
 *
 *  This module contains common routines used by all the regression tests.
 *  Each test should be linked with this module, but have its own main()
 *  routine.
 */


/*
 *  printchar():
 *
 *  This function should print a character to the (serial) console.
 *  In the emulator, it is as simple as storing a byte at a specific
 *  address.
 */
#define	PUTCHAR_ADDRESS		0xb0000000
void printchar(char ch)
{
	*((volatile unsigned char *) PUTCHAR_ADDRESS) = ch;
}


/*
 *  printstr():
 */
void printstr(char *s)
{
	while (*s)
		printchar(*s++);
}


/*
 *  printhex():
 *
 *  Print a number in hex. len is the number of characters to output.
 *  (This should be sizeof(value).)
 */
void printhex(long n, int len)
{
	char *hex = "0123456789abcdef";
	int i;

	printstr("0x");
	for (i=len*2-1; i>=0; i--)
		printchar(hex[(n >> (4*i)) & 15]);
}


/*
 *  printdec():
 *
 *  Print a number in decimal form.
 */
void printdec(long x)
{
	if (x == 0)
		printchar('0');
	else if (x < 0) {
		printchar('-');
		printdec(-x);
	} else {
		int y = x / 10;
		if (y > 0)
			printdec(y);
		printchar('0' + (x % 10));
	}
}


/*
 *  main():
 */
int main(int argc, char *argv[])
{
	unsigned int x;

	x = testmain();

	printhex(x, 4);
	printstr("\n");

	/*  This should shut down the emulator:  */
	(*(volatile char *)0xb0000010) = 0;

	/*  This should never be reached:  */
	for (;;)
		;

	return 0;
}

