//
// wav.cpp - cteni WAV: PCM 8/16/24/32 bit, float 32 bit, WAVE_FORMAT_EXTENSIBLE,
// libovolny pocet kanalu (zprumeruje se do mona), libovolne vzorkovani
// (linearne prevzorkuje na 44100 Hz). Na Raspberry neni sox, proto si
// engine musi poradit s tim, co clovek na kartu nakopiruje.
//
#include "wav.h"
#include <fatfs/ff.h>
#include <circle/util.h>

#define CILOVY_RATE	44100

static unsigned Le16 (const u8 *p) { return p[0] | (p[1] << 8); }
static unsigned Le32 (const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned) p[3] << 24); }

const char *PopisChyby (TWavChyba Chyba)
{
	switch (Chyba)
	{
	case WavOK:		return "OK";
	case WavNelzeOtevrit:	return "soubor nejde otevrit";
	case WavFormat:		return "neznamy format WAV";
	case WavPamet:		return "malo pameti";
	case WavPrazdny:	return "WAV neobsahuje zadny zvuk";
	}
	return "?";
}

// jeden vzorek jednoho kanalu jako float -1..1
static float Vzorek (const u8 *p, unsigned nBity, boolean bFloat)
{
	if (bFloat)
	{
		u32 u = Le32 (p);
		float f;
		memcpy (&f, &u, 4);
		return f;
	}
	switch (nBity)
	{
	case 8:	 return ((int) p[0] - 128) / 128.0f;
	case 16: return (s16) Le16 (p) / 32768.0f;
	case 24: return ((s32) ((p[0] << 8) | (p[1] << 16) | ((u32) p[2] << 24)) >> 8) / 8388608.0f;
	case 32: return (s32) Le32 (p) / 2147483648.0f;
	}
	return 0.0f;
}

TWavChyba NactiWav (const char *pCesta, TZvuk *pZvuk)
{
	memset (pZvuk, 0, sizeof *pZvuk);

	FIL Soubor;
	if (f_open (&Soubor, pCesta, FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return WavNelzeOtevrit;
	}
	unsigned nVelikost = f_size (&Soubor);
	if (nVelikost < 44)
	{
		f_close (&Soubor);
		return WavFormat;
	}
	u8 *pSoubor = new u8[nVelikost];
	if (pSoubor == 0)
	{
		f_close (&Soubor);
		return WavPamet;
	}
	unsigned nPrecteno = 0;
	while (nPrecteno < nVelikost)
	{
		unsigned nKus = nVelikost - nPrecteno;
		if (nKus > 65536) nKus = 65536;
		unsigned n;
		if (f_read (&Soubor, pSoubor + nPrecteno, nKus, &n) != FR_OK || n == 0)
		{
			break;
		}
		nPrecteno += n;
	}
	f_close (&Soubor);
	if (nPrecteno != nVelikost)
	{
		delete [] pSoubor;
		return WavNelzeOtevrit;
	}

	TWavChyba Vysledek = WavFormat;
	if (memcmp (pSoubor, "RIFF", 4) == 0 && memcmp (pSoubor + 8, "WAVE", 4) == 0)
	{
		unsigned nFormat = 0, nKanaly = 0, nRate = 0, nBity = 0, nBlok = 0;
		const u8 *pData = 0;
		unsigned nDataBajtu = 0;

		// projdi chunky
		unsigned i = 12;
		while (i + 8 <= nVelikost)
		{
			unsigned nDelka = Le32 (pSoubor + i + 4);
			const u8 *p = pSoubor + i + 8;
			unsigned nZbyva = nVelikost - (i + 8);
			if (memcmp (pSoubor + i, "fmt ", 4) == 0 && nDelka >= 16 && nZbyva >= 16)
			{
				nFormat = Le16 (p);
				nKanaly = Le16 (p + 2);
				nRate	= Le32 (p + 4);
				nBlok	= Le16 (p + 12);
				nBity	= Le16 (p + 14);
				if (nFormat == 0xFFFE && nDelka >= 26 && nZbyva >= 26)
				{
					nFormat = Le16 (p + 24);	// prvni 2 bajty SubFormat GUID
				}
			}
			else if (memcmp (pSoubor + i, "data", 4) == 0)
			{
				pData = p;
				// delka muze byt nesmyslna (zaznam prerusen) - orizni na soubor
				nDataBajtu = nDelka <= nZbyva ? nDelka : nZbyva;
				break;
			}
			i += 8 + nDelka + (nDelka & 1);		// chunky jsou zarovnane na sudou delku
		}

		boolean bFloat = nFormat == 3;
		boolean bFormatOK =    (nFormat == 1 && (nBity == 8 || nBity == 16 || nBity == 24 || nBity == 32))
				    || (bFloat && nBity == 32);
		if (   pData != 0 && bFormatOK && nKanaly >= 1 && nKanaly <= 8
		    && nRate >= 4000 && nRate <= 192000 && nBlok == nKanaly * (nBity / 8))
		{
			unsigned nVstup = nDataBajtu / nBlok;
			if (nVstup == 0)
			{
				Vysledek = WavPrazdny;
			}
			else
			{
				unsigned nVystup = (unsigned) ((u64) nVstup * CILOVY_RATE / nRate);
				if (nVystup == 0) nVystup = 1;
				s16 *pVystup = new s16[nVystup];
				if (pVystup == 0)
				{
					Vysledek = WavPamet;
				}
				else
				{
					unsigned nBajtu = nBity / 8;
					// mono vzorek s indexem k
					auto Mono = [&] (unsigned k) -> float
					{
						const u8 *q = pData + k * nBlok;
						float s = 0.0f;
						for (unsigned c = 0; c < nKanaly; c++)
						{
							s += Vzorek (q + c * nBajtu, nBity, bFloat);
						}
						return s / nKanaly;
					};
					double fKrok = (double) nRate / CILOVY_RATE;
					for (unsigned k = 0; k < nVystup; k++)
					{
						float v;
						if (nRate == CILOVY_RATE)
						{
							v = Mono (k);
						}
						else
						{
							double fPos = k * fKrok;
							unsigned i0 = (unsigned) fPos;
							float fFrac = (float) (fPos - i0);
							unsigned i1 = i0 + 1 < nVstup ? i0 + 1 : i0;
							v = Mono (i0) * (1.0f - fFrac) + Mono (i1) * fFrac;
						}
						int n = (int) (v * 32767.0f);
						if (n > 32767) n = 32767;
						if (n < -32768) n = -32768;
						pVystup[k] = (s16) n;
					}
					pZvuk->pData = pVystup;
					pZvuk->nVzorku = nVystup;
					pZvuk->nPuvRate = nRate;
					pZvuk->nPuvKanaly = nKanaly;
					pZvuk->nPuvBity = nBity;
					Vysledek = WavOK;
				}
			}
		}
	}

	delete [] pSoubor;
	return Vysledek;
}
