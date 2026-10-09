/*
SI.c for Nintendont (Kernel)

Copyright (C) 2014 FIX94

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include "SI.h"
#include "Config.h"
#include "debug.h"
#include "string.h"

#define SI_GC_CONTROLLER 0x09000000
#define SI_ERROR_NO_RESPONSE 0x08

/* Real SI registers, as seen from the Starlet */
#define REAL_SI_BASE	0x0D806400
#define REAL_SI_POLL	(REAL_SI_BASE+0x30)
#define REAL_SI_CONTROL	(REAL_SI_BASE+0x34)
#define REAL_SI_STATUS	(REAL_SI_BASE+0x38)
#define REAL_SI_IO_BUF	(REAL_SI_BASE+0x80)

/* ~5ms, a GBA transfer takes well under 1ms */
#define GBA_LINK_TIMEOUT	9500

extern vu32 TRIGame;

u32 SI_IRQ = 0;
static bool complete = true;
static u32 cur_control = 0;
/* GBA link passthrough: game port and real SI channel, GBALinkPort -1 = off */
static s32 GBALinkPort = -1;
static u32 GBALinkPhys = 0;
void SIInit()
{
	memset((void*)SI_BASE, 0, 0x120);
	sync_after_write((void*)SI_BASE, 0x120);

	sync_before_read((void*)PAD_BUFF, 0x40);
	memset((void*)PAD_BUFF, 0, 0x30); //For Triforce to not instantly reset
	sync_after_write((void*)PAD_BUFF, 0x40);

	SI_IRQ = 0;
	complete = true;
	cur_control = 0;

	GBALinkPort = ConfigGetGBALinkPort();
	GBALinkPhys = ConfigGetGBALinkPhys();
	if (GBALinkPort >= 0)
		dbgprintf("SI:GBA link on port %d, real channel %d\r\n", GBALinkPort + 1, GBALinkPhys);
}

static void SIWaitRealTransfer(void)
{
	u32 start = read32(HW_TIMER);
	while ((read32(REAL_SI_CONTROL) & 1) && TimerDiffTicks(start) < GBA_LINK_TIMEOUT)
		;
}

/*
 * Run the game's pending transfer on the real SI channel the GBA is
 * wired to, then report the result back through the emulated registers
 * as if it happened on the game's port.
 */
static void SIGBALinkTransfer(u32 *control, u32 *status)
{
	u32 outlen = (*control >> 16) & 0x7F;
	u32 inlen = (*control >> 8) & 0x7F;
	u32 i;

	/* A GBA only answers direct transfers, never poll it */
	u32 poll = read32(REAL_SI_POLL);
	if (poll & (0x80 >> GBALinkPhys))
		write32(REAL_SI_POLL, poll & ~(0x80 >> GBALinkPhys));

	SIWaitRealTransfer();

	/* Output buffer, 0 length means 128 bytes */
	for (i = 0; i < (outlen ? outlen : 0x80); i += 4)
		write32(REAL_SI_IO_BUF + i, read32(SI_IO_BUF + i));

	/* Clear the channel's error bits, then start the transfer.
	   Keep RDSTINTMSK as-is and leave TCINTMSK off so the PPC never
	   sees an interrupt from the real hardware. */
	write32(REAL_SI_STATUS, 0x0F000000 >> (8 * GBALinkPhys));
	u32 real = read32(REAL_SI_CONTROL) & (1 << 27);
	write32(REAL_SI_CONTROL, real | (1 << 31) | (outlen << 16) | (inlen << 8) | (GBALinkPhys << 1) | 1);

	SIWaitRealTransfer();

	u32 result = read32(REAL_SI_CONTROL);
	u32 err;
	bool comerr;
	if (result & 1)
	{
		dbgprintf("SI:GBA link transfer timed out\r\n");
		err = SI_ERROR_NO_RESPONSE;
		comerr = true;
	}
	else
	{
		err = (read32(REAL_SI_STATUS) >> (8 * (3 - GBALinkPhys))) & 0xF;
		comerr = !!(result & (1 << 29));
	}
	/* Acknowledge the real transfer complete interrupt */
	write32(REAL_SI_CONTROL, (result & (1 << 27)) | (1 << 31));

	/* Input buffer */
	for (i = 0; i < (inlen ? inlen : 0x80); i += 4)
		write32(SI_IO_BUF + i, read32(REAL_SI_IO_BUF + i));
	sync_after_write((void*)SI_IO_BUF, 0x80);

	/* Move the error bits over to the game's port */
	u32 shift = 8 * (3 - GBALinkPort);
	*status = (*status & ~(0x0F << shift)) | (err << shift);
	write32(SI_STATUS, *status);
	sync_after_write((void*)SI_STATUS, 4);

	if (comerr)
		*control |= (1 << 29);
	else
		*control &= ~(1 << 29);
}

