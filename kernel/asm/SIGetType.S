#include <asm.h>

.set	SI_GC_CONTROLLER,		0x09000000
.set	SI_ERROR_NO_RESPONSE,	0x08
.set	PAD_USED_BASE,			0x93003020
.set	NIN_CFG_CONFIG,			0xD3004008	#ncfg->Config, uncached
.set	GBA_LINK_TYPE,			0xD30031C0	#kept up to date by the kernel, uncached

#
#
#	r3	channel

SIGetType:
	#GBA link port: report the real type the kernel last saw
	lis		r4,	NIN_CFG_CONFIG@ha
	lwz		r0,	NIN_CFG_CONFIG@l(r4)
	rlwinm.	r4,	r0,	0,	11,	11		#NIN_CFG_GBA_LINK (1<<20)
	beq		NoGBALink
	rlwinm	r4,	r0,	11,	30,	31		#NIN_CFG_GBA_PORT (bits 21-22)
	cmpw	r4,	r3
	bne		NoGBALink
	lis		r4,	GBA_LINK_TYPE@ha
	lwz		r3,	GBA_LINK_TYPE@l(r4)
	cmpwi	r3,	0
	bnelr
	li		r3,	SI_ERROR_NO_RESPONSE@l
	blr

NoGBALink:
	lis		r4,		PAD_USED_BASE@h
	ori		r4,	r4,	PAD_USED_BASE@l

DCInvalidateRange:
	dcbi	0,	r4
	sync

	lwz		r0,	4(r4)
	li		r4,	1
	slw		r4,	r4,	r3
	and		r4,	r0,	r4
	cmpwi	r4,	0
	beq		NoPad

	lis		r3,	SI_GC_CONTROLLER@h
	blr

NoPad:
	li		r3,	SI_ERROR_NO_RESPONSE@l
	blr
