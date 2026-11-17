//
// mapovani.cpp
//
#include "mapovani.h"
#include <circle/util.h>

namespace
{
	class CVystup
	{
	public:
		CVystup (char *p, unsigned nMax) : m_p (p), m_nMax (nMax), m_n (0), m_bPlno (FALSE) {}
		void Pis (const char *p, unsigned n)
		{
			if (m_n + n > m_nMax) { m_bPlno = TRUE; return; }
			memcpy (m_p + m_n, p, n);
			m_n += n;
		}
		void Pis (const char *p)	{ Pis (p, strlen (p)); }
		void Mezery (unsigned n)	{ while (n--) Pis (" ", 1); }
		unsigned Delka (void) const	{ return m_bPlno ? 0 : m_n; }
		char Posledni (void) const	{ return m_n ? m_p[m_n - 1] : '\n'; }
	private:
		char *m_p;
		unsigned m_nMax, m_n;
		boolean m_bPlno;
	};

	boolean Mezera (char c)	{ return c == ' ' || c == '\t'; }
	char Male (char c)	{ return c >= 'A' && c <= 'Z' ? c + 32 : c; }

	// Radek se stejnym nazvem klice? (bez ohledu na velikost pismen a mezery)
	boolean JeKlic (const char *pZac, const char *pRovna, const char *pKlic)
	{
		while (pZac < pRovna && Mezera (*pZac)) pZac++;
		while (pRovna > pZac && Mezera (pRovna[-1])) pRovna--;
		unsigned n = strlen (pKlic);
		if ((unsigned) (pRovna - pZac) != n) return FALSE;
		for (unsigned i = 0; i < n; i++)
		{
			if (Male (pZac[i]) != pKlic[i]) return FALSE;
		}
		return TRUE;
	}

	// Radek "klic = stara  # komentar" s novou hodnotou. Komentar zustane
	// na stejnem miste, pokud to jde.
	void NahradHodnotu (CVystup &V, const char *pZac, const char *pRovna, const char *pKonec,
			    const char *pNova)
	{
		const char *pH = pRovna + 1;
		while (pH < pKonec && Mezera (*pH)) pH++;
		const char *pHk = pH;
		while (pHk < pKonec && !Mezera (*pHk) && *pHk != '#' && *pHk != ';' && *pHk != '\r' && *pHk != '\n') pHk++;
		V.Pis (pZac, pH - pZac);
		V.Pis (pNova);
		unsigned nStara = pHk - pH, nNova = strlen (pNova);
		const char *pZbytek = pHk;
		if (nNova < nStara && pZbytek < pKonec && Mezera (*pZbytek))
		{
			V.Mezery (nStara - nNova);
		}
		else
		{
			// delsi hodnota: ubrat mezery pred komentarem, aspon jedna zustane
			for (unsigned i = nStara; i < nNova && pZbytek + 1 < pKonec
						  && Mezera (pZbytek[0]) && Mezera (pZbytek[1]); i++)
			{
				pZbytek++;
			}
		}
		V.Pis (pZbytek, pKonec - pZbytek);
	}
}

unsigned UpravNastaveni (const char *pIn, unsigned nIn, const unsigned *pKanal,
			 char *pOut, unsigned nMax)
{
	CVystup V (pOut, nMax);
	boolean bKanal[FUNKCI];
	for (unsigned i = 0; i < FUNKCI; i++) bKanal[i] = FALSE;
	boolean bMapovat = FALSE;
	char Sekce[32] = "";
	char Cislo[8];

	const char *p = pIn, *pKonec = pIn + nIn;
	while (p < pKonec)
	{
		const char *pZac = p;
		while (p < pKonec && *p != '\n') p++;
		const char *pKon = p;		// konec radku bez '\n'
		if (p < pKonec) p++;

		const char *q = pZac;
		while (q < pKon && Mezera (*q)) q++;
		if (q < pKon && *q == '[')
		{
			unsigned n = 0;
			for (q++; q < pKon && *q != ']'; q++)
			{
				if (!Mezera (*q) && n + 1 < sizeof Sekce) Sekce[n++] = Male (*q);
			}
			Sekce[n] = '\0';
			V.Pis (pZac, p - pZac);
			continue;
		}
		// '=' pred zacatkem komentare
		const char *pRovna = 0;
		for (q = pZac; q < pKon && *q != '#' && *q != ';'; q++)
		{
			if (*q == '=') { pRovna = q; break; }
		}
		boolean bHotovo = FALSE;
		if (pRovna != 0 && strcmp (Sekce, "kanaly") == 0)
		{
			for (unsigned i = 0; i < FUNKCI; i++)
			{
				if (JeKlic (pZac, pRovna, NazevFunkce[i]))
				{
					unsigned c = pKanal[i], n = 0;
					char Obr[8];
					do { Obr[n++] = '0' + c % 10; c /= 10; } while (c && n < sizeof Obr);
					for (unsigned k = 0; k < n; k++) Cislo[k] = Obr[n - 1 - k];
					Cislo[n] = '\0';
					NahradHodnotu (V, pZac, pRovna, p, Cislo);
					bKanal[i] = bHotovo = TRUE;
					break;
				}
			}
		}
		else if (pRovna != 0 && strcmp (Sekce, "diagnostika") == 0 && JeKlic (pZac, pRovna, "mapovat"))
		{
			NahradHodnotu (V, pZac, pRovna, p, "ne");
			bMapovat = bHotovo = TRUE;
		}
		if (!bHotovo)
		{
			V.Pis (pZac, p - pZac);
		}
	}

	// co chybelo, pripsat na konec
	boolean bChybi = FALSE;
	for (unsigned i = 0; i < FUNKCI; i++) if (!bKanal[i]) bChybi = TRUE;
	if (bChybi)
	{
		if (V.Posledni () != '\n') V.Pis ("\n");
		V.Pis ("\n[kanaly]\n");
		for (unsigned i = 0; i < FUNKCI; i++)
		{
			if (bKanal[i]) continue;
			V.Pis (NazevFunkce[i]);
			V.Pis (" = ");
			unsigned c = pKanal[i];
			char s[3] = {(char) ('0' + c / 10), (char) ('0' + c % 10), 0};
			V.Pis (c >= 10 ? s : s + 1);
			V.Pis ("\n");
		}
	}
	if (!bMapovat)
	{
		if (V.Posledni () != '\n') V.Pis ("\n");
		V.Pis ("\n[diagnostika]\nmapovat = ne\n");
	}
	return V.Delka ();
}
