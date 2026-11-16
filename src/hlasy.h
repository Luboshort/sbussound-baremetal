//
// hlasy.h - jednotlive zvuky ve smesi. Odpovidaji tridam Smycka, Jednoraz
// a Motor v sound_engine2.py. Vsechny maji navic plynuly nabeh a dobeh 5 ms,
// aby zapnuti a vypnuti necvakalo.
//
#ifndef _hlasy_h
#define _hlasy_h

#include <circle/types.h>

// Smycka v puvodni rychlosti (kulomet, pozdeji vez a pozadi).
// Vypnuta smycka stoji na miste a pri zapnuti pokracuje.
class CSmycka
{
public:
	CSmycka (void);
	void Data (const s16 *pData, unsigned nVzorku);
	void Zap (boolean bZap)		{ m_bZap = bZap; }
	void Blok (float *pOut, unsigned n, float fMix);
private:
	const s16 *m_pData;
	unsigned m_nVzorku, m_nPozice;
	volatile boolean m_bZap;
	float m_fZes;
};

// Zvuk, ktery zazni jednou (start, dobeh, pozdeji vystrel).
class CJednoraz
{
public:
	CJednoraz (void);
	void Data (const s16 *pData, unsigned nVzorku);
	void Spust (void);		// od zacatku
	void Stop (void);		// doznie za 5 ms
	boolean Bezi (void) const	{ return m_bBezi && !m_bKonci; }
	boolean Ma (void) const		{ return m_pData != 0; }
	void Blok (float *pOut, unsigned n, float fMix);
private:
	const s16 *m_pData;
	unsigned m_nVzorku, m_nPozice;
	boolean m_bBezi, m_bKonci;
	float m_fZes;
};

// Smycka motoru s plynule menenou rychlosti prehravani.
//
// Zrychlene prehravani posouva vsechny frekvence nahoru; co by se dostalo nad
// polovinu vzorkovaci frekvence, se zrcadli zpet jako kovovy sum (aliasing).
// Proto se pri nacteni pripravi nekolik kopii smycky s postupne nizsim
// orezem (kazda pro rychlost o 1,25x vyssi) a mezi dvema sousednimi se
// plynule prolina podle otacek. Filtr je kruhovy, takze konec kopie
// navazuje na zacatek stejne jako u originalu.
class CMotorHlas
{
public:
	CMotorHlas (void);
	// Pripravi kopie. Vraci pocet urovni (0 = malo pameti).
	unsigned Data (const s16 *pData, unsigned nVzorku, double fRateMax);
	void Zap (boolean bZap)		{ m_bZap = bZap; }
	// fOtacky 0..1, fRate = rateMax ^ fOtacky
	void Blok (float *pOut, unsigned n, double fRate, double fOtacky, float fMix);
private:
	enum { MAX_UROVNI = 8 };
	float *m_pUroven[MAX_UROVNI];
	unsigned m_nUrovni;
	unsigned m_nVzorku;
	double m_fPozice;
	volatile boolean m_bZap;
	float m_fZes;
};

#endif
