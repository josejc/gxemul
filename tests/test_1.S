/*
 *  $Id: test_1.S,v 1.2 2004/03/27 05:42:50 debug Exp $
 *
 *  Instruction: 'addu'
 */

.rdata
	.align	3
$NL:	.ascii	"\n"

.text
	.align	4
	.globl	testmain
	.ent	testmain
testmain:
	/*  Save stack pointer:  */
	addiu	$29,$29,-16
	sd	$31,0($29)


/*  TODO: s0 and s1  */
	dli	$8, 0x123456789abcdef
	dli	$9, 0xa8cb67df6bac0726


	move	$4, $8
	dli	$5, 8
	jal	printhex

	dla	$4, $NL
	jal	printstr

/*
	addu	$4, $8, $9

	dli	$5, 8
	jal	printhex

	dla	$4, $NL
	jal	printstr
*/

	/*  Return value:  */
	li	$2, 0x12348765

	/*  Restore stack pointer:  */
	ld	$31,0($29)
	addiu	$29,$29,16

	/*  ... and return.  */
	jr	$31

	.end	testmain

