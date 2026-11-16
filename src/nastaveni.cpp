//
// nastaveni.cpp
//
#include "nastaveni.h"
#include <circle/util.h>
#include <circle/string.h>

const char *const NazevFunkce[FUNKCI] =
	{"vez", "motor", "plyn", "kulomet", "delo", "pozadi", "hlasitost"};

static const unsigned KanalVychozi[FUNKCI] = {11, 9, 10, 7, 5, 8, 6};

const char *NazevZarizeni (TZarizeni Z)	{ return Z == ZarizeniJack ? "jack" : "i2s"; }
const char *NazevStrany (TStrana S)	{ return S == StranaLevy ? "levy" : S == StranaPravy ? "pravy" : "oba"; }

static void Pripis (char *pZprava, unsigned nZprava, const char *pText)
{
	if (pZprava == 0 || nZprava < 8) return;
	unsigned n = strlen (pZprava);
	if (n >= 3 && strcmp (pZprava + n - 3, "...") == 0) return;	// uz je plno
	unsigned nOddel = n > 0 ? 2 : 0;
	if (n + nOddel + strlen (pText) < nZprava - 4)
	{
		if (nOddel) strcat (pZprava, "; ");
		strcat (pZprava, pText);
	}
	else
	{
		strcat (pZprava, n + 4 < nZprava ? " ..." : "");	// dalsi se nevejdou
	}
}

// cele cislo v rozsahu, jinak FALSE
static boolean Cele (const char *p, int nMin, int nMax, int *pVysledek)
{
	if (p == 0 || *p == '\0') return FALSE;
	boolean bMinus = *p == '-';
	if (bMinus) p++;
	if (*p == '\0') return FALSE;
	long n = 0;
	for (; *p; p++)
	{
		if (*p < '0' || *p > '9' || n > 100000) return FALSE;
		n = n * 10 + (*p - '0');
	}
	if (bMinus) n = -n;
	if (n < nMin || n > nMax) return FALSE;
	*pVysledek = (int) n;
	return TRUE;
}

// desetinne cislo "1.6" nebo "1,6" v rozsahu, jinak FALSE
static boolean Desetinne (const char *p, double fMin, double fMax, double *pVysledek)
{
	if (p == 0 || *p == '\0') return FALSE;
	double f = 0.0, fRad = 0.0;
	unsigned nCislic = 0;
	for (; *p; p++)
	{
		if (*p >= '0' && *p <= '9')
		{
			if (fRad == 0.0) f = f * 10.0 + (*p - '0');
			else { f += (*p - '0') * fRad; fRad *= 0.1; }
			if (++nCislic > 12) return FALSE;
		}
		else if ((*p == '.' || *p == ',') && fRad == 0.0)
		{
			fRad = 0.1;
		}
		else
		{
			return FALSE;
		}
	}
	if (nCislic == 0 || f < fMin - 1e-9 || f > fMax + 1e-9) return FALSE;
	*pVysledek = f;
	return TRUE;
}

// [prevodovka]: klic, meze, vychozi hodnota a texty pro hlasky
struct TPolePrevodovky
{
	const char *pKlic;
	double fMin, fMax, fVychozi;
	boolean bCele;
	const char *pRozsah, *pVychozi;
};
static const TPolePrevodovky PolePrev[] =
{
	{"prevodovka.stupnu",	    1,   6,   3,    TRUE,  "cele cislo 1 az 6",      "3"},
	{"prevodovka.pomer",	    1.2, 2.2, 1.6,  FALSE, "cislo 1,2 az 2,2",       "1,6"},
	{"prevodovka.rozjezd",	    1,   12,  5,    FALSE, "cislo 1 az 12 (s)",      "5"},
	{"prevodovka.dobeh",	    1,   12,  3,    FALSE, "cislo 1 az 12 (s)",      "3"},
	{"prevodovka.navrat",	    40,  800, 250,  TRUE,  "cele cislo 40 az 800 (ms)", "250"},
	{"prevodovka.propad",	    0,   60,  0,    TRUE,  "cele cislo 0 az 60 (%)", "0"},
	{"prevodovka.zpatecka",	    10,  100, 40,   TRUE,  "cele cislo 10 az 100 (%)", "40"},
	{"prevodovka.max_rychlost", 1.2, 3.0, 1.81, FALSE, "cislo 1,2 az 3,0",       "1,81"},
};
#define POLI_PREV	(sizeof PolePrev / sizeof PolePrev[0])

