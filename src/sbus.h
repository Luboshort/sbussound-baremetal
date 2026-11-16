//
// sbus.h - prijem SBus na GPIO15 (RXD, pin 10): 100000 Bd, 8E2
//
#ifndef _sbus_h
#define _sbus_h

#include <circle/serial.h>
#include <circle/interrupt.h>
#include <circle/types.h>
#include "sbusramec.h"

#define SBUS_TICHO_US	500000		// po teto dobe bez ramce = bez signalu

class CSBus
{
public:
	CSBus (CInterruptSystem *pInterrupt);

	boolean Initialize (void);

	// Volat casto (kazdou ms). Vybere prijata data a rozebere ramce.
	void Poll (void);

	// Jsou cerstva data a vysilac neni ve failsafe?
	boolean MaSignal (void) const;
	// Chodi vubec ramce (i s priznakem failsafe)?
	boolean ChodiRamce (void) const;

public:
	// posledni platny ramec
	u16	 m_Kanal[SBUS_KANALU];
	u8	 m_nPriznaky;
	u8	 m_nKoncovy;
	// statistika
	unsigned m_nRamcu;
	unsigned m_nZahozeno;		// bajty mimo ramec (hledani zacatku)
	unsigned m_nChybParita;
	unsigned m_nChybRamec;		// framing error
	unsigned m_nChybPreteceni;
	unsigned m_nChybBreak;

private:
	CSerialDevice	m_Serial;
	u8		m_Buf[128];
	unsigned	m_nBuf;
	unsigned	m_nCasRamce;	// CTimer::GetClockTicks() posledniho ramce
	boolean		m_bNekdyRamec;
};

#endif
