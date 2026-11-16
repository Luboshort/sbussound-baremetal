//
// ini.cpp
//
#include "ini.h"
#include <fatfs/ff.h>
#include <circle/util.h>
#include <circle/string.h>

CIni::CIni (void)
:	m_nPolozek (0),
	m_nChyb (0)
{
	m_Chyby[0] = '\0';
}

boolean CIni::Nacti (const char *pCesta)
{
	m_nPolozek = 0;
	m_nChyb = 0;
	m_Chyby[0] = '\0';
	FIL Soubor;
	if (f_open (&Soubor, pCesta, FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return FALSE;
	}
	unsigned nVelikost = f_size (&Soubor);
	if (nVelikost > 16384)
	{
		f_close (&Soubor);
		Chyba (0, "soubor je prilis velky");
		return FALSE;
	}
	char *pText = new char[nVelikost + 1];
	unsigned nPrecteno = 0;
	FRESULT Vysledek = f_read (&Soubor, pText, nVelikost, &nPrecteno);
	f_close (&Soubor);
	if (Vysledek != FR_OK || nPrecteno != nVelikost)
	{
		delete [] pText;
		Chyba (0, "soubor nejde precist");
		return FALSE;
	}
	Zpracuj (pText, nVelikost);
	delete [] pText;
	return TRUE;
}

static char Male (char c)
{
	return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

static boolean Mezera (char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

void CIni::Zpracuj (const char *pText, unsigned nDelka)
{
	m_nPolozek = 0;
	m_nChyb = 0;
	m_Chyby[0] = '\0';
	const char *p = pText, *pKonec = pText + nDelka;
	if (nDelka >= 3 && (u8) p[0] == 0xEF && (u8) p[1] == 0xBB && (u8) p[2] == 0xBF)
	{
		p += 3;				// UTF-8 BOM
	}
	char Sekce[MAX_KLIC] = "";
	unsigned nRadek = 0;

	while (p < pKonec)
	{
		// jeden radek bez komentare a bez mezer na krajich
		nRadek++;
		const char *pZac = p;
		while (p < pKonec && *p != '\n') p++;
		const char *pKon = p;
		if (p < pKonec) p++;		// '\n'
		for (const char *q = pZac; q < pKon; q++)
		{
			if (*q == '#' || *q == ';') { pKon = q; break; }
		}
		while (pZac < pKon && Mezera (*pZac)) pZac++;
		while (pKon > pZac && Mezera (pKon[-1])) pKon--;
		if (pZac == pKon)
		{
			continue;
		}

		if (*pZac == '[')
		{
			if (pKon[-1] != ']' || pKon - pZac < 3 || (unsigned) (pKon - pZac - 2) >= sizeof Sekce)
			{
				Chyba (nRadek, "spatna hlavicka sekce");
				continue;
			}
			unsigned n = 0;
			for (const char *q = pZac + 1; q < pKon - 1; q++)
			{
				if (!Mezera (*q)) Sekce[n++] = Male (*q);
			}
			Sekce[n] = '\0';
			continue;
		}

		const char *pRovna = pZac;
		while (pRovna < pKon && *pRovna != '=') pRovna++;
		if (pRovna == pKon)
		{
			Chyba (nRadek, "chybi =");
			continue;
		}
		const char *pK1 = pRovna;
		while (pK1 > pZac && Mezera (pK1[-1])) pK1--;
		const char *pH0 = pRovna + 1;
		while (pH0 < pKon && Mezera (*pH0)) pH0++;
		if (pK1 == pZac || pH0 == pKon)
		{
			Chyba (nRadek, pK1 == pZac ? "chybi nazev pred =" : "chybi hodnota za =");
			continue;
		}
		unsigned nSekce = strlen (Sekce);
		if (   nSekce + 1 + (pK1 - pZac) >= MAX_KLIC
		    || (unsigned) (pKon - pH0) >= MAX_HODNOTA)
		{
			Chyba (nRadek, "prilis dlouhy nazev nebo hodnota");
			continue;
		}
		if (m_nPolozek >= MAX_POLOZEK)
		{
			Chyba (nRadek, "prilis mnoho polozek");
			break;
		}
		TPolozka &P = m_Polozky[m_nPolozek];
		unsigned n = 0;
		for (unsigned i = 0; i < nSekce; i++) P.Klic[n++] = Sekce[i];
		if (nSekce) P.Klic[n++] = '.';
		for (const char *q = pZac; q < pK1; q++) P.Klic[n++] = Male (*q);
		P.Klic[n] = '\0';
		n = 0;
		for (const char *q = pH0; q < pKon; q++) P.Hodnota[n++] = Male (*q);
		P.Hodnota[n] = '\0';
		P.nRadek = nRadek;

		// stejny klic podruhe: plati posledni (a upozornime)
		boolean bNahrazeno = FALSE;
		for (unsigned i = 0; i < m_nPolozek; i++)
		{
			if (strcmp (m_Polozky[i].Klic, P.Klic) == 0)
			{
				CString s; s.Format ("%s uz je na radku %u, plati tento", P.Klic, m_Polozky[i].nRadek);
				Chyba (nRadek, s);
				m_Polozky[i] = P;
				bNahrazeno = TRUE;
				break;
			}
		}
		if (!bNahrazeno)
		{
			m_nPolozek++;
		}
	}
}

const char *CIni::Hodnota (const char *pKlic) const
{
	for (unsigned i = 0; i < m_nPolozek; i++)
	{
		if (strcmp (m_Polozky[i].Klic, pKlic) == 0)
		{
			return m_Polozky[i].Hodnota;
		}
	}
	return 0;
}

void CIni::Chyba (unsigned nRadek, const char *pText)
{
	m_nChyb++;
	CString s;
	if (nRadek) s.Format ("radek %u: %s", nRadek, pText);
	else s = pText;
	unsigned n = strlen (m_Chyby);
	if (n >= 3 && strcmp (m_Chyby + n - 3, "...") == 0) return;	// uz je plno
	unsigned nOddel = n > 0 ? 2 : 0;
	if (n + nOddel + s.GetLength () < sizeof m_Chyby - 4)
	{
		if (nOddel) strcat (m_Chyby, "; ");
		strcat (m_Chyby, s);
	}
	else
	{
		strcat (m_Chyby, " ...");
	}
}
