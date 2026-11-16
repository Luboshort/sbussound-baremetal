//
// wav.h - nacteni WAV do pameti jako mono 16 bit / 44100 Hz
//
#ifndef _wav_h
#define _wav_h

#include <circle/types.h>

enum TWavChyba
{
	WavOK = 0,
	WavNelzeOtevrit,	// soubor neni nebo nejde cist
	WavFormat,		// neni to WAV nebo neznamy format
	WavPamet,		// malo pameti
	WavPrazdny		// WAV bez zvuku
};

struct TZvuk
{
	s16	*pData;		// mono, 44100 Hz
	unsigned nVzorku;
	// puvodni format, jen pro vypis
	unsigned nPuvRate;
	unsigned nPuvKanaly;
	unsigned nPuvBity;
};

// cesta napr. "SD:/sounds/machinegun.wav"
TWavChyba NactiWav (const char *pCesta, TZvuk *pZvuk);

const char *PopisChyby (TWavChyba Chyba);

#endif
