//
// prevodovka.cpp
//
#include "prevodovka.h"
#include "matika.h"

// Podrazuje se pod timto podilem vrcholu NIZSIHO stupne (musi byt < 1).
#define PODRAZ	0.85

CPrevodovka::CPrevodovka (void)
:	m_fRychlost (0.0),
	m_nSmer (1),
	m_nStupen (1),
	m_fOtacky (0.0)
{
	TParPrevodovky P = {3, 1.6, 5.0, 3.0, 250.0, 0.0, 0.4, 1.81};
	Parametry (P);
}

void CPrevodovka::Parametry (const TParPrevodovky &Par)
{
	m_P = Par;
	m_fLnRate = Ln (m_P.fRateMax);
}

double CPrevodovka::Vrchol (int g) const
{
	if (m_nSmer < 0)
	{
		return m_P.fZpatecka;
	}
	// 1 / prevod^(stupnu - g)
	double f = 1.0;
	for (int i = g; i < m_P.nStupnu; i++) f /= m_P.fPrevod;
	return f;
}

bool CPrevodovka::Krok (double fPlyn, double fDt)
{
	bool bRadil = false;
	bool bVzad = fPlyn < -0.02;
	double fVelikost = fPlyn < 0 ? -fPlyn : fPlyn;

	// smer se meni jen ve stoje
	if (m_fRychlost < 0.01)
	{
		if (bVzad && m_nSmer > 0) { m_nSmer = -1; m_nStupen = 1; }
		else if (!bVzad && fVelikost > 0.02 && m_nSmer < 0) { m_nSmer = 1; m_nStupen = 1; }
	}

	bool bSedi = (bVzad && m_nSmer < 0) || (!bVzad && m_nSmer > 0);
	double fStrop = m_nSmer > 0 ? 1.0 : m_P.fZpatecka;
	double fCil = bSedi ? fVelikost * fStrop : 0.0;

	double fZmena = (fCil > m_fRychlost ? fStrop / m_P.fRozjezd : fStrop / m_P.fDobeh) * fDt;
	if (fCil > m_fRychlost)
	{
		m_fRychlost = m_fRychlost + fZmena < fCil ? m_fRychlost + fZmena : fCil;
	}
	else
	{
		m_fRychlost = m_fRychlost - fZmena > fCil ? m_fRychlost - fZmena : fCil;
	}
	if (m_fRychlost < 0.001)
	{
		m_fRychlost = 0.0;
	}

	bool bNahoru = false;
	if (m_nSmer > 0)
	{
		while (m_nStupen < m_P.nStupnu && m_fRychlost > Vrchol (m_nStupen))
		{
			m_nStupen++; bRadil = true; bNahoru = true;
		}
		while (m_nStupen > 1 && m_fRychlost < Vrchol (m_nStupen - 1) * PODRAZ)
		{
			m_nStupen--; bRadil = true;
		}
	}
	else
	{
		m_nStupen = 1;
	}

	// propad navic - odpovida tomu, ze pri razeni uberes plyn. Jen nahoru.
	if (bNahoru && m_P.fPropad > 0)
	{
		m_fOtacky = m_fOtacky * (1.0 - m_P.fPropad);
		if (m_fOtacky < 0) m_fOtacky = 0;
	}

	double fCilOt = m_fRychlost / Vrchol (m_nStupen);
	if (fCilOt > 1.0) fCilOt = 1.0;
	double fTau = m_P.fNavrat / 1000.0;
	if (fTau < 0.02) fTau = 0.02;
	m_fOtacky += (fCilOt - m_fOtacky) * (1.0 - Exp (-fDt / fTau));
	return bRadil;
}

double CPrevodovka::Rate (void) const
{
	return Exp (m_fOtacky * m_fLnRate);		// rateMax ^ otacky
}

void CPrevodovka::Nuluj (void)
{
	m_fRychlost = 0.0;
	m_fOtacky = 0.0;
	m_nStupen = 1;
}
