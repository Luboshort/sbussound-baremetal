//
// sbus.cpp
//
#include "sbus.h"
#include <circle/timer.h>
#include <circle/util.h>

CSBus::CSBus (CInterruptSystem *pInterrupt)
:	m_nPriznaky (0),
	m_nKoncovy (0),
	m_nRamcu (0),
	m_nZahozeno (0),
	m_nChybParita (0),
	m_nChybRamec (0),
	m_nChybPreteceni (0),
	m_nChybBreak (0),
	// FIQ misto IRQ: zapis na kartu (log.txt) blokoval IRQ tak dlouho, ze
	// pretekl 16bajtovy FIFO UARTu a ztratil se ramec (v logu kroku 2:
	// 30 preteceni = 30 zapisu). FIQ ma prednost a zapis ho nezdrzi.
	m_Serial (pInterrupt, TRUE),
	m_nBuf (0),
	m_nCasRamce (0),
	m_bNekdyRamec (FALSE)
{
	memset (m_Kanal, 0, sizeof m_Kanal);
}

boolean CSBus::Initialize (void)
{
	// SBus je 100000 Bd, 8 datovych bitu, suda parita, 2 stop bity.
	// Signal na GPIO15 uz musi byt neinvertovany - stejne jako u linuxove verze.
	return m_Serial.Initialize (100000, 8, 2, CSerialDevice::ParityEven);
}

void CSBus::Poll (void)
{
	while (1)
	{
		int n = m_Serial.Read (m_Buf + m_nBuf, sizeof m_Buf - m_nBuf);
		if (n < 0)
		{
			switch (-n)
			{
			case SERIAL_ERROR_PARITY:	m_nChybParita++;	break;
			case SERIAL_ERROR_FRAMING:	m_nChybRamec++;		break;
			case SERIAL_ERROR_OVERRUN:	m_nChybPreteceni++;	break;
			case SERIAL_ERROR_BREAK:	m_nChybBreak++;		break;
			}
			continue;		// chyba se ohlasi jednou, data cteme dal
		}
		if (n == 0)
		{
			break;
		}
		m_nBuf += n;

		unsigned i = 0;
		while (m_nBuf - i >= SBUS_DELKA)
		{
			const u8 *b = m_Buf + i;
			if (!SBusRamecOK (b))
			{
				i++;
				m_nZahozeno++;
				continue;
			}
			SBusKanaly (b, m_Kanal);
			m_nPriznaky = b[23];
			m_nKoncovy = b[24];
			m_nRamcu++;
			m_nCasRamce = CTimer::GetClockTicks ();
			m_bNekdyRamec = TRUE;
			i += SBUS_DELKA;
		}
		memmove (m_Buf, m_Buf + i, m_nBuf - i);
		m_nBuf -= i;
	}
}

boolean CSBus::ChodiRamce (void) const
{
	return m_bNekdyRamec && CTimer::GetClockTicks () - m_nCasRamce < SBUS_TICHO_US;
}

boolean CSBus::MaSignal (void) const
{
	return ChodiRamce () && !(m_nPriznaky & SBUS_FLAG_FAILSAFE);
}
