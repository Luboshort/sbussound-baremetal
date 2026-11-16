//
// hlasy.cpp
//
#include "hlasy.h"
#include "matika.h"

#define RAMPA	(1.0f / 220.0f)		// zmena zesileni za vzorek: 0 -> 1 za 5 ms

static inline float Krok (float fZes, float fCil)
{
	if (fZes < fCil) { fZes += RAMPA; if (fZes > fCil) fZes = fCil; }
	else if (fZes > fCil) { fZes -= RAMPA; if (fZes < fCil) fZes = fCil; }
	return fZes;
}

// ---------------------------------------------------------------- smycka
CSmycka::CSmycka (void)
:	m_pData (0), m_nVzorku (0), m_nPozice (0), m_bZap (FALSE), m_fZes (0.0f)
{
}

void CSmycka::Data (const s16 *pData, unsigned nVzorku)
{
	m_pData = nVzorku ? pData : 0;
	m_nVzorku = nVzorku;
	m_nPozice = 0;
}

void CSmycka::Blok (float *pOut, unsigned n, float fMix)
{
	float fCil = m_bZap ? 1.0f : 0.0f;
	if (m_pData == 0 || (fCil == 0.0f && m_fZes == 0.0f))
	{
		return;
	}
	for (unsigned i = 0; i < n; i++)
	{
		m_fZes = Krok (m_fZes, fCil);
		pOut[i] += m_pData[m_nPozice] * (1.0f / 32768.0f) * m_fZes * fMix;
		if (++m_nPozice >= m_nVzorku) m_nPozice = 0;
		if (m_fZes == 0.0f) break;		// doznelo - smycka stoji
	}
}

// ---------------------------------------------------------------- jednoraz
CJednoraz::CJednoraz (void)
:	m_pData (0), m_nVzorku (0), m_nPozice (0), m_bBezi (FALSE), m_bKonci (FALSE), m_fZes (0.0f)
{
}

void CJednoraz::Data (const s16 *pData, unsigned nVzorku)
{
	m_pData = nVzorku ? pData : 0;
	m_nVzorku = nVzorku;
	m_bBezi = FALSE;
}

void CJednoraz::Spust (void)
{
	if (m_pData == 0)
	{
		return;
	}
	m_nPozice = 0;
	m_fZes = 1.0f;			// zacatek zvuku uz ma byt nahravka sama
	m_bKonci = FALSE;
	m_bBezi = TRUE;
}

void CJednoraz::Stop (void)
{
	if (m_bBezi) m_bKonci = TRUE;
}

void CJednoraz::Blok (float *pOut, unsigned n, float fMix)
{
	if (!m_bBezi)
	{
		return;
	}
	for (unsigned i = 0; i < n; i++)
	{
		if (m_bKonci)
		{
			m_fZes = Krok (m_fZes, 0.0f);
			if (m_fZes == 0.0f) { m_bBezi = FALSE; return; }
		}
		pOut[i] += m_pData[m_nPozice] * (1.0f / 32768.0f) * m_fZes * fMix;
		if (++m_nPozice >= m_nVzorku) { m_bBezi = FALSE; return; }
	}
}

// ---------------------------------------------------------------- motor
// Kruhovy FIR filtr typu dolni propust (okno Blackman, 127 koeficientu).
// fMez = mez propustneho pasma jako podil vzorkovaci frekvence (0..0,5).
static void DolniPropust (const float *pVstup, float *pVystup, unsigned L, double fMez)
{
	enum { N = 127, M = N / 2 };
	float h[N];
	double fSoucet = 0.0;
	for (int k = 0; k < N; k++)
	{
		int j = k - M;
		double fSinc = j == 0 ? 2.0 * fMez : Sin (2.0 * PI * fMez * j) / (PI * j);
		double fOkno = 0.42 - 0.5 * Cos (2.0 * PI * k / (N - 1)) + 0.08 * Cos (4.0 * PI * k / (N - 1));
		h[k] = (float) (fSinc * fOkno);
		fSoucet += h[k];
	}
	for (int k = 0; k < N; k++) h[k] = (float) (h[k] / fSoucet);	// zesileni 1 pro DC

	for (unsigned i = 0; i < L; i++)
	{
		float y = 0.0f;
		// index vstupu i - j pro j = -M..M, kruhove
		unsigned nZac = (i + L - M % L) % L;
		unsigned p = nZac;
		for (int k = N - 1; k >= 0; k--)
		{
			y += h[k] * pVstup[p];
			if (++p >= L) p = 0;
		}
		pVystup[i] = y;
	}
}

