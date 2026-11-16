//
// prevodovka.h - setrvacnost tanku a prevodovka. Prima kopie tridy
// Prevodovka ze sound_engine2.py, vcetne opravy podrazovani.
//
#ifndef _prevodovka_h
#define _prevodovka_h

struct TParPrevodovky
{
	int	nStupnu;	// 1..6
	double	fPrevod;	// pomer mezi stupni 1,2..2,2
	double	fRozjezd;	// s na plnou rychlost (tAccel)
	double	fDobeh;		// s do zastaveni (tDecel)
	double	fNavrat;	// ms, casova konstanta otacek (dip)
	double	fPropad;	// 0..0,6, propad otacek pri razeni nahoru
	double	fZpatecka;	// 0,1..1, nejvyssi rychlost vzad (zpetMax)
	double	fRateMax;	// 1,2..3, rychlost prehravani pri plnych otackach
};

class CPrevodovka
{
public:
	CPrevodovka (void);
	void Parametry (const TParPrevodovky &Par);

	// plyn -1..1 (zaporny = vzad), dt v sekundach. Vraci TRUE, kdyz radil.
	bool Krok (double fPlyn, double fDt);
	double Rate (void) const;
	void Nuluj (void);		// pri vypnuti motoru

public:
	double	m_fRychlost;		// 0..1
	int	m_nSmer;		// 1 vpred, -1 vzad
	int	m_nStupen;		// 1..N
	double	m_fOtacky;		// 0..1

private:
	double Vrchol (int g) const;

	TParPrevodovky m_P;
	double m_fLnRate;
};

#endif
