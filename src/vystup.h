//
// vystup.h - zvukovy vystup: I2S (GY-PCM5102) nebo 3,5mm jack (PWM).
// Oba jen predavaji vzorky z mixeru; vybira se v nastaveni.txt.
//
#ifndef _vystup_h
#define _vystup_h

#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/sound/i2ssoundbasedevice.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include "mixer.h"

#define VYSTUP_RATE	44100
// 2048 slov = 1024 stereo vzorku = 23,2 ms na blok. Vetsi blok zvuk ochrani,
// kdyz se preruseni na chvili zdrzi. Odezva na ovladace se tim prodlouzi jen
// o ~12 ms (linuxova verze mela ~100 ms).
#define VYSTUP_CHUNK	2048

class CVystupPWM : public CPWMSoundBaseDevice
{
public:
	CVystupPWM (CInterruptSystem *pInterrupt, CMixer *pMixer)
	:	CPWMSoundBaseDevice (pInterrupt, VYSTUP_RATE, VYSTUP_CHUNK),
		m_pMixer (pMixer) {}
private:
	unsigned GetChunk (u32 *pBuffer, unsigned nChunkSize) override
	{
		unsigned t0 = CTimer::GetClockTicks ();
		m_pMixer->ZaznamenejVolani (t0, nChunkSize / 2);
		m_pMixer->Vypln (pBuffer, nChunkSize, GetRangeMin (), GetRangeMax (), AreChannelsSwapped ());
		m_pMixer->ZaznamenejCas (CTimer::GetClockTicks () - t0);
		return nChunkSize;
	}
	CMixer *m_pMixer;
};

// PCM5102A nepotrebuje I2C, staci hodiny a data:
//   GPIO18 = BCK, GPIO19 = LCK, GPIO21 = DIN (stejne jako pod Linuxem)
class CVystupI2S : public CI2SSoundBaseDevice
{
public:
	CVystupI2S (CInterruptSystem *pInterrupt, CMixer *pMixer)
	:	CI2SSoundBaseDevice (pInterrupt, VYSTUP_RATE, VYSTUP_CHUNK),
		m_pMixer (pMixer) {}
private:
	unsigned GetChunk (u32 *pBuffer, unsigned nChunkSize) override
	{
		unsigned t0 = CTimer::GetClockTicks ();
		m_pMixer->ZaznamenejVolani (t0, nChunkSize / 2);
		m_pMixer->Vypln (pBuffer, nChunkSize, GetRangeMin (), GetRangeMax (), AreChannelsSwapped ());
		m_pMixer->ZaznamenejCas (CTimer::GetClockTicks () - t0);
		return nChunkSize;
	}
	CMixer *m_pMixer;
};

#endif
