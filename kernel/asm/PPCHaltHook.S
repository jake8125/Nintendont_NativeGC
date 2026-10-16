#include <asm.h>

# Replaces the game's PPCHalt (where OSPanic and unhandled exceptions end
# up) when Log is on. Before halting, write the data cache back over all of
# MEM1, so the kernel's dump on power off reads current memory, and leave
# where it was called from for the kernel (PPCFlushReady in kernel/main.c).

.set	FLUSH_REQUEST,	0xD3154180	#uncached: +4 'HALT', +8 caller, +C stack pointer

PPCHaltHook:
	lis		r4,	FLUSH_REQUEST@h
	ori		r4,	r4,	FLUSH_REQUEST@l
	mflr	r3
	stw		r3,	8(r4)
	stw		r1,	0xC(r4)
	lis		r5,	0x8000
	lis		r6,	0x000C			#0x1800000 / 32 lines
	mtctr	r6
FlushLine:
	dcbst	0,	r5
	addi	r5,	r5,	32
	bdnz	FlushLine
	sync
	lis		r3,	0x4841			#'HALT'
	ori		r3,	r3,	0x4C54
	stw		r3,	4(r4)
	sync
Halt:
	nop
	li		r3,	0
	nop
	b		Halt
