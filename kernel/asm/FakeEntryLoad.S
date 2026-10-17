#include <asm.h>

.set	RESET_STATUS,	0xD3003420
.set	FLUSH_LEN,		0xD3003424
.set	FLUSH_ADDR,		0xD3003428
.set	EXTRA_ADDR,		0xD300342C	#optional range to invalidate (patches placed elsewhere)
.set	EXTRA_LEN,		0xD3003430	#in 32 byte lines, 0 for none
.set	ARENA_HI,		0xD3003434	#if set, the game's arena ends here (BootInfo arenaHi)

FakeEntryLoad:
	lis		r3,	RESET_STATUS@h
	li		r0,	0x4DEA
	stw		r0,	RESET_STATUS@l(r3)
wait_loop:
	lwz		r4,	RESET_STATUS@l(r3)
	cmplw	r4,	r0
	beq		wait_loop
	mtlr	r4

	lwz		r4,	FLUSH_LEN@l(r3)
	mtctr	r4

	lwz		r4,	FLUSH_ADDR@l(r3)
InvalidateRange1:
	dcbi	0,	r4
	icbi	0,	r4
	addi	r4,	r4,	0x20
	bdnz	InvalidateRange1

	li		r4,	0x100
	mtctr	r4
	lis		r4,	0x8000
	ori		r4,	r4,	0x1000

InvalidateRange2:
	dcbi	0,	r4
	icbi	0,	r4
	addi	r4,	r4,	0x20
	bdnz	InvalidateRange2

	lwz		r4,	EXTRA_LEN@l(r3)
	cmpwi	r4,	0
	beq		NoExtraRange
	mtctr	r4
	lwz		r4,	EXTRA_ADDR@l(r3)
InvalidateRange3:
	dcbi	0,	r4
	icbi	0,	r4
	addi	r4,	r4,	0x20
	bdnz	InvalidateRange3
NoExtraRange:

	lwz		r4,	ARENA_HI@l(r3)
	cmpwi	r4,	0
	beq		NoArenaHi
	lis		r5,	0x8000
	stw		r4,	0x34(r5)		#read by OSInit
NoArenaHi:

	sync
	isync

	blr
