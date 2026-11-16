//
// ini.h - cteni nastaveni.txt: [sekce] a radky "klic = hodnota", komentar za # nebo ;
//
#ifndef _ini_h
#define _ini_h

#include <circle/types.h>

class CIni
{
public:
	CIni (void);

	// FALSE, kdyz soubor neni nebo nejde precist (popis v Chyby()).
	boolean Nacti (const char *pCesta);
	void Zpracuj (const char *pText, unsigned nDelka);

	// Hodnota pod "sekce.klic" (mala pismena), nebo 0.
	const char *Hodnota (const char *pKlic) const;

	unsigned Polozek (void) const			{ return m_nPolozek; }
	const char *Klic (unsigned i) const		{ return m_Polozky[i].Klic; }
	unsigned Radek (unsigned i) const		{ return m_Polozky[i].nRadek; }

	// Syntakticke chyby ("radek 12: ..."), oddelene "; "
	const char *Chyby (void) const			{ return m_Chyby; }
	unsigned PocetChyb (void) const			{ return m_nChyb; }

private:
	void Chyba (unsigned nRadek, const char *pText);

private:
	enum { MAX_POLOZEK = 64, MAX_KLIC = 40, MAX_HODNOTA = 24 };
	struct TPolozka
	{
		char	 Klic[MAX_KLIC];
		char	 Hodnota[MAX_HODNOTA];
		unsigned nRadek;
	};
	TPolozka m_Polozky[MAX_POLOZEK];
	unsigned m_nPolozek;
	char	 m_Chyby[256];
	unsigned m_nChyb;
};

#endif
