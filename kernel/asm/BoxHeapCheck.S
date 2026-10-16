#include <asm.h>

# Pokemon Box (GPXE01) heap watch, diagnostics only (Log on).
# PADReadGC calls this on every game PADRead. It walks the free and used
# lists of the game's main JKRExpHeap on the game CPU, which sees its own
# cached writes (the kernel can't, main memory may be stale), and leaves the
# first broken block for the kernel (BoxHeapReport in kernel/main.c).
# Leaf function, only uses volatile registers and cr0.

.set	REPORT,			0xD3154100	#uncached
	# +00 clean checks, +04 found, +08 block, +0C previous, +10 0x10000 - index,
	# +14 list tail, +18 list (0x78 free, 0x80 used),
	# +20 0x20 bytes at the previous block, +40 0x40 bytes around the block
.set	HEAP,			0x8057D4B0	#JKRExpHeap: +20 mutex owner, +30 start, +34 end,
									#+78/+7C free head/tail, +80/+84 used head/tail
.set	HEAP_VTABLE,	0x801EB0E0

BoxHeapCheck:
	lis		r3,	REPORT@h
	ori		r3,	r3,	REPORT@l
	lwz		r0,	4(r3)
	cmpwi	r0,	0
	bnelr						#already reported
	lis		r4,	HEAP@ha
	addi	r4,	r4,	HEAP@l
	lis		r5,	HEAP_VTABLE@ha
	addi	r5,	r5,	HEAP_VTABLE@l
	lwz		r0,	0(r4)
	cmplw	r0,	r5
	bnelr						#heap not created yet
	lwz		r0,	0x20(r4)
	cmpwi	r0,	0
	bnelr						#someone holds the heap's mutex, it may be mid-update
	lwz		r5,	0x30(r4)		#heap start
	lwz		r6,	0x34(r4)
	subf	r6,	r5,	r6
	subi	r6,	r6,	0x10		#highest valid header offset
	li		r7,	0x78			#free list first, then the used list

NextList:
	lwzx	r8,	r4,	r7			#block = head
	addi	r9,	r7,	4
	lwzx	r9,	r4,	r9			#tail
	li		r10,	0			#previous block
	lis		r0,	1
	mtctr	r0					#at most 0x10000 blocks, in case of a loop
	cmpwi	r8,	0
	beq		ListDone			#empty list

CheckBlock:
	subf	r11,	r5,	r8		#offset in the heap
	cmplw	r11,	r6
	bgt		Broken				#outside the heap (or null before the tail)
	andi.	r0,	r8,	3
	bne		Broken
	lwz		r0,	4(r8)			#size must fit in the heap
	subf	r12,	r11,	r6
	cmplw	r0,	r12
	bgt		Broken
	cmpwi	r10,	0
	beq		CheckMagic
	lwz		r0,	8(r8)			#its prev must be the block we came from
	cmplw	r0,	r10
	bne		Broken
CheckMagic:
	cmpwi	r7,	0x80
	bne		CheckTail			#free blocks carry no magic
	lhz		r0,	0(r8)
	cmplwi	r0,	0x484D		#'HM'
	bne		Broken
CheckTail:
	cmplw	r8,	r9
	beq		ListDone
	mr		r10,	r8
	lwz		r8,	0xC(r8)		#next
	bdnz	CheckBlock
	b		Broken

ListDone:
	addi	r7,	r7,	8
	cmpwi	r7,	0x88
	bne		NextList
	lwz		r11,	0(r3)		#count a clean check (addi can't use r0)
	addi	r11,	r11,	1
	stw		r11,	0(r3)
	blr

Broken:
	stw		r8,	0x08(r3)
	stw		r10,	0x0C(r3)
	mfctr	r0
	stw		r0,	0x10(r3)
	stw		r9,	0x14(r3)
	stw		r7,	0x18(r3)
	cmpwi	r10,	0
	beq		CopyAround
	li		r0,	8				#0x20 bytes at the previous block
	mtctr	r0
	addi	r11,	r3,	0x1C
	subi	r12,	r10,	4
CopyPrev:
	lwzu	r0,	4(r12)
	stwu	r0,	4(r11)
	bdnz	CopyPrev
CopyAround:
	subf	r11,	r5,	r8		#0x40 bytes around the block, if it is in the heap
	cmplw	r11,	r6
	bgt		Report
	li		r0,	16
	mtctr	r0
	addi	r11,	r3,	0x3C
	subi	r12,	r8,	0x24
CopyBlock:
	lwzu	r0,	4(r12)
	stwu	r0,	4(r11)
	bdnz	CopyBlock
Report:
	eieio
	li		r0,	1
	stw		r0,	4(r3)
	blr
