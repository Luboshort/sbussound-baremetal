//
// mapovani.h - zapis vysledku mapovani kanalu do nastaveni.txt
// (bez zavislosti na HW, testuje se i na pocitaci)
//
#ifndef _mapovani_h
#define _mapovani_h

#include "nastaveni.h"

// Prepise v textu nastaveni.txt hodnoty v sekci [kanaly] na pKanal[FUNKCI]
// a v sekci [diagnostika] mapovat na "ne". Komentare, mezery a ostatni radky
// zustanou, jak byly; zarovnani komentaru za hodnotou se zachova. Co v souboru
// chybi, se pripise na konec. Vraci delku vystupu, 0 = nevejde se do nMax.
unsigned UpravNastaveni (const char *pIn, unsigned nIn, const unsigned *pKanal,
			 char *pOut, unsigned nMax);

#endif