void SIInterrupt()
{
	//sync_before_read((void*)0x14, 0x4);
	//if (read32(0x14) != 0)
	//	return;
	if (SI_IRQ & 0x1)
	{
		if (complete)
		{
			cur_control &= ~(1<<28); //no read interrupt requested anymore, completed transfer
		}
		else
		{
			cur_control |= (1<<28); //read status interupt request, might need more if we wanna use that
		}
		//dbgprintf("Read SI Control: 0x%08X 0x%08X\r\n", cur_control);
		write32(SI_CONTROL, cur_control);
		sync_after_write((void*)SI_CONTROL, 4);
	}
	if (SI_IRQ & 0x2)
	{
		sync_before_read((void*)SI_CONTROL, 0x4);
		cur_control = read32(SI_CONTROL);
		if ((cur_control & (1 << 31)) == 0)
			SI_IRQ &= ~0x2; //one-shot interrupt skipped
		if ((cur_control & (1 << 30)) == 0)
			return;  // interrupts not enabled
		SI_IRQ &= ~0x2; //one-shot interrupt complete
		//dbgprintf("SI Done Transfer %d, 0x%08X\r\n", SI_IRQ, cur_control);
	}

	write32( SI_INT, 0x8 );		// SI IRQ
	sync_after_write( (void*)SI_INT, 0x20 );
	write32( HW_IPC_ARMCTRL, 8 ); //throw irq

	complete ^= 1;
}

