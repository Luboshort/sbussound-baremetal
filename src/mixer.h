//
// mixer.h - "srdce" enginu: stavovy automat motoru, prevodovka a smes zvuku.
// Odpovida tride Engine ze sound_engine2.py. Bezi v preruseni zvukoveho
// vystupu - kazdy blok 512 vzorku (11,6 ms) spocita krok prevodovky a smes.
// Hlavni smycka jen nastavuje vstupy (Vstupy) a cte stav pro log.
//
#ifndef _mixer_h
#define _mixer_h

#include <circle/types.h>
#include "nastaveni.h"
#include "hlasy.h"
#include "prevodovka.h"

enum TStavMotoru
{
	MotorVypnuty,		// OFF
	MotorStartuje,		// STARTING - hraje motor_start, plyn nereaguje
	MotorBezi,		// RUN
	MotorDobiha		// STOPPING - hraje motor_stop
};

const char *NazevStavu (TStavMotoru S);

struct TZvukyMixeru		// vse muze byt 0 = zvuk chybi
{
	const s16 *pStart;	unsigned nStart;
	const s16 *pBeh;	unsigned nBeh;
	const s16 *pStop;	unsigned nStop;
	const s16 *pKulomet;	unsigned nKulomet;
	const s16 *pVez;	unsigned nVez;
	const s16 *pDelo;	unsigned nDelo;
	const s16 *pPozadi;	unsigned nPozadi;
};

struct TVstupy			// stav ovladacu z vysilace (nastavuje hlavni smycka)
{
	float	fPlyn;		// -1..1, zaporny = vzad
	boolean	bMotor;
	boolean	bVez;		// paka veze mimo stred
	boolean	bKulomet;
	boolean	bDelo;		// prepinac dela nahore
	boolean	bPozadi;
	boolean	bSignal;	// FALSE = ztrata vysilace: vse hned ztichne
	float	fHlasitost;	// 0..1 (kanal hlasitosti)
};

class CMixer
{
public:
	CMixer (void);

	// Pred spustenim vystupu. Vraci pocet pripravenych kopii motor_run.
	unsigned Priprav (const TZvukyMixeru &Zvuky, const TParPrevodovky &Prev);

	// Z nastaveni.txt: celkova hlasitost 0..100 % a strana reproduktoru.
	void Vystup (unsigned nCelkova, TStrana Strana);

	// Z hlavni smycky.
	void Vstupy (const TVstupy &V);

	// Pipani pro ohlaseni chyby: nPocet pipnuti, pauza, dokola.
	// Hraje na 80 % bez ohledu na hlasitost - musi byt slyset.
	void Pipani (unsigned nPocet);

	// Vola vystup z preruseni DMA. nChunkSize slov, stridave levy a pravy,
	// kazde v rozsahu nMin..nMax. Musi byt rychle a nesmi sahat na kartu.
	void Vypln (u32 *pBuffer, unsigned nChunkSize, int nMin, int nMax, boolean bProhodit);

	// Jeden blok smesi (mono, -1..1 pred hlasitosti). Verejne kvuli testum.
	void Blok (float *pOut, unsigned n);

public:
	// stav pro vypis (zapisuje preruseni, cte hlavni smycka)
	volatile TStavMotoru m_Stav;
	volatile unsigned m_nRazeni;
	volatile unsigned m_nVystrelu;

	// mereni pro log (od startu)
	volatile float	  m_fSpicka;	// nejvyssi spicka smesi pred limiterem (1,0 = plny rozsah)
	volatile float	  m_fMinZes;	// nejvetsi ztlumeni limiterem (1,0 = nikdy)
	volatile unsigned m_nPretizeno;	// vzorku, kde musel limiter ztlumit
	volatile unsigned m_nOrezano;	// vzorku urizlych i po limiteru (ma byt 0)
	volatile unsigned m_nMaxUs;	// nejdelsi vypocet jednoho bloku v us
	void ZaznamenejCas (unsigned nUs)	{ if (nUs > m_nMaxUs) m_nMaxUs = nUs; }

	// Casovani vystupu: blok se ma pocitat kazdych nVzorku/44100 s. Kdyz se
	// preruseni zdrzi o vic nez delku bloku, prehraje se stary blok = prasknuti.
	volatile unsigned m_nMaxRozestup;	// nejdelsi rozestup mezi bloky v us
	volatile unsigned m_nPozde;		// rozestup nad 1,5x delky bloku (na hrane)
	volatile unsigned m_nPodteceni;		// rozestup nad 2x delky bloku (jiste prasklo)
	volatile unsigned m_nBloku;
	void ZaznamenejVolani (unsigned nTedUs, unsigned nVzorku);
	CPrevodovka m_Box;

private:
	// vstupy
	volatile float m_fPlyn;
	volatile boolean m_bMotor;
	volatile boolean m_bVez;
	volatile boolean m_bKulomet;
	volatile boolean m_bDelo;
	volatile boolean m_bPozadi;
	volatile boolean m_bSignal;
	volatile float m_fHlasitost;
	boolean m_bDeloNabito;

	CMotorHlas m_Motor;
	CJednoraz m_Start;
	CJednoraz m_Stop;
	CSmycka m_Kulomet;
	CSmycka m_Vez;
	CSmycka m_Pozadi;
	CJednoraz m_Delo;

	float m_fHlasitostPred;
	float m_fLimiter;		// aktualni zesileni limiteru 0..1
	unsigned m_nPosledniVolani;
	volatile unsigned m_nCelkova;	// %
	volatile TStrana m_Strana;

	volatile boolean m_bPipani;
	CSmycka m_Pipak;
	s16 *m_pPipani;

	enum { MAX_BLOK = 2048 };
	float m_Smes[MAX_BLOK];
};

#endif
