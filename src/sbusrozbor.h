//
// sbusrozbor.h - hledani vypadku v prijatych SBus ramcich (bez zavislosti na HW)
//
// Pro kazdy platny ramec se zavola Ramec(). Najde:
//   mezeru     - dlouho zadny platny ramec (vypadek na vodici nebo prijimace)
//   ztraceno   - prijimac hlasi ztracene ramce z vysilace (priznak LOST);
//                hodnoty pritom drzi, takze se to v RC obvykle neprojevi
//   skok       - hodnota kanalu na 1-2 ramce odskoci a vrati se (poskozeny
//                ramec - rukou se paka ani prepinac takhle rychle nepohne)
// Udalosti si vybira kernel do logu.
//
#ifndef _sbusrozbor_h
#define _sbusrozbor_h

#include <circle/types.h>
#include "sbusramec.h"

#define SBUS_MEZERA_US	50000		// mezera mezi ramci delsi nez tohle = vypadek
#define SBUS_SKOK	200		// odskok kanalu aspon o tolik (cca 12 % rozsahu)...
#define SBUS_VRAT	60		// ...a navrat do tolika od puvodni hodnoty

enum TSBusJev
{
	JevMezera,		// nTrvaniUs
	JevZtraceno,		// nTrvaniUs, nPocet ramcu
	JevSkok			// nKanal (0..15), nZ -> nNa -> nPo, nDelka ramcu
};

struct TSBusUdalost
{
	TSBusJev Typ;
	unsigned nTrvaniUs;
	unsigned nPocet;
	unsigned nKanal;
	unsigned nZ, nNa, nPo;
	unsigned nDelka;
};

class CSBusRozbor
{
public:
	CSBusRozbor (void)
	:	m_nMezer (0), m_nMaxMezeraUs (0), m_nZtrat (0), m_nZtracenychRamcu (0),
		m_nMaxZtrataUs (0), m_nSkoku (0),
		m_bMeritMezeru (FALSE), m_nPredUs (0), m_bZtrata (FALSE), m_nZtrataOdUs (0),
		m_nZtrataRamcu (0), m_nHist (0), m_nCteni (0), m_nZapis (0)
	{
	}

	// Dalsi mezera se nemeri (hlavni smycka byla zdrzena, napr. zapisem
	// log.txt - ramce se pak vyberou najednou a mezera by byla falesna).
	void Preskoc (void)		{ m_bMeritMezeru = FALSE; }

	void Ramec (const u8 *b, const u16 *pKanal, unsigned nUs)
	{
		// mezera od predchoziho platneho ramce
		if (m_bMeritMezeru)
		{
			unsigned d = nUs - m_nPredUs;
			if (d > m_nMaxMezeraUs) m_nMaxMezeraUs = d;
			if (d > SBUS_MEZERA_US)
			{
				m_nMezer++;
				TSBusUdalost U = {JevMezera, d, 0, 0, 0, 0, 0, 0};
				Pridej (U);
			}
		}
		m_bMeritMezeru = TRUE;
		m_nPredUs = nUs;

		// prijimac nedostal ramec z vysilace
		boolean bZtrata = (b[23] & SBUS_FLAG_LOST) != 0;
		if (bZtrata)
		{
			m_nZtracenychRamcu++;
			if (!m_bZtrata) { m_bZtrata = TRUE; m_nZtrataOdUs = nUs; m_nZtrataRamcu = 0; }
			m_nZtrataRamcu++;
		}
		else if (m_bZtrata)
		{
			m_bZtrata = FALSE;
			unsigned d = nUs - m_nZtrataOdUs;
			m_nZtrat++;
			if (d > m_nMaxZtrataUs) m_nMaxZtrataUs = d;
			TSBusUdalost U = {JevZtraceno, d, m_nZtrataRamcu, 0, 0, 0, 0, 0};
			Pridej (U);
		}

		// odskok kanalu na 1 nebo 2 ramce. m_Hist[0] = predchozi ramec.
		for (unsigned nDelka = 1; nDelka <= 2; nDelka++)
		{
			if (m_nHist < nDelka + 1) break;
			for (unsigned c = 0; c < SBUS_KANALU; c++)
			{
				int nPred = m_Hist[nDelka][c], nPo = pKanal[c];
				if (Abs (nPo - nPred) > SBUS_VRAT) continue;
				boolean bOdskok = TRUE;
				int nNejdal = nPred;
				for (unsigned k = 0; k < nDelka; k++)
				{
					int v = m_Hist[k][c];
					if (Abs (v - nPred) <= SBUS_SKOK || Abs (v - nPo) <= SBUS_SKOK) { bOdskok = FALSE; break; }
					if (Abs (v - nPred) > Abs (nNejdal - nPred)) nNejdal = v;
				}
				if (!bOdskok) continue;
				m_nSkoku++;
				TSBusUdalost U = {JevSkok, 0, 0, c, (unsigned) nPred, (unsigned) nNejdal, (unsigned) nPo, nDelka};
				Pridej (U);
			}
		}
		for (unsigned k = HIST - 1; k > 0; k--)
		{
			for (unsigned c = 0; c < SBUS_KANALU; c++) m_Hist[k][c] = m_Hist[k - 1][c];
		}
		for (unsigned c = 0; c < SBUS_KANALU; c++) m_Hist[0][c] = pKanal[c];
		if (m_nHist < HIST) m_nHist++;
	}

	// vybere nejstarsi udalost; FALSE = zadna neni
	boolean Vyber (TSBusUdalost *pU)
	{
		if (m_nCteni == m_nZapis) return FALSE;
		*pU = m_Fronta[m_nCteni % FRONTA];
		m_nCteni++;
		return TRUE;
	}

public:
	// statistika od zapnuti
	unsigned m_nMezer;
	unsigned m_nMaxMezeraUs;	// nejdelsi zmerena mezera mezi ramci vubec
	unsigned m_nZtrat;		// epizod ztracenych ramcu
	unsigned m_nZtracenychRamcu;
	unsigned m_nMaxZtrataUs;
	unsigned m_nSkoku;

private:
	enum { HIST = 3, FRONTA = 32 };

	static int Abs (int x)		{ return x < 0 ? -x : x; }

	void Pridej (const TSBusUdalost &U)
	{
		if (m_nZapis - m_nCteni >= FRONTA) m_nCteni++;	// plna: zahodit nejstarsi
		m_Fronta[m_nZapis % FRONTA] = U;
		m_nZapis++;
	}

	boolean	 m_bMeritMezeru;
	unsigned m_nPredUs;
	boolean	 m_bZtrata;
	unsigned m_nZtrataOdUs;
	unsigned m_nZtrataRamcu;
	u16	 m_Hist[HIST][SBUS_KANALU];
	unsigned m_nHist;
	TSBusUdalost m_Fronta[FRONTA];
	unsigned m_nCteni, m_nZapis;
};

#endif