CMotorHlas::CMotorHlas (void)
:	m_nUrovni (0), m_nVzorku (0), m_fPozice (0.0), m_bZap (FALSE), m_fZes (0.0f)
{
	for (unsigned i = 0; i < MAX_UROVNI; i++) m_pUroven[i] = 0;
}

unsigned CMotorHlas::Data (const s16 *pData, unsigned nVzorku, double fRateMax)
{
	m_nUrovni = 0;
	if (pData == 0 || nVzorku == 0)
	{
		return 0;
	}
	m_nVzorku = nVzorku;
	m_fPozice = 0.0;

	// pocet kopii: rychlost mezi sousednimi nejvys 1,25x
	unsigned K = 0;
	for (double r = 1.0; r < fRateMax - 1e-9 && K < MAX_UROVNI - 1; r *= 1.25) K++;
	if (nVzorku < 128) K = 0;		// prilis kratka smycka - bez filtru

	float *pPuvodni = new float[nVzorku];
	if (pPuvodni == 0) return 0;
	for (unsigned i = 0; i < nVzorku; i++) pPuvodni[i] = pData[i] * (1.0f / 32768.0f);
	m_pUroven[0] = pPuvodni;
	m_nUrovni = 1;

	for (unsigned k = 1; k <= K; k++)
	{
		float *p = new float[nVzorku];
		if (p == 0) break;
		// kopie pro rychlost r = rateMax^(k/K): po zrychleni nesmi nic prekrocit
		// 0,92 Nyquista. Blackman 127 ma prechod ~0,043 fs, proto mez o kus niz.
		double r = Pow (fRateMax, (double) k / K);
		double fMez = 0.5 * 0.92 / r - 0.0215;
		DolniPropust (pPuvodni, p, nVzorku, fMez);
		m_pUroven[m_nUrovni++] = p;
	}
	return m_nUrovni;
}

void CMotorHlas::Blok (float *pOut, unsigned n, double fRate, double fOtacky, float fMix)
{
	float fCil = m_bZap ? 1.0f : 0.0f;
	if (m_nUrovni == 0 || (fCil == 0.0f && m_fZes == 0.0f))
	{
		return;
	}
	// dve sousedni kopie a vaha mezi nimi
	unsigned K = m_nUrovni - 1;
	const float *a = m_pUroven[0], *b = m_pUroven[0];
	float w = 0.0f;
	if (K > 0)
	{
		double x = fOtacky * K;
		if (x < 0) x = 0;
		if (x > K) x = K;
		unsigned k = (unsigned) x;
		if (k > K - 1) k = K - 1;
		w = (float) (x - k);
		a = m_pUroven[k];
		b = m_pUroven[k + 1];
	}
	double L = m_nVzorku;
	for (unsigned i = 0; i < n; i++)
	{
		m_fZes = Krok (m_fZes, fCil);
		unsigned i0 = (unsigned) m_fPozice;
		if (i0 >= m_nVzorku) i0 = 0;
		float fFrac = (float) (m_fPozice - i0);
		unsigned i1 = i0 + 1 < m_nVzorku ? i0 + 1 : 0;
		float sa = a[i0] + (a[i1] - a[i0]) * fFrac;
		float v = sa;
		if (w > 1e-3f)
		{
			float sb = b[i0] + (b[i1] - b[i0]) * fFrac;
			v = sa + (sb - sa) * w;
		}
		pOut[i] += v * m_fZes * fMix;
		m_fPozice += fRate;
		while (m_fPozice >= L) m_fPozice -= L;
	}
}
