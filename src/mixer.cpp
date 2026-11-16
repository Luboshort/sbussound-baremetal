//
// mixer.cpp
//
#include "mixer.h"
#include <circle/util.h>

#define RATE		44100

// Hlasitosti hlasu ve smesi - stejne jako MIX v sound_engine2.py.
#define MIX_MOTOR	0.85f
#define MIX_START	0.9f
#define MIX_STOP	0.9f
#define MIX_KULOMET	0.65f
#define MIX_VEZ		0.55f
#define MIX_POZADI	0.45f
#define MIX_DELO	0.9f

// Limiter: kdyz by smes po hlasitosti presahla PRAH, ztlumi se cela smes
// presne tolik, aby se vesla (okamzite), a pak se zesileni pomalu vraci
// (polovina za ~100 ms). Misto urizlych spicek, ktere praskaji, je slyset
// jen kratke ztlumeni.
#define LIMIT_PRAH	0.89f		// -1 dB pod maximem
#define LIMIT_NAVRAT	0.00016f	// podil zbytku k 1,0 za vzorek (~150 ms)

const char *NazevStavu (TStavMotoru S)
{
	switch (S)
	{
	case MotorVypnuty:	return "OFF";
	case MotorStartuje:	return "STARTING";
	case MotorBezi:		return "RUN";
	case MotorDobiha:	return "STOPPING";
	}
	return "?";
}

CMixer::CMixer (void)
:	m_Stav (MotorVypnuty),
	m_nRazeni (0),
	m_nVystrelu (0),
	m_fSpicka (0.0f),
	m_fMinZes (1.0f),
	m_nPretizeno (0),
	m_nOrezano (0),
	m_nMaxUs (0),
	m_nMaxRozestup (0),
	m_nPozde (0),
	m_nPodteceni (0),
	m_nBloku (0),
	m_fPlyn (0.0f),
	m_bMotor (FALSE),
	m_bVez (FALSE),
	m_bKulomet (FALSE),
	m_bDelo (FALSE),
	m_bPozadi (FALSE),
	m_bSignal (FALSE),
	m_fHlasitost (0.0f),
	m_bDeloNabito (TRUE),
	m_fHlasitostPred (0.0f),
	m_fLimiter (1.0f),
	m_nPosledniVolani (0),
	m_nCelkova (100),
	m_Strana (StranaOba),
	m_bPipani (FALSE),
	m_pPipani (0)
{
}

unsigned CMixer::Priprav (const TZvukyMixeru &Z, const TParPrevodovky &Prev)
{
	m_Box.Parametry (Prev);
	m_Start.Data (Z.pStart, Z.nStart);
	m_Stop.Data (Z.pStop, Z.nStop);
	m_Kulomet.Data (Z.pKulomet, Z.nKulomet);
	m_Vez.Data (Z.pVez, Z.nVez);
	m_Pozadi.Data (Z.pPozadi, Z.nPozadi);
	m_Delo.Data (Z.pDelo, Z.nDelo);
	return m_Motor.Data (Z.pBeh, Z.nBeh, Prev.fRateMax);
}

void CMixer::Vystup (unsigned nCelkova, TStrana Strana)
{
	m_nCelkova = nCelkova > 100 ? 100 : nCelkova;
	m_Strana = Strana;
}

void CMixer::Vstupy (const TVstupy &V)
{
	m_fPlyn = V.fPlyn;
	m_bMotor = V.bMotor;
	m_bVez = V.bVez;
	m_bKulomet = V.bKulomet;
	m_bDelo = V.bDelo;
	m_bPozadi = V.bPozadi;
	m_bSignal = V.bSignal;
	m_fHlasitost = V.fHlasitost;
}

void CMixer::Blok (float *pOut, unsigned n)
{
	double fDt = (double) n / RATE;
	boolean bMotor = m_bMotor;
	TStavMotoru Stav = m_Stav;

	// stavovy automat motoru (Engine.blok v sound_engine2.py)
	if (bMotor)
	{
		if (Stav == MotorVypnuty)
		{
			m_Start.Spust ();
			Stav = MotorStartuje;
		}
		else if (Stav == MotorStartuje && !m_Start.Bezi ())
		{
			m_Motor.Zap (TRUE);
			Stav = MotorBezi;
		}
		else if (Stav == MotorDobiha && !m_Stop.Bezi ())
		{
			m_Start.Spust ();
			Stav = MotorStartuje;
		}
	}
	else
	{
		if (Stav == MotorStartuje || Stav == MotorBezi)
		{
			m_Motor.Zap (FALSE);
			m_Start.Stop ();
			m_Stop.Spust ();
			Stav = MotorDobiha;
			m_Box.Nuluj ();
		}
		else if (Stav == MotorDobiha && !m_Stop.Bezi ())
		{
			Stav = MotorVypnuty;
		}
	}
	m_Stav = Stav;

	// prevodovka bezi jen v RUN, jinak plyn nema co ovlivnovat
	if (m_Box.Krok (Stav == MotorBezi ? m_fPlyn : 0.0, fDt))
	{
		m_nRazeni++;
	}

	m_Kulomet.Zap (m_bKulomet);
	m_Vez.Zap (m_bVez);
	m_Pozadi.Zap (m_bPozadi);

	// Delo: jedna rana na jedno prepnuti. Prepnuti behem dohravajici rany
	// se spotrebuje (nevystreli se pozde). Pri ztrate signalu rana hned
	// dozni - stejne jako umlcet() v sound_engine2.py.
	if (!m_bSignal)
	{
		m_Delo.Stop ();
	}
	if (m_bDelo)
	{
		if (m_bDeloNabito)
		{
			m_bDeloNabito = FALSE;
			if (!m_Delo.Bezi ())
			{
				m_Delo.Spust ();
				m_nVystrelu++;
			}
		}
	}
	else
	{
		m_bDeloNabito = TRUE;
	}

	for (unsigned i = 0; i < n; i++) pOut[i] = 0.0f;
	m_Motor.Blok (pOut, n, m_Box.Rate (), m_Box.m_fOtacky, MIX_MOTOR);
	m_Start.Blok (pOut, n, MIX_START);
	m_Stop.Blok (pOut, n, MIX_STOP);
	m_Kulomet.Blok (pOut, n, MIX_KULOMET);
	m_Vez.Blok (pOut, n, MIX_VEZ);
	m_Pozadi.Blok (pOut, n, MIX_POZADI);
	m_Delo.Blok (pOut, n, MIX_DELO);
}

