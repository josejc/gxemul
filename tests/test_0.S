/*
 *  $Id: test_0.S,v 1.3 2004/03/26 20:15:58 debug Exp $
 *
 *  A dummy test.
 */

.rdata
	.align	3
$TESTTEXT:
	.ascii	"Test text.\n"

.text
	.align	4
	.globl	testmain
	.ent	testmain
testmain:
	/*  Save stack pointer:  */
	addiu	$29,$29,-16
	sd	$31,0($29)

	/*  ADD ACTUAL TEST CODE HERE  */
	dla	$4, $TESTTEXT
	jal	printstr

	/*  Return value:  */
	li	$2, 0x12348765

	/*  Restore stack pointer:  */
	ld	$31,0($29)
	addiu	$29,$29,16

	/*  ... and return.  */
	jr	$31

	.end	testmain