// znamy klic? (aby se dal ohlasit preklep)
static boolean ZnamyKlic (const char *pKlic)
{
	static const char *const Zname[] =
	{
		"vystup.zarizeni", "vystup.reproduktor", "vystup.hlasitost",
		"kalibrace.min", "kalibrace.max", "kalibrace.stred",
		"kalibrace.stred_plyn", "kalibrace.stred_vez", 0
	};
	for (unsigned i = 0; Zname[i]; i++)
	{
		if (strcmp (pKlic, Zname[i]) == 0) return TRUE;
	}
	for (unsigned i = 0; i < POLI_PREV; i++)
	{
		if (strcmp (pKlic, PolePrev[i].pKlic) == 0) return TRUE;
	}
	if (strncmp (pKlic, "kanaly.", 7) == 0)
	{
		for (unsigned i = 0; i < FUNKCI; i++)
		{
			if (strcmp (pKlic + 7, NazevFunkce[i]) == 0) return TRUE;
		}
	}
	return FALSE;
}

void NactiNastaveni (const CIni &Ini, boolean bSouborOK, TNastaveni *pNast,
		     char *pZprava, unsigned nZprava)
{
	if (pZprava && nZprava) pZprava[0] = '\0';
	pNast->nOprav = 0;
	CString s;
	auto Oprava = [&] (const char *pText) { Pripis (pZprava, nZprava, pText); pNast->nOprav++; };

	if (Ini.PocetChyb ())
	{
		Pripis (pZprava, nZprava, Ini.Chyby ());
		pNast->nOprav += Ini.PocetChyb ();
	}
	for (unsigned i = 0; i < Ini.Polozek (); i++)
	{
		if (!ZnamyKlic (Ini.Klic (i)))
		{
			s.Format ("radek %u: neznamy nazev %s, ignoruji", Ini.Radek (i), Ini.Klic (i));
			Oprava (s);
		}
	}

	// [vystup]
	pNast->Zarizeni = ZarizeniI2S;
	const char *p = Ini.Hodnota ("vystup.zarizeni");
	if (p != 0)
	{
		if (strcmp (p, "i2s") == 0) pNast->Zarizeni = ZarizeniI2S;
		else if (strcmp (p, "jack") == 0) pNast->Zarizeni = ZarizeniJack;
		else { s.Format ("vystup.zarizeni = %s neznam (i2s nebo jack), beru i2s", p); Oprava (s); }
	}
	else if (bSouborOK)
	{
		Oprava ("chybi vystup.zarizeni, beru i2s");
	}

	pNast->Strana = StranaOba;
	p = Ini.Hodnota ("vystup.reproduktor");
	if (p != 0)
	{
		if (strcmp (p, "oba") == 0) pNast->Strana = StranaOba;
		else if (strcmp (p, "levy") == 0) pNast->Strana = StranaLevy;
		else if (strcmp (p, "pravy") == 0) pNast->Strana = StranaPravy;
		else { s.Format ("vystup.reproduktor = %s neznam (oba, levy, pravy), beru oba", p); Oprava (s); }
	}

	pNast->nHlasitost = 100;
	p = Ini.Hodnota ("vystup.hlasitost");
	int n;
	if (p != 0)
	{
		if (Cele (p, 0, 100, &n)) pNast->nHlasitost = (unsigned) n;
		else { s.Format ("vystup.hlasitost = %s (ma byt 0 az 100), beru 100", p); Oprava (s); }
	}

	// [kanaly]
	for (unsigned i = 0; i < FUNKCI; i++)
	{
		s.Format ("kanaly.%s", NazevFunkce[i]);
		pNast->Kanal[i] = KanalVychozi[i];
		p = Ini.Hodnota (s);
		if (p == 0)
		{
			if (bSouborOK)
			{
				CString t; t.Format ("chybi %s, beru CH%u", (const char *) s, KanalVychozi[i]);
				Oprava (t);
			}
		}
		else if (Cele (p, 1, 16, &n))
		{
			pNast->Kanal[i] = (unsigned) n;
		}
		else
		{
			CString t; t.Format ("%s = %s (ma byt 1 az 16), beru CH%u", (const char *) s, p, KanalVychozi[i]);
			Oprava (t);
		}
	}
	// stejny kanal u vice funkci je povoleny (muze byt zamer), jen upozornime
	for (unsigned i = 0; i < FUNKCI; i++)
	{
		for (unsigned j = i + 1; j < FUNKCI; j++)
		{
			if (pNast->Kanal[i] == pNast->Kanal[j])
			{
				s.Format ("pozor: %s i %s maji CH%u", NazevFunkce[i], NazevFunkce[j], pNast->Kanal[i]);
				Pripis (pZprava, nZprava, s);
			}
		}
	}

	// [kalibrace] - min/stred/max jen vsechny tri, nebo vychozi
	pNast->nMin = 172; pNast->nMax = 1811; pNast->nStred = 992;
	const char *pMin = Ini.Hodnota ("kalibrace.min");
	const char *pMax = Ini.Hodnota ("kalibrace.max");
	const char *pStred = Ini.Hodnota ("kalibrace.stred");
	if (pMin || pMax || pStred)
	{
		int mn, mx, md;
		if (   Cele (pMin, 0, 2047, &mn) && Cele (pMax, 0, 2047, &mx) && Cele (pStred, 0, 2047, &md)
		    && mn < md && md < mx && mx - mn >= 200)
		{
			pNast->nMin = mn; pNast->nMax = mx; pNast->nStred = md;
		}
		else
		{
			Oprava ("kalibrace: min, stred a max musi byt vsechny, min < stred < max, beru 172/992/1811");
		}
	}
	else if (bSouborOK)
	{
		Oprava ("chybi kalibrace, beru 172/992/1811");
	}

	// [prevodovka] - hodnoty jako v simulatoru (procenta, sekundy, ms)
	double Hodnoty[POLI_PREV];
	for (unsigned i = 0; i < POLI_PREV; i++)
	{
		const TPolePrevodovky &P = PolePrev[i];
		Hodnoty[i] = P.fVychozi;
		p = Ini.Hodnota (P.pKlic);
		double f;
		if (p == 0)
		{
			if (bSouborOK)
			{
				s.Format ("chybi %s, beru %s", P.pKlic, P.pVychozi);
				Oprava (s);
			}
		}
		else if (   Desetinne (p, P.fMin, P.fMax, &f)
			 && (!P.bCele || f == (double) (long long) f))
		{
			Hodnoty[i] = f;
		}
		else
		{
			s.Format ("%s = %s (ma byt %s), beru %s", P.pKlic, p, P.pRozsah, P.pVychozi);
			Oprava (s);
		}
	}
	pNast->Prev.nStupnu   = (int) Hodnoty[0];
	pNast->Prev.fPrevod   = Hodnoty[1];
	pNast->Prev.fRozjezd  = Hodnoty[2];
	pNast->Prev.fDobeh    = Hodnoty[3];
	pNast->Prev.fNavrat   = Hodnoty[4];
	pNast->Prev.fPropad   = Hodnoty[5] / 100.0;
	pNast->Prev.fZpatecka = Hodnoty[6] / 100.0;
	pNast->Prev.fRateMax  = Hodnoty[7];

	pNast->nStredPlyn = pNast->nStredVez = pNast->nStred;
	static const char *const Stredy[2] = {"kalibrace.stred_plyn", "kalibrace.stred_vez"};
	for (unsigned i = 0; i < 2; i++)
	{
		p = Ini.Hodnota (Stredy[i]);
		if (p == 0) continue;
		if (Cele (p, pNast->nMin + 1, pNast->nMax - 1, &n))
		{
			(i == 0 ? pNast->nStredPlyn : pNast->nStredVez) = n;
		}
		else
		{
			s.Format ("%s = %s je mimo kalibraci, beru stred %d", Stredy[i], p, pNast->nStred);
			Oprava (s);
		}
	}
}

int PrahPrepinace (const TNastaveni &Nast)
{
	int nHorni = Nast.nMax - Nast.nStred;
	return Nast.nStred + (nHorni < 1 ? 1 : nHorni) * 58 / 100;
}

void MezePrepinace (const TNastaveni &Nast, CPrepinac *pPrepinac)
{
	int nHorni = Nast.nMax - Nast.nStred;
	if (nHorni < 1) nHorni = 1;
	pPrepinac->Meze (Nast.nStred + nHorni * 58 / 100, Nast.nStred + nHorni * 42 / 100);
}

unsigned Procenta (int v, const TNastaveni &Nast)
{
	int nRozsah = Nast.nMax - Nast.nMin;
	if (nRozsah < 1) nRozsah = 1;
	int p = (v - Nast.nMin) * 100 / nRozsah;
	if (p < 0) p = 0;
	if (p > 100) p = 100;
	return (unsigned) p;
}