void CMixer::Pipani (unsigned nPocet)
{
	const unsigned nPip = RATE * 15 / 100;		// 150 ms ton
	const unsigned nMezera = RATE * 15 / 100;	// 150 ms ticho
	const unsigned nPauza = RATE * 3 / 2;		// 1,5 s ticho na konci
	unsigned nDelka = nPocet * (nPip + nMezera) + nPauza;

	s16 *pNove = new s16[nDelka];
	if (pNove == 0)
	{
		return;
	}
	const unsigned nPerioda = RATE / 880;		// trojuhelnik 880 Hz
	unsigned k = 0;
	for (unsigned p = 0; p < nPocet; p++)
	{
		for (unsigned i = 0; i < nPip; i++)
		{
			unsigned f = i % nPerioda;
			int v = f < nPerioda / 2 ? (int) (f * 4 * 16000 / nPerioda) - 16000
						 : 48000 - (int) (f * 4 * 16000 / nPerioda);
			unsigned nOkraj = RATE / 200;
			unsigned nObalka = i < nOkraj ? i : (nPip - i < nOkraj ? nPip - i : nOkraj);
			pNove[k++] = (s16) (v * (int) nObalka / (int) nOkraj);
		}
		for (unsigned i = 0; i < nMezera; i++) pNove[k++] = 0;
	}
	for (unsigned i = 0; i < nPauza; i++) pNove[k++] = 0;

	m_pPipani = pNove;
	m_Pipak.Data (pNove, nDelka);
	m_Pipak.Zap (TRUE);
	m_bPipani = TRUE;
}

void CMixer::ZaznamenejVolani (unsigned nTedUs, unsigned nVzorku)
{
	unsigned nBlokUs = nVzorku * 1000000u / RATE;
	if (m_nBloku > 2)		// prvni dva bloky se plni hned po startu
	{
		unsigned d = nTedUs - m_nPosledniVolani;
		if (d > m_nMaxRozestup) m_nMaxRozestup = d;
		if (d > nBlokUs * 2) m_nPodteceni++;
		else if (d > nBlokUs * 3 / 2) m_nPozde++;
	}
	m_nPosledniVolani = nTedUs;
	m_nBloku++;
}

void CMixer::Vypln (u32 *pBuffer, unsigned nChunkSize, int nMin, int nMax, boolean bProhodit)
{
	unsigned n = nChunkSize / 2;
	if (n > MAX_BLOK) n = MAX_BLOK;

	float fHlasitost;
	if (m_bPipani)
	{
		for (unsigned i = 0; i < n; i++) m_Smes[i] = 0.0f;
		m_Pipak.Blok (m_Smes, n, 0.8f);
		fHlasitost = 1.0f;
		m_fHlasitostPred = 1.0f;
	}
	else
	{
		// prevodovka a smes po 512 vzorcich (11,6 ms) jako v sound_engine2.py,
		// i kdyz je blok vystupu delsi
		for (unsigned nOd = 0; nOd < n; nOd += 512)
		{
			Blok (m_Smes + nOd, n - nOd < 512 ? n - nOd : 512);
		}
		fHlasitost = m_fHlasitost * (m_nCelkova / 100.0f);
	}

	int nStred = (nMin + nMax) / 2;
	float fRozsah = (float) ((nMax - nMin) / 2);
	// hlasitost se meni plynule pres cely blok - bez schodu
	float fH = m_fHlasitostPred, fKrokH = (fHlasitost - m_fHlasitostPred) / n;
	TStrana Strana = m_Strana;

	float fSpicka = m_fSpicka, fMinZes = m_fMinZes, g = m_fLimiter;
	unsigned nPretizeno = 0, nOrezano = 0;

	for (unsigned i = 0; i < n; i++)
	{
		fH += fKrokH;
		float v = m_Smes[i] * fH;

		// limiter
		float a = v < 0 ? -v : v;
		if (a > fSpicka) fSpicka = a;
		if (a * g > LIMIT_PRAH)
		{
			g = LIMIT_PRAH / a;		// okamzite ztlumit prave tolik, kolik je treba
			nPretizeno++;
		}
		else
		{
			g += (1.0f - g) * LIMIT_NAVRAT;	// pomaly navrat
		}
		if (g < fMinZes) fMinZes = g;
		v *= g;

		if (v > 1.0f) { v = 1.0f; nOrezano++; }
		if (v < -1.0f) { v = -1.0f; nOrezano++; }
		int s = nStred + (int) (v * fRozsah);

		int L = Strana == StranaPravy ? nStred : s;
		int R = Strana == StranaLevy ? nStred : s;
		if (bProhodit) { int t = L; L = R; R = t; }
		pBuffer[2 * i] = (u32) L;
		pBuffer[2 * i + 1] = (u32) R;
	}
	m_fHlasitostPred = fHlasitost;
	m_fLimiter = g;
	m_fSpicka = fSpicka;
	m_fMinZes = fMinZes;
	m_nPretizeno += nPretizeno;
	m_nOrezano += nOrezano;
}
