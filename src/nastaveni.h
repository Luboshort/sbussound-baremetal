//
// nastaveni.h - z nastaveni.txt udela overena cisla pro engine
//
#ifndef _nastaveni_h
#define _nastaveni_h

#include <circle/types.h>
#include "ini.h"
#include "prevodovka.h"

enum TFunkce
{
	FceVez, FceMotor, FcePlyn, FceKulomet, FceDelo, FcePozadi, FceHlasitost,
	FUNKCI
};

extern const char *const NazevFunkce[FUNKCI];	// nazvy klicu v sekci [kanaly]

enum TZarizeni
{
	ZarizeniI2S,		// GY-PCM5102 (PCM5102A) na GPIO18/19/21
	ZarizeniJack		// 3,5mm jack na desce (PWM)
};

enum TStrana
{
	StranaOba,
	StranaLevy,
	StranaPravy
};

struct TNastaveni
{
	TZarizeni Zarizeni;
	TStrana	  Strana;
	unsigned  nHlasitost;		// celkova hlasitost vystupu 0..100 %
	unsigned  Kanal[FUNKCI];	// 1..16
	int nMin, nMax, nStred;		// kalibrace
	int nStredPlyn, nStredVez;	// stred pak (kdyz neni zadan, nStred)
	TParPrevodovky Prev;		// [prevodovka]
	boolean	  bLog;			// [diagnostika] log = ano: kazde 2 s log.txt
	unsigned nOprav;		// kolik veci bylo spatne a nahrazeno
};

const char *NazevZarizeni (TZarizeni Z);
const char *NazevStrany (TStrana S);

// Vzdy naplni pNast. Co v souboru chybi, je spatne nebo navic, nahradi
// vychozi hodnotou a popise do pZprava (oddelene "; ").
void NactiNastaveni (const CIni &Ini, boolean bSouborOK, TNastaveni *pNast,
		     char *pZprava, unsigned nZprava);

// Dvoustavovy vstup s hysterezi - na hranici neblika.
class CPrepinac
{
public:
	CPrepinac (void) : m_nZap (0), m_nVyp (0), m_bStav (FALSE) {}
	void Meze (int nZap, int nVyp)	{ m_nZap = nZap; m_nVyp = nVyp; }
	boolean operator() (int v)
	{
		if (m_bStav) { if (v < m_nVyp) m_bStav = FALSE; }
		else if (v > m_nZap) m_bStav = TRUE;
		return m_bStav;
	}
	void Vypni (void)		{ m_bStav = FALSE; }
private:
	int m_nZap, m_nVyp;
	boolean m_bStav;
};

// Prepinace spinaji jen v HORNI poloze: prah v pulce mezi stredem
// a maximem s hysterezi (stejne jako sound_engine2.py).
void MezePrepinace (const TNastaveni &Nast, CPrepinac *pPrepinac);
int PrahPrepinace (const TNastaveni &Nast);

// 0..100 podle kalibrace
unsigned Procenta (int v, const TNastaveni &Nast);

#endif
