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
 *  $Id: do_tests.c,v 1.1 2004/03/15 16:08:30 debug Exp $
 *
 *  This program should assemble, link, and run all tests.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>


char *progname;
char *invoke_cc, *invoke_as, *invoke_ld, *invoke_mips64emul;


/*
 *  usage():
 */
void usage(void)
{
	fprintf(stderr, "usage: %s how_to_invoke_cc how_to_invoke_as how_to_invoke_ld\n", progname);
	fprintf(stderr, "For example, when using GCC compiled for the mips64-unknown-elf target:\n");
	fprintf(stderr, "    %s \"mips64-unknown-elf-gcc -g -O3 -fno-builtin -fschedule-insns -mips4 -mabi=64\"\n"
	    "        \"mips64-unknown-elf-as\"\n"
	    "        \"mips64-unknown-elf-ld -Ttext 0xffffffff80030000 -e main --oformat=elf64-bigmips\"\n", progname);
}


/*
 *  do_tests():
 *
 *  Returns 0 if ALL tests passed.
 *  Returns non-zero otherwise.
 */
int do_tests(void)
{
	int test_nr = 0;
	int tests_passed = 0, tests_failed = 0;

	printf("\nStarting tests:\n");

	for (;;) {
		char fname[200], oname[200], ename[200], gname[200];
		struct stat st;
		int res;

		sprintf(fname, "test_%i.S", test_nr);
		sprintf(oname, "test_%i.o", test_nr);
		sprintf(ename, "test_%i.out", test_nr);	/*  MIPS executable  */
		sprintf(gname, "test_%i.good", test_nr);
		res = stat(fname, &st);
		if (res == 0) {
			char s[10000];
			/*  File exists, let's assemble it:  */
			sprintf(s, "%s %s -o %s", invoke_as, fname, oname);
			res = system(s);
			if (res != 0) {
				printf("%05i: ERROR when invoking assembler, error code %i\n", test_nr, res);
				tests_failed ++;
			} else {
				sprintf(s, "%s test_common.o %s -o %s", invoke_ld, oname, ename);
				res = system(s);
				if (res != 0) {
					printf("%05i: ERROR when invoking linker, error code %i\n", test_nr, res);
					tests_failed ++;
				} else {
					char *t = tmpnam(NULL);
					/*  printf("t = %s\n", t);  */
					sprintf(s, "%s -q %s > %s", invoke_mips64emul, ename, t);
					res = system(s);
					if (res != 0) {
						printf("%05i: ERROR when invoking mips64emul, error code %i\n", test_nr, res);
						tests_failed ++;
					} else {
						/*  Compare the output to a known good test result:  */
						sprintf(s, "cmp -s %s %s", gname, t);
						res = system(s);
						if (res != 0) {
							printf("%05i: ERROR when comparing current output to known good output\n", test_nr);
							sprintf(s, "cat %s\n", gname);
							printf("%s\n", s);
							system(s);
							sprintf(s, "cat %s\n", t);
							printf("%s\n", s);
							system(s);
							tests_failed ++;
						} else {
							tests_passed ++;
						}
						remove(t);
					}
				}
			}
		} else
			break;

		test_nr ++;
	}

	printf("Ending tests (%i tests done)\n", test_nr);
	printf("    PASS: %6i\n", tests_passed);
	printf("    FAIL: %6i\n", tests_failed);

	if (tests_failed != 0) {
		printf("\n***  ONE OR MORE TESTS FAILED!!!  ***\n\n");
		return 1;
	} else if (tests_failed + tests_passed != test_nr) {
		printf("\n***  INTERNAL ERROR IN do_tests.c  ***\n\n");
		return 1;
	} else {
		printf("\nAll tests OK\n\n");
	}

	return 0;
}


/*
 *  main():
 */
int main(int argc, char *argv[])
{
	char s[10000];

	progname = argv[0];

	if (argc != 5) {
		usage();
		exit(1);
	}

	invoke_cc = argv[1];
	invoke_as = argv[2];
	invoke_ld = argv[3];
	invoke_mips64emul = argv[4];

	/*  fprintf(stderr, "%s: debug:\n"
	    "    invoke_cc = '%s'\n"
	    "    invoke_as = '%s'\n"
	    "    invoke_ld = '%s'\n"
	    "    invoke_mips64emul = '%s'\n",
	    progname, invoke_cc, invoke_as, invoke_ld, invoke_mips64emul);  */

	return do_tests();
}

