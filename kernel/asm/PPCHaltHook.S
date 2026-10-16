#include <asm.h>

# Replaces the game's PPCHalt (where OSPanic and unhandled exceptions end
# up) when Log is on. Copied to MEM2 at 0x93154200, outside the memory it
# writes back and that the game can reach. Before halting, write the data
# cache back over all of MEM1, so the kernel's dump on power off reads
# current memory, and leave where it was called from for the kernel
# (PPCFlushReady in kernel/main.c). The kernel never writes this line.

.set	HALT_RECORD,	0xD31541A0	#uncached: +0 'HALT', +4 caller, +8 stack pointer

PPCHaltHook:
	lis		r4,	HALT_RECORD@h
	ori		r4,	r4,	HALT_RECORD@l
	mflr	r3
	stw		r3,	4(r4)
	stw		r1,	8(r4)
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
	stw		r3,	0(r4)
	sync
Halt:
	nop
	li		r3,	0
	nop
	b		Halt
