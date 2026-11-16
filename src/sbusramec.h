//
// sbusramec.h - rozbor jednoho 25bajtoveho SBus ramce (bez zavislosti na HW)
//
#ifndef _sbusramec_h
#define _sbusramec_h

#include <circle/types.h>

#define SBUS_DELKA		25
#define SBUS_KANALU		16
#define SBUS_FLAG_LOST		0x04
#define SBUS_FLAG_FAILSAFE	0x08

// Hlavicka 0x0F, koncovy bajt 0x00 (SBUS) nebo 0x04/0x14/0x24/0x34 (SBUS2,
// Futaba) a v bajtu priznaku nepouzite horni 4 bity nulove.
// Stejna kontrola jako v linuxovem sound_engine2.py.
static inline boolean SBusRamecOK (const u8 *b)
{
	u8 k = b[24];
	return b[0] == 0x0F && (k == 0x00 || (k & 0x0F) == 0x04) && (b[23] & 0xF0) == 0;
}

// 16 kanalu po 11 bitech, little endian, bajty 1..22
static inline void SBusKanaly (const u8 *b, u16 *pKanal)
{
	unsigned nBit = 0;
	for (unsigned i = 0; i < SBUS_KANALU; i++)
	{
		unsigned v = 0;
		for (unsigned j = 0; j < 11; j++, nBit++)
		{
			if (b[1 + nBit / 8] & (1 << (nBit % 8)))
			{
				v |= 1 << j;
			}
		}
		pKanal[i] = (u16) v;
	}
}

#endif
