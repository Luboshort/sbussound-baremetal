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
	m_nPodezrelych (0),
	// FIQ misto IRQ: zapis na kartu (log.txt) blokoval IRQ tak dlouho, ze
	// pretekl 16bajtovy FIFO UARTu a ztratil se ramec (v logu kroku 2:
	// 30 preteceni = 30 zapisu). FIQ ma prednost a zapis ho nezdrzi.
	m_Serial (pInterrupt, TRUE),
	m_nBuf (0),
	m_nCasRamce (0),
	m_bNekdyRamec (FALSE),
	m_bOtrava (FALSE),
	m_nOtravaOd (0),
	m_nOtravaDo (0)
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
			// Vadny bajt UART ulozil do bufferu a vyda ho v nekterem z dalsich cteni.
			m_bOtrava = TRUE;
			continue;		// chyba se ohlasi jednou, data cteme dal
		}
		if (n == 0)
		{
			m_bOtrava = FALSE;	// vse, co UART mel, je prectene
			break;
		}
		if (m_bOtrava)
		{
			if (m_nOtravaDo <= m_nOtravaOd) m_nOtravaOd = m_nBuf;	// jinak usek prodlouzit
			m_nOtravaDo = m_nBuf + n;	// plati, dokud se buffer UARTu nevyprazdni
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
			if (i < m_nOtravaDo && i + SBUS_DELKA > m_nOtravaOd)
			{
				m_nPodezrelych++;	// zatim jen pro log, ramec se pouzije
			}
			SBusKanaly (b, m_Kanal);
			m_nPriznaky = b[23];
			m_nKoncovy = b[24];
			m_nRamcu++;
			m_nCasRamce = CTimer::GetClockTicks ();
			m_bNekdyRamec = TRUE;
			m_Rozbor.Ramec (b, m_Kanal, m_nCasRamce);
			i += SBUS_DELKA;
		}
		memmove (m_Buf, m_Buf + i, m_nBuf - i);
		m_nBuf -= i;
		// usek s moznym vadnym bajtem se posune s bufferem
		m_nOtravaOd = m_nOtravaOd > i ? m_nOtravaOd - i : 0;
		m_nOtravaDo = m_nOtravaDo > i ? m_nOtravaDo - i : 0;
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