void SIUpdateRegisters()
{
	sync_before_read((void*)SI_BASE, 0x100);
	cur_control = read32(SI_CONTROL);
	u32 cur_status = read32(SI_STATUS);
	//u32 cur_poll = read32(SI_POLL);
	//dbgprintf("Read SI Control: 0x%08X\r\n", cur_control);
	if (cur_control & (1 << 0)) // transfer
	{
		//dbgprintf("SI Buffer Cmd: %08x\r\n", read32(SI_IO_BUF));
		u32 chan = (cur_control >> 1) & 0x3;
		u32 ChanBuff = PAD_BUFF + (chan * 0xC);
		bool PadGood = !!(read32(0x13003024) & (1<<chan));
		/* With GBA link, MaxPads limits real ports only; emulated pads still
		   need to answer Get Type so the game can tell them from the GBA. */
		if (chan >= ConfigGetMaxPads() && GBALinkPort < 0)
			PadGood = false;
		if ((s32)chan == GBALinkPort && TRIGame == TRI_NONE)
		{
			SIGBALinkTransfer(&cur_control, &cur_status);
		}
		else
		{
			cur_control |= (1<<29); //we normally always have some communication error?
			//cur_control &= ~(1 << 29); //we normally always have some communication error?
			switch ((read32(SI_IO_BUF) >> 24) & 0xFF)
			{
				case 0x00: // Get Type
					{
						//dbgprintf("SI GetType\r\n");
						write32(SI_IO_BUF, PadGood ? SI_GC_CONTROLLER : SI_ERROR_NO_RESPONSE);
						sync_after_write((void*)SI_IO_BUF, 4);
					} break;
				case 0x40: // Direct Cmd
					{
						//dbgprintf("SI Direct\r\n");
						//Might need to fix this (multiple modes)
						write32(SI_IO_BUF + 0, read32(ChanBuff + 0));
						write32(SI_IO_BUF + 4, read32(ChanBuff + 4));
						sync_after_write((void*)SI_IO_BUF, 8);
					} break;
				case 0x41: // Origin
				case 0x42: // Calibrate
					{
						//dbgprintf("SI Origin/Cal\r\n");
						//Might need to fix this (multiple modes)
						write32(SI_IO_BUF + 0, 0x41008080);
						write32(SI_IO_BUF + 4, 0x80801F1F);
						write32(SI_IO_BUF + 8, 0x00000000);
						sync_after_write((void*)SI_IO_BUF, 12);
					} break;
			}
		}
		if (!PadGood)
		{
			//cur_control |= (1 << 29); //we normally always have some communication error?
			//cur_status |= (0x08000000 >> (0x8 * chan));
			//write32(SI_STATUS, cur_status);
			//sync_after_write((void*)SI_STATUS, 4);
		}
		cur_control &= ~1;
		//cur_control |= (1 << 28); //read status interupt request, might need more if we wanna use that
		cur_control |= (1 << 31); //tc status interupt request, might need more if we wanna use that
		/* set controller responses, not needed with patched PADRead
		if((cur_control & 7) == 3) //chan 1
			cur_status |= (1<<19);
		else if((cur_control & 7) == 5) //chan 2
			cur_status |= (1<<11);
		else if((cur_control & 7) == 7) //chan 3
			cur_status |= (1<<3);
		//dbgprintf("New SI Control: %08x\r\n", cur_control);
		write32(SI_STATUS, cur_status);
		sync_after_write((void*)SI_STATUS, 4);*/
		write32(SI_CONTROL, cur_control);
		sync_after_write((void*)SI_CONTROL, 4);
		SI_IRQ |= 0x2; //we will give the game one interrupt
		//dbgprintf("Read SI mod Control: 0x%08X\r\n", cur_control);
		complete = false;
		//if((cur_control & 0x7F00) != 0)
		//	complete = false; //requested some bytes
		//u8 cur_chan = 0, prev_chan = 0;
		//cur_chan = (cur_control & 6) >> 1;
		//if(cur_chan != prev_chan) //force repeat
		//{
		//	complete = false;
		//	prev_chan = cur_chan;
		//}
	}
	if (cur_status & 0x80000000)
	{
		//dbgprintf("Read SI Status: %08x\r\n", cur_status);
		u32 ChanIdx;
		for (ChanIdx = 0; ChanIdx < 4; ChanIdx++)
		{
			bool PadGood = true;
			//bool PadGood = read32(PAD_BUFF + 8 + (ChanIdx * 0xC)) == 0;
			//if (ChanIdx >= ConfigGetMaxPads())
			//	PadGood = false;
			//cur_status = (cur_status << 0x8) | (PadGood ? 0x0 : 0x20);
			u32 ChanAdd = SI_CHAN_0 + (0xC * ChanIdx);
			write32(ChanAdd + 4, PadGood ? 0x00808080 : 0);//read32(0x1300280C+0));
			write32(ChanAdd + 8, PadGood ? 0x80800000 : 0);//read32(0x1300280C+4));
		}
		if (GBALinkPort >= 0) // keep the GBA port's real error bits
		{
			u32 mask = 0xFF << (8 * (3 - GBALinkPort));
			cur_status = (0x20202020 & ~mask) | (cur_status & mask & 0x0F0F0F0F);
		}
		else
			cur_status = 0x20202020;
		//cur_status = 0x0;
		sync_after_write((void*)SI_CHAN_0, 0x30);
		//dbgprintf("Read SI Cmd 0-3: 0x%08X, 0x%08X, 0x%08X, 0x%08X,\r\n", read32(SI_CHAN_0), read32(SI_CHAN_1), read32(SI_CHAN_2), read32(SI_CHAN_3));
		write32(SI_STATUS, cur_status);
		sync_after_write((void*)SI_STATUS, 4);
		//dbgprintf("Wrote SI Status: %08x\r\n", cur_status);
	}

	// This should use cur_poll.  Something's still not right.
	//if (((cur_poll & 0xF0) != 0) && (cur_control & (1 << 27)) != 0)
	if ((cur_control & (1 << 27)) != 0)
	{
		cur_control |= (1<<29); //we normally always have some communication error?
		//if ((SI_IRQ & 0x1) == 0x0)
		//	dbgprintf("Enabled polling SI IRQ %d\r\n", SI_IRQ);
		SI_IRQ |= 0x1; //give the game regular updates
	}
	else
	{
		//if ((SI_IRQ & 0x1) == 0x1)
		//	dbgprintf("Disabled polling SI IRQ %d\r\n", SI_IRQ);
		SI_IRQ &= ~(0x1); //give the game regular updates
	}
}
