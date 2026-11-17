//
// kernel.h - SBusSound bare metal, krok 5: motor, kulomet, vez, delo, pozadi
//
#ifndef _kernel_h
#define _kernel_h

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/screen.h>
#include <circle/nulldevice.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/string.h>
#include <circle/sound/soundbasedevice.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>
#include <circle/types.h>
#include "mixer.h"
#include "vystup.h"
#include "sbus.h"
#include "ini.h"
#include "nastaveni.h"
#include "wav.h"

// Kod chyby = pocet kratkych bliknuti LED (po jednom dlouhem) a pocet pipnuti
enum TKodChyby
{
	ChybaKarta   = 2,	// karta nebo FAT oddil nejde precist
	ChybaPamet   = 5,	// malo pameti
	ChybaZvuk    = 6,	// nejde spustit zvukovy vystup (jen LED)
	ChybaUART    = 7	// nejde nastavit seriovy port pro SBus
};

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);
	void Run (void);

private:
	void Chyba (TKodChyby Kod);		// nevraci se
	void Udalost (const char *pText);	// do logu a do historie pro log.txt
	void Vypadek (const char *pText);	// do logu a do historie vypadku signalu
	void SledujVypadky (unsigned nTed);	// udalosti z rozboru SBus, chyby linky
	void Mapuj (const TZvukyMixeru &Z);	// mapovani kanalu, konci restartem
	void MapCekej (unsigned nMs);		// cekani v mapovani: SBus, LED, log
	boolean UlozMapovani (const unsigned *pKanal);
	void ZapisDiag (void);
	CString Prehled (void);
	unsigned Ms (void) const;
	boolean SpustVystup (void);
	void NactiZvuk (const char *pJmeno, TZvuk *pZvuk);

private:
	// poradi nemenit - zavisi na nem inicializace
	CActLED			m_ActLED;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	CScreenDevice		m_Screen;
	CNullDevice		m_Nic;
	CExceptionHandler	m_ExceptionHandler;
	CInterruptSystem	m_Interrupt;
	CTimer			m_Timer;
	CLogger			m_Logger;
	CEMMCDevice		m_EMMC;
	FATFS			m_FileSystem;
	CMixer			m_Mixer;
	CSBus			m_SBus;

	CSoundBaseDevice	*m_pVystup;
	boolean			m_bZvukBezi;
	CIni			m_Ini;
	TNastaveni		m_Nast;
	CString			m_NastStav;
	CString			m_ZvukStav;
	CPrepinac		m_PrepMotor;
	CPrepinac		m_PrepKulomet;
	CPrepinac		m_PrepDelo;
	CPrepinac		m_PrepPozadi;
	CPrepinac		m_PrepVez;	// na odchylce paky od stredu
	boolean			m_bDiag;
	unsigned		m_nMaxZapisMs;	// nejdelsi zapis log.txt

	// stav vstupu (posila se mixeru a vypisuje do logu)
	TVstupy			m_V;
	unsigned		m_nHlasitost;

	// posledni udalosti pro log.txt
	enum { UDALOSTI = 20 };
	CString			m_Udalost[UDALOSTI];
	unsigned		m_nUdalosti;

	// vypadky signalu zvlast, aby je nevytlacilo razeni a prepinace
	enum { VYPADKU = 40 };
	CString			m_Vypadek[VYPADKU];
	unsigned		m_nVypadku;
	unsigned		m_nFailsafu, m_nBezRamcu;	// epizod od zapnuti
	unsigned		m_nRamcuZaS;			// platnych ramcu za posledni sekundu
	unsigned		m_nRamcuPred, m_nRamcuCas;
	unsigned		m_nLinkaCas;			// posledni kontrola chyb linky
	unsigned		m_LinkaPred[6];
	int			m_nMapHlas;	// mapovani: kanal hlasitosti 0..15, -1 = jeste neni			// parita, ramec, preteceni, break, zahozeno, s chybou
};

#endif
