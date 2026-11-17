//
// kernel.cpp - SBusSound bare metal, krok 5
//
// Nacte nastaveni.txt a vsech 7 zvuku a cte SBus z GPIO15.
//   motor   - prepinac nahore: motor_start, pak motor_run s prevodovkou podle
//             plynu (dozadu zpatecka), prepinac dolu: motor_stop
//   kulomet - smycka, dokud je prepinac nahore
//   vez     - smycka, dokud je paka veze mimo stred
//   delo    - jedna rana na jedno prepnuti nahoru
//   pozadi  - smycka, dokud je prepinac nahore
//   hlasitost podle kanalu hlasitosti. Pri ztrate signalu vse ztichne
//   a motor korektne dobehne.
//
// Zelena LED (ACT):
//   kratke bliknuti kazdou sekundu  = SBus signal v poradku
//   rychle blikani (4x za sekundu)  = zadny SBus signal nebo failsafe
//   1 dlouhe + N kratkych bliknuti  = chyba N (viz kernel.h), zaroven N pipnuti
//
// Kdyz je v nastaveni.txt [diagnostika] log = ano, zapisuje se kazde 2 s na
// kartu log.txt. Jen pro ladeni: vypnuti napajeni behem zapisu muze poskodit
// soubory na karte. Vypadky signalu (mezery mezi ramci, ztracene ramce,
// failsafe, odskoky kanalu, chyby linky) maji v logu vlastni seznam s casem.
//
#include "kernel.h"
#include "mapovani.h"
#include <circle/startup.h>
#include <circle/util.h>

#define VERZE		"krok 7"
#define DRIVE		"SD:"
#define NASTAVENI	DRIVE "/nastaveni.txt"
#define LOG		DRIVE "/log.txt"
#define NASTAVENI_NOVE	DRIVE "/nastaveni.new"	// zapis po mapovani, pak prejmenovani

#define PLYN_MRTVA	30		// mrtve pasmo plynu kolem stredu (jako v Pythonu)

static const char Zdroj[] = "sbussound";

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
	m_SBus (&m_Interrupt),
	m_pVystup (0),
	m_bZvukBezi (FALSE),
	m_bDiag (FALSE),
	m_nMaxZapisMs (0),
	m_nHlasitost (0),
	m_nUdalosti (0),
	m_nVypadku (0),
	m_nFailsafu (0),
	m_nBezRamcu (0),
	m_nRamcuZaS (0),
	m_nRamcuPred (0),
	m_nRamcuCas (0),
	m_nLinkaCas (0),
	m_nMapHlas (-1)
{
	memset (m_LinkaPred, 0, sizeof m_LinkaPred);
	m_ActLED.Blink (1, 300, 100);		// "zije" - hned po nacteni kernelu
	// vychozi hodnoty pro pripad, ze se k nastaveni.txt vubec nedostaneme
	NactiNastaveni (m_Ini, FALSE, &m_Nast, 0, 0);
	memset (&m_V, 0, sizeof m_V);
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	// Monitor je nepovinny: kdyz neni, loguje se do prazdna.
	CDevice *pLog = &m_Nic;
	if (m_Screen.Initialize ())
	{
		pLog = &m_Screen;
	}

	return    m_Logger.Initialize (pLog)
	       && m_Interrupt.Initialize ()
	       && m_Timer.Initialize ();
}

unsigned CKernel::Ms (void) const
{
	return (unsigned) (CTimer::GetClockTicks64 () / 1000);
}

void CKernel::NactiZvuk (const char *pJmeno, TZvuk *pZvuk)
{
	CString Cesta, s;
	Cesta.Format (DRIVE "/sounds/%s.wav", pJmeno);
	TWavChyba Vysledek = NactiWav (Cesta, pZvuk);
	if (Vysledek == WavOK)
	{
		s.Format ("%s.wav: %u Hz, %u kan., %u bit, %u.%01u s", pJmeno,
			  pZvuk->nPuvRate, pZvuk->nPuvKanaly, pZvuk->nPuvBity,
			  pZvuk->nVzorku / 44100, pZvuk->nVzorku % 44100 * 10 / 44100);
	}
	else
	{
		// chybejici zvuk nezastavi ostatni - jen ten jeden nebude hrat
		s.Format ("%s.wav: %s - nebude hrat", pJmeno, PopisChyby (Vysledek));
		if (Vysledek == WavPamet)
		{
			Chyba (ChybaPamet);
		}
	}
	m_Logger.Write (Zdroj, Vysledek == WavOK ? LogNotice : LogWarning, "%s", (const char *) s);
	if (m_ZvukStav.GetLength () > 0) m_ZvukStav.Append ("\n");
	m_ZvukStav.Append (s);
}

void CKernel::Run (void)
{
	m_Logger.Write (Zdroj, LogNotice, "SBusSound bare metal, " VERZE ", sestaveno " __DATE__ " " __TIME__);

	if (!m_EMMC.Initialize ())
	{
		m_Logger.Write (Zdroj, LogError, "SD karta nejde inicializovat");
		Chyba (ChybaKarta);
	}
	if (f_mount (&m_FileSystem, DRIVE, 1) != FR_OK)
	{
		m_Logger.Write (Zdroj, LogError, "FAT oddil nejde namontovat");
		Chyba (ChybaKarta);
	}

	// Mapovani zapisuje nastaveni.new a pak ho prejmenuje. Kdyby mezitim
	// vypadlo napajeni, dokoncit to ted.
	{
		FILINFO Info;
		if (f_stat (NASTAVENI, &Info) != FR_OK && f_stat (NASTAVENI_NOVE, &Info) == FR_OK)
		{
			f_rename (NASTAVENI_NOVE, NASTAVENI);
		}
	}

	// nastaveni.txt - co chybi nebo je spatne, nahradi se vychozi hodnotou
	boolean bNastOK = m_Ini.Nacti (NASTAVENI);
	char Opravy[1024];
	NactiNastaveni (m_Ini, bNastOK, &m_Nast, Opravy, sizeof Opravy);
	if (!bNastOK)
	{
		m_NastStav.Format ("nastaveni.txt: %s - jedu s vychozimi hodnotami",
				   m_Ini.PocetChyb () ? m_Ini.Chyby () : "soubor neni na karte");
	}
	else if (Opravy[0] != '\0')
	{
		m_NastStav.Format ("nastaveni.txt: nacteno, %u oprav: %s", m_Nast.nOprav, Opravy);
	}
	else
	{
		m_NastStav = "nastaveni.txt: nacteno bez chyb";
	}
	m_Logger.Write (Zdroj, bNastOK && m_Nast.nOprav == 0 ? LogNotice : LogWarning, "%s",
			(const char *) m_NastStav);
	m_bDiag = m_Nast.bLog;
	m_Mixer.Vystup (m_Nast.nHlasitost, m_Nast.Strana);
	MezePrepinace (m_Nast, &m_PrepMotor);
	MezePrepinace (m_Nast, &m_PrepKulomet);
	MezePrepinace (m_Nast, &m_PrepDelo);
	MezePrepinace (m_Nast, &m_PrepPozadi);
	// vez: zapne pri odchylce nad 6 % rozsahu paky, vypne pod 3,5 %
	// (stejne jako sound_engine2.py)
	{
		int nRozsahVez = m_Nast.nStredVez - m_Nast.nMin < m_Nast.nMax - m_Nast.nStredVez
			       ? m_Nast.nStredVez - m_Nast.nMin : m_Nast.nMax - m_Nast.nStredVez;
		if (nRozsahVez < 1) nRozsahVez = 1;
		m_PrepVez.Meze (nRozsahVez * 60 / 1000, nRozsahVez * 35 / 1000);
	}

	// zvuky
	TZvuk Start, Beh, Stop, Kulomet, Vez, Delo, Pozadi;
	NactiZvuk ("motor_start", &Start);
	NactiZvuk ("motor_run", &Beh);
	NactiZvuk ("motor_stop", &Stop);
	NactiZvuk ("machinegun", &Kulomet);
	NactiZvuk ("turret", &Vez);
	NactiZvuk ("cannon", &Delo);
	NactiZvuk ("background", &Pozadi);
	TZvukyMixeru Z = {Start.pData, Start.nVzorku, Beh.pData, Beh.nVzorku,
			  Stop.pData, Stop.nVzorku, Kulomet.pData, Kulomet.nVzorku,
			  Vez.pData, Vez.nVzorku, Delo.pData, Delo.nVzorku,
			  Pozadi.pData, Pozadi.nVzorku};
	unsigned nKopii = m_Mixer.Priprav (Z, m_Nast.Prev);
	if (Beh.pData != 0)
	{
		CString s; s.Format ("motor_run: %u kopii pro rychlosti 1,0 az %u.%02ux", nKopii,
				     (unsigned) m_Nast.Prev.fRateMax,
				     (unsigned) ((m_Nast.Prev.fRateMax - (unsigned) m_Nast.Prev.fRateMax) * 100 + 0.5));
		m_Logger.Write (Zdroj, LogNotice, "%s", (const char *) s);
		m_ZvukStav.Append ("\n"); m_ZvukStav.Append (s);
	}

	if (!SpustVystup ())
	{
		Chyba (ChybaZvuk);
	}
	if (!m_SBus.Initialize ())
	{
		m_Logger.Write (Zdroj, LogError, "seriovy port pro SBus nejde nastavit");
		Chyba (ChybaUART);
	}
	Udalost ("start, cekam na SBus");
	if (m_bDiag)
	{
		Udalost ("log = ano - zapisuji log.txt kazde 2 s");
	}
	if (m_Nast.bMapovat)
	{
		Mapuj (Z);		// nevraci se - na konci restart
	}

	boolean bRamcePred = FALSE, bFailsafePred = FALSE;
	TStavMotoru StavPred = MotorVypnuty;
	unsigned nRazeniPred = 0, nVystreluPred = 0;
	int nSmerPred = 1;
	TVstupy Pred = m_V;
	unsigned nLedCas = Ms (), nVypisCas = Ms (), nDiagCas = Ms ();
	unsigned nFailsafeOd = 0, nBezRamcuOd = 0;
	m_nRamcuCas = m_nLinkaCas = Ms ();
	boolean bLed = FALSE;

	while (1)
	{
		m_SBus.Poll ();
		unsigned nTed = Ms ();

		boolean bRamce = m_SBus.ChodiRamce ();
		boolean bSignal = m_SBus.MaSignal ();
		boolean bFailsafe = bRamce && (m_SBus.m_nPriznaky & SBUS_FLAG_FAILSAFE);

		if (bSignal)
		{
			const u16 *ch = m_SBus.m_Kanal;
			// plyn: mrtve pasmo kolem stredu, pak linearne do krajni polohy
			int d = ch[m_Nast.Kanal[FcePlyn] - 1] - m_Nast.nStredPlyn;
			int nRozsah = m_Nast.nStredPlyn - m_Nast.nMin < m_Nast.nMax - m_Nast.nStredPlyn
				    ? m_Nast.nStredPlyn - m_Nast.nMin : m_Nast.nMax - m_Nast.nStredPlyn;
			int nAbs = d < 0 ? -d : d;
			float fPlyn = 0.0f;
			if (nAbs > PLYN_MRTVA)
			{
				fPlyn = (float) (nAbs - PLYN_MRTVA) / (nRozsah - PLYN_MRTVA > 1 ? nRozsah - PLYN_MRTVA : 1);
				if (fPlyn > 1.0f) fPlyn = 1.0f;
				if (d < 0) fPlyn = -fPlyn;
			}
			m_V.fPlyn = fPlyn;
			m_V.bMotor = m_PrepMotor (ch[m_Nast.Kanal[FceMotor] - 1]);
			m_V.bKulomet = m_PrepKulomet (ch[m_Nast.Kanal[FceKulomet] - 1]);
			m_V.bDelo = m_PrepDelo (ch[m_Nast.Kanal[FceDelo] - 1]);
			m_V.bPozadi = m_PrepPozadi (ch[m_Nast.Kanal[FcePozadi] - 1]);
			int nVez = ch[m_Nast.Kanal[FceVez] - 1] - m_Nast.nStredVez;
			m_V.bVez = m_PrepVez (nVez < 0 ? -nVez : nVez);
			m_nHlasitost = Procenta (ch[m_Nast.Kanal[FceHlasitost] - 1], m_Nast);
			m_V.fHlasitost = m_nHlasitost / 100.0f;
			m_V.bSignal = TRUE;
		}
		else
		{
			// bez signalu vse ticho; motor korektne dobehne (motor_stop)
			m_PrepMotor.Vypni ();
			m_PrepKulomet.Vypni ();
			m_PrepDelo.Vypni ();
			m_PrepPozadi.Vypni ();
			m_PrepVez.Vypni ();
			m_V.bMotor = m_V.bKulomet = m_V.bDelo = m_V.bPozadi = m_V.bVez = FALSE;
			m_V.fPlyn = 0.0f;
			m_V.bSignal = FALSE;
		}
		m_Mixer.Vstupy (m_V);

		// udalosti prepinacu do logu
		const char *pDuvod = bSignal ? "" : " (bez signalu)";
		struct { boolean bNyni, bPred; const char *pNazev; } Zmeny[] =
		{
			{m_V.bKulomet, Pred.bKulomet, "kulomet"},
			{m_V.bVez, Pred.bVez, "vez"},
			{m_V.bDelo, Pred.bDelo, "prepinac dela"},
			{m_V.bPozadi, Pred.bPozadi, "pozadi"},
		};
		for (auto &Z : Zmeny)
		{
			if (Z.bNyni != Z.bPred)
			{
				CString s; s.Format ("%s %s%s", Z.pNazev, Z.bNyni ? "ZAP" : "VYP", Z.bNyni ? "" : pDuvod);
				Udalost (s);
			}
		}
		Pred = m_V;

		if (bRamce != bRamcePred)
		{
			if (!bRamce)
			{
				// ztlumeni: posledni ramec prisel pred 0,5 s
				m_nBezRamcu++;
				nBezRamcuOd = nTed - SBUS_TICHO_US / 1000;
				Vypadek ("SBus: zadne ramce uz 0,5 s - ZVUK ZTLUMEN");
			}
			else if (m_SBus.m_nRamcu > 1 && nBezRamcuOd != 0)
			{
				CString s; s.Format ("SBus ramce zase chodi, nechodily %u ms", nTed - nBezRamcuOd);
				Vypadek (s);
			}
			else
			{
				Udalost ("SBus ramce chodi");
			}
		}
		if (bFailsafe && !bFailsafePred)
		{
			m_nFailsafu++;
			nFailsafeOd = nTed;
			Vypadek ("FAILSAFE: prijimac hlasi ztratu vysilace - ZVUK ZTLUMEN");
		}
		else if (!bFailsafe && bFailsafePred && bRamce)
		{
			// jen kdyz ramce chodi dal - kdyz prestaly, signal se nevratil
			CString s; s.Format ("failsafe skoncil po %u ms, signal obnoven", nTed - nFailsafeOd);
			Vypadek (s);
		}
		SledujVypadky (nTed);
		bRamcePred = bRamce;
		bFailsafePred = bFailsafe;

		TStavMotoru Stav = m_Mixer.m_Stav;
		if (Stav != StavPred)
		{
			CString s; s.Format ("motor %s -> %s", NazevStavu (StavPred), NazevStavu (Stav));
			Udalost (s);
			StavPred = Stav;
		}
		unsigned nVystrelu = m_Mixer.m_nVystrelu;
		if (nVystrelu != nVystreluPred)
		{
			Udalost ("delo VYSTREL");
			nVystreluPred = nVystrelu;
		}
		int nSmer = m_Mixer.m_Box.m_nSmer;
		if (nSmer != nSmerPred)
		{
			Udalost (nSmer < 0 ? "smer: VZAD" : "smer: vpred");
			nSmerPred = nSmer;
		}
		unsigned nRazeni = m_Mixer.m_nRazeni;
		if (nRazeni != nRazeniPred)
		{
			CString s; s.Format ("razeni -> %u. stupen (rychlost %u %%)", m_Mixer.m_Box.m_nStupen,
					     (unsigned) (m_Mixer.m_Box.m_fRychlost * 100 + 0.5));
			Udalost (s);
			nRazeniPred = nRazeni;
		}

		// LED: s signalem kratce jednou za sekundu, bez nej rychle blikani
		boolean bChci = bSignal ? (nTed - nLedCas) % 1000 < 80
					: ((nTed - nLedCas) / 125) % 2 == 0;
		if (bChci != bLed) { bLed = bChci; bLed ? m_ActLED.On () : m_ActLED.Off (); }

		if (nTed - nVypisCas >= 1000)
		{
			nVypisCas = nTed;
			m_Logger.Write (Zdroj, LogNotice, "%s", (const char *) Prehled ());
		}
		if (m_bDiag && nTed - nDiagCas >= 2000)
		{
			nDiagCas = nTed;
			ZapisDiag ();
			// ramce z doby zapisu se vyberou najednou - mezeru nemerit
			m_SBus.m_Rozbor.Preskoc ();
		}

		m_Timer.MsDelay (1);
	}
}

// ---------------------------------------------------------------------------
// Mapovani kanalu ([diagnostika] mapovat = ano)
//
// Postupne dokola hraje zvuk kazde funkce, dokud uzivatel jasne nepohne
// nejakym ovladacem (aspon 30 % rozsahu kalibrace, 3 ramce po sobe). Kanal
// s nejvetsi vychylkou se priradi funkci, nasleduji 3 s ticha a dalsi zvuk.
// Prvni je hlasitost (stoupajici tony): jeji kanal pak hned ridi hlasitost
// dalsich zvuku a uz se na zadnou dalsi funkci nepriradi. Zaroven je to
// kontrola, ze SBus vubec chodi.
// Kdyz vyjde jeden kanal u dvou funkci, zaznou tri hluboke tony a cele se to
// opakuje. Jinak se kanaly zapisou do nastaveni.txt, mapovani se vypne
// (mapovat = ne) a Raspberry se restartuje.
//
// Ovladac, kterym uz se v tomto kole pohnulo, se smi vratit do vychozi
// polohy - pocita se jen pohyb pryc od polohy na zacatku kola.
// ---------------------------------------------------------------------------

#define MAP_PRAH_PROC	30	// jasny pohyb: aspon tolik % rozsahu min..max
#define MAP_RAMCU	3	// ...po tolik ramcu po sobe (cca 30 ms)
#define MAP_TICHO_MS	3000	// ticho po prirazeni kanalu

void CKernel::MapCekej (unsigned nMs)
{
	unsigned nZac = Ms ();
	static unsigned nDiagCas = 0;
	do
	{
		m_SBus.Poll ();
		unsigned nTed = Ms ();
		SledujVypadky (nTed);
		// LED: dve kratka bliknuti za sekundu = mapovani
		unsigned f = nTed % 1000;
		boolean bLed = f < 60 || (f >= 200 && f < 260);
		bLed ? m_ActLED.On () : m_ActLED.Off ();
		// hlasitost uz namapovana: ridi hlasitost mapovani
		if (m_nMapHlas >= 0 && m_SBus.MaSignal ())
		{
			unsigned nHlas = Procenta (m_SBus.m_Kanal[m_nMapHlas], m_Nast);
			if (nHlas < 10) nHlas = 10;	// uplne ztlumene zvuky by nebylo slyset
			if (nHlas != m_nHlasitost)
			{
				m_nHlasitost = nHlas;
				m_V.fHlasitost = nHlas / 100.0f;
				m_Mixer.Vstupy (m_V);
			}
		}
		if (m_bDiag && nTed - nDiagCas >= 2000)
		{
			nDiagCas = nTed;
			ZapisDiag ();
			m_SBus.m_Rozbor.Preskoc ();
		}
		m_Timer.MsDelay (1);
	}
	while (Ms () - nZac < nMs);
}

void CKernel::Mapuj (const TZvukyMixeru &Z)
{
	// poradi a zvuky; hlasitost prvni (nema zvuk - hraji stoupajici tony),
	// motor_stop se vynechava (stejny kanal jako start), motor_run = plyn
	struct TKrok { TFunkce Fce; const s16 *pData; unsigned nVzorku; const char *pZvuk; };
	TKrok Kroky[] =
	{
		{FceHlasitost,	0,		0,		"tony"},
		{FceMotor,	Z.pStart,	Z.nStart,	"motor_start"},
		{FcePlyn,	Z.pBeh,		Z.nBeh,		"motor_run"},
		{FceVez,	Z.pVez,		Z.nVez,		"turret"},
		{FceKulomet,	Z.pKulomet,	Z.nKulomet,	"machinegun"},
		{FceDelo,	Z.pDelo,	Z.nDelo,	"cannon"},
		{FcePozadi,	Z.pPozadi,	Z.nPozadi,	"background"},
	};
	const unsigned KROKU = sizeof Kroky / sizeof Kroky[0];

	// tony: hlasitost = stoupajici stupnice; chybejici zvuk = tolik pipnuti,
	// kolikaty je v poradi; chyba (stejny kanal dvakrat) = tri hluboke tony
	static const unsigned Stupnice[] = {440, 554, 659, 880};
	static const unsigned Pip[] = {880, 880, 880, 880, 880, 880, 880};
	static const unsigned Chyba3[] = {220, 220, 220};
	for (unsigned k = 0; k < KROKU; k++)
	{
		if (Kroky[k].pData != 0) continue;
		if (Kroky[k].Fce == FceHlasitost)
			Kroky[k].pData = CMixer::VyrobTony (Stupnice, 4, 140, 40, 700, &Kroky[k].nVzorku);
		else
			Kroky[k].pData = CMixer::VyrobTony (Pip, k + 1, 150, 150, 1200, &Kroky[k].nVzorku);
	}
	unsigned nChyba = 0;
	const s16 *pChyba = CMixer::VyrobTony (Chyba3, 3, 400, 200, 0, &nChyba);

	memset (&m_V, 0, sizeof m_V);
	m_V.fHlasitost = 1.0f;		// kanal hlasitosti jeste nezname - plna (x celkova)
	m_nHlasitost = 100;
	m_Mixer.Vstupy (m_V);
	m_Mixer.Mapovani (0, 0);

	int nPrah = (m_Nast.nMax - m_Nast.nMin) * MAP_PRAH_PROC / 100;
	if (nPrah < 50) nPrah = 50;
	CString s;
	s.Format ("MAPOVANI KANALU: jasny pohyb = aspon %d (%u %% rozsahu %d..%d)",
		  nPrah, MAP_PRAH_PROC, m_Nast.nMin, m_Nast.nMax);
	Udalost (s);
	auto Abs = [] (int x) { return x < 0 ? -x : x; };

	for (unsigned nKolo = 1; ; nKolo++)
	{
		// vychozi poloha vsech ovladacu na zacatku kola
		while (!m_SBus.MaSignal ()) MapCekej (10);
		u16 Klid[SBUS_KANALU];
		memcpy (Klid, m_SBus.m_Kanal, sizeof Klid);
		s.Format ("mapovani: kolo %u", nKolo);
		Udalost (s);

		unsigned Kanal[FUNKCI];
		for (unsigned i = 0; i < FUNKCI; i++) Kanal[i] = m_Nast.Kanal[i];

		for (unsigned k = 0; k < KROKU; k++)
		{
			const TKrok &K = Kroky[k];
			m_Mixer.Mapovani (K.pData, K.nVzorku);
			s.Format ("mapovani: hraje %s (%s), cekam na pohyb", K.pZvuk, NazevFunkce[K.Fce]);
			Udalost (s);

			u16 Zaklad[SBUS_KANALU];
			boolean bZaklad = FALSE;
			unsigned Nad[SBUS_KANALU] = {0};
			unsigned nRamec = m_SBus.m_nRamcu;
			int nVybrany = -1;
			while (nVybrany < 0)
			{
				MapCekej (1);
				if (!m_SBus.MaSignal ())
				{
					for (unsigned c = 0; c < SBUS_KANALU; c++) Nad[c] = 0;
					continue;
				}
				if (m_SBus.m_nRamcu == nRamec) continue;
				nRamec = m_SBus.m_nRamcu;
				const u16 *ch = m_SBus.m_Kanal;
				if (!bZaklad)
				{
					memcpy (Zaklad, ch, sizeof Zaklad);	// poloha pri zacatku zvuku
					bZaklad = TRUE;
					continue;
				}
				int nNejvic = 0;
				for (unsigned c = 0; c < SBUS_KANALU; c++)
				{
					// kanal hlasitosti ridi hlasitost - jinam se nepriradi
					if (K.Fce != FceHlasitost && (int) c == m_nMapHlas) { Nad[c] = 0; continue; }
					int d = Abs (ch[c] - Zaklad[c]);
					// pohyb pryc od zakladu i od polohy na zacatku kola
					if (d >= nPrah && Abs (ch[c] - Klid[c]) >= nPrah) Nad[c]++;
					else Nad[c] = 0;
					if (Nad[c] >= MAP_RAMCU && d > nNejvic) { nNejvic = d; nVybrany = c; }
				}
			}
			m_Mixer.Mapovani (0, 0);
			Kanal[K.Fce] = nVybrany + 1;
			if (K.Fce == FceHlasitost) m_nMapHlas = nVybrany;
			s.Format ("mapovani: %s -> CH%d (%u -> %u)", NazevFunkce[K.Fce], nVybrany + 1,
				  Zaklad[nVybrany], m_SBus.m_Kanal[nVybrany]);
			Udalost (s);
			MapCekej (MAP_TICHO_MS);
		}

		// kontrola duplicit
		CString Dupl;
		for (unsigned i = 0; i < FUNKCI; i++)
		{
			for (unsigned j = i + 1; j < FUNKCI; j++)
			{
				if (Kanal[i] == Kanal[j])
				{
					CString t; t.Format ("%sCH%u: %s a %s", Dupl.GetLength () ? "; " : "",
							     Kanal[i], NazevFunkce[i], NazevFunkce[j]);
					Dupl.Append (t);
				}
			}
		}
		if (Dupl.GetLength () == 0)
		{
			s.Format ("mapovani hotovo: motor CH%u, plyn CH%u, vez CH%u, kulomet CH%u, delo CH%u, pozadi CH%u, hlasitost CH%u",
				  Kanal[FceMotor], Kanal[FcePlyn], Kanal[FceVez], Kanal[FceKulomet],
				  Kanal[FceDelo], Kanal[FcePozadi], Kanal[FceHlasitost]);
			Udalost (s);
			if (!UlozMapovani (Kanal))
			{
				Udalost ("mapovani: nastaveni.txt nejde zapsat");
				if (m_bDiag) ZapisDiag ();
				Chyba (ChybaKarta);
			}
			Udalost ("mapovani: zapsano do nastaveni.txt (mapovat = ne), restart");
			if (m_bDiag) ZapisDiag ();
			f_mount (0, DRIVE, 0);
			m_Timer.MsDelay (300);
			reboot ();
		}

		s.Format ("mapovani: stejny kanal u vic funkci (%s) - znovu od zacatku", (const char *) Dupl);
		Udalost (s);
		if (pChyba != 0)
		{
			m_Mixer.Mapovani (pChyba, nChyba);
			MapCekej (nChyba * 1000 / 44100 - 30);	// jednou cele
			m_Mixer.Mapovani (0, 0);
		}
		MapCekej (MAP_TICHO_MS);
	}
}

boolean CKernel::UlozMapovani (const unsigned *pKanal)
{
	FIL Soubor;
	if (f_open (&Soubor, NASTAVENI, FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		// soubor neni (jelo se s vychozimi hodnotami) - vyrobit novy
		char Nic[1] = "";
		static char Novy[1024];
		unsigned n = UpravNastaveni (Nic, 0, pKanal, Novy, sizeof Novy);
		UINT nZaps;
		if (n == 0 || f_open (&Soubor, NASTAVENI, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return FALSE;
		boolean bOK = f_write (&Soubor, Novy, n, &nZaps) == FR_OK && nZaps == n;
		return f_close (&Soubor) == FR_OK && bOK;
	}
	unsigned nVel = f_size (&Soubor);
	char *pIn = new char[nVel + 1];
	unsigned nMax = nVel + 1024;
	char *pOut = new char[nMax];
	UINT nPrec = 0;
	boolean bOK = pIn != 0 && pOut != 0 && f_read (&Soubor, pIn, nVel, &nPrec) == FR_OK && nPrec == nVel;
	f_close (&Soubor);
	unsigned n = bOK ? UpravNastaveni (pIn, nVel, pKanal, pOut, nMax) : 0;
	bOK = n > 0;
	// novy soubor vedle, pak vymenit - vypadek napajeni nenecha pul souboru
	if (bOK && f_open (&Soubor, NASTAVENI_NOVE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		UINT nZaps = 0;
		bOK = f_write (&Soubor, pOut, n, &nZaps) == FR_OK && nZaps == n;
		bOK = f_close (&Soubor) == FR_OK && bOK;
		bOK = bOK && f_unlink (NASTAVENI) == FR_OK && f_rename (NASTAVENI_NOVE, NASTAVENI) == FR_OK;
	}
	else
	{
		bOK = FALSE;
	}
	delete [] pIn;
	delete [] pOut;
	return bOK;
}

CString CKernel::Prehled (void)
{
	CString s, t;
	boolean bRamce = m_SBus.ChodiRamce ();
	s.Format ("SBus: %s, ramcu %u, zahozeno bajtu %u, chyby parita %u ramec %u preteceni %u break %u",
		  !bRamce ? "ZADNE RAMCE" : (m_SBus.m_nPriznaky & SBUS_FLAG_FAILSAFE) ? "FAILSAFE" : "signal OK",
		  m_SBus.m_nRamcu, m_SBus.m_nZahozeno, m_SBus.m_nChybParita, m_SBus.m_nChybRamec,
		  m_SBus.m_nChybPreteceni, m_SBus.m_nChybBreak);
	const CSBusRozbor &R = m_SBus.m_Rozbor;
	t.Format ("\n  vypadky od zapnuti: ramcu/s %u, nejdelsi mezera mezi ramci %u ms, mezer nad %u ms %u | "
		  "prijimac ztracene ramce z vysilace %u (%u epizod, nejdelsi %u ms) | odskoku kanalu %u | "
		  "ramcu s chybou linky %u | failsafe %u, ztlumeno bez ramcu %u",
		  m_nRamcuZaS, R.m_nMaxMezeraUs / 1000, SBUS_MEZERA_US / 1000, R.m_nMezer,
		  R.m_nZtracenychRamcu, R.m_nZtrat, R.m_nMaxZtrataUs / 1000, R.m_nSkoku,
		  m_SBus.m_nPodezrelych, m_nFailsafu, m_nBezRamcu);
	s.Append (t);
	const CPrevodovka &B = m_Mixer.m_Box;
	int nPlyn = (int) (m_V.fPlyn * 100 + (m_V.fPlyn < 0 ? -0.5f : 0.5f));
	unsigned nRate = (unsigned) (B.Rate () * 100 + 0.5);
	t.Format ("\n  motor %s, stupen %u%s, rychlost %u %%, otacky %u %%, prehravani %u.%02ux, plyn %d %%, razeni %u",
		  NazevStavu (m_Mixer.m_Stav), B.m_nStupen, B.m_nSmer < 0 ? " (vzad)" : "",
		  (unsigned) (B.m_fRychlost * 100 + 0.5), (unsigned) (B.m_fOtacky * 100 + 0.5),
		  nRate / 100, nRate % 100, nPlyn, m_Mixer.m_nRazeni);
	s.Append (t);
	{
		// spicka v % plneho rozsahu; ztlumeni limiterem v % (100 = vubec)
		unsigned nSpicka = (unsigned) (m_Mixer.m_fSpicka * 100 + 0.5f);
		unsigned nZes = (unsigned) (m_Mixer.m_fMinZes * 100 + 0.5f);
		t.Format ("\n  smes: nejvyssi spicka %u %% plneho rozsahu, limiter zasahl ve %u vzorcich, "
			  "nejvic ztlumil na %u %%, urizlo %u vzorku | vypocet bloku nejvys %u us z 23220 us",
			  nSpicka, m_Mixer.m_nPretizeno, nZes, m_Mixer.m_nOrezano, m_Mixer.m_nMaxUs);
		s.Append (t);
		t.Format ("\n  casovani zvuku: bloku %u, nejdelsi rozestup %u us (ma byt 23220), "
			  "na hrane %u, PODTECENI (prasknuti) %u | nejdelsi zapis log.txt %u ms",
			  m_Mixer.m_nBloku, m_Mixer.m_nMaxRozestup, m_Mixer.m_nPozde,
			  m_Mixer.m_nPodteceni, m_nMaxZapisMs);
		s.Append (t);
	}
	if (m_SBus.m_nRamcu > 0)
	{
		t.Format ("\n  priznaky 0x%02X, koncovy bajt 0x%02X | kulomet %s, vez %s, delo %s (vystrelu %u), pozadi %s | hlasitost %u %%\n  kanaly:",
			  m_SBus.m_nPriznaky, m_SBus.m_nKoncovy, m_V.bKulomet ? "ZAP" : "vyp",
			  m_V.bVez ? "ZAP" : "vyp", m_V.bDelo ? "nahore" : "dole", m_Mixer.m_nVystrelu,
			  m_V.bPozadi ? "ZAP" : "vyp", m_nHlasitost);
		s.Append (t);
		for (unsigned i = 0; i < SBUS_KANALU; i++)
		{
			t.Format (" %u:%u", i + 1, m_SBus.m_Kanal[i]);
			s.Append (t);
		}
	}
	return s;
}

void CKernel::Udalost (const char *pText)
{
	m_Logger.Write (Zdroj, LogNotice, "%s", pText);
	CString s;
	unsigned nMs = Ms ();
	s.Format ("%5u.%01u s  %s", nMs / 1000, nMs % 1000 / 100, pText);
	if (m_nUdalosti == UDALOSTI)
	{
		for (unsigned i = 1; i < UDALOSTI; i++) m_Udalost[i - 1] = m_Udalost[i];
		m_nUdalosti--;
	}
	m_Udalost[m_nUdalosti++] = s;
}

void CKernel::Vypadek (const char *pText)
{
	m_Logger.Write (Zdroj, LogWarning, "%s", pText);
	CString s;
	unsigned nMs = Ms ();
	s.Format ("%5u.%03u s  %s", nMs / 1000, nMs % 1000, pText);
	if (m_nVypadku == VYPADKU)
	{
		for (unsigned i = 1; i < VYPADKU; i++) m_Vypadek[i - 1] = m_Vypadek[i];
		m_nVypadku--;
	}
	m_Vypadek[m_nVypadku++] = s;
}

void CKernel::SledujVypadky (unsigned nTed)
{
	CString s;
	TSBusUdalost U;
	while (m_SBus.m_Rozbor.Vyber (&U))
	{
		switch (U.Typ)
		{
		case JevMezera:
			s.Format ("SBus: mezera %u ms bez platneho ramce", (U.nTrvaniUs + 500) / 1000);
			break;
		case JevZtraceno:
			// jednotlive ztracene ramce jsou v RC bezne - jen do souhrnu
			if (U.nPocet < 3) continue;
			s.Format ("prijimac: ztracene ramce z vysilace %u ms (%u ramcu), hodnoty drzi",
				  (U.nTrvaniUs + 500) / 1000, U.nPocet);
			break;
		case JevSkok:
		{
			// ktere funkci kanal patri
			CString f;
			for (unsigned i = 0; i < FUNKCI; i++)
			{
				if (m_Nast.Kanal[i] == U.nKanal + 1)
				{
					f.Append (f.GetLength () ? ", " : " (");
					f.Append (NazevFunkce[i]);
				}
			}
			if (f.GetLength ()) f.Append (")");
			s.Format ("odskok CH%u%s: %u -> %u -> %u na %u ramec%s - poskozena data",
				  U.nKanal + 1, (const char *) f, U.nZ, U.nNa, U.nPo, U.nDelka, U.nDelka == 1 ? "" : "e");
			break;
		}
		}
		Vypadek (s);
	}

	// ramcu za sekundu
	if (nTed - m_nRamcuCas >= 1000)
	{
		m_nRamcuZaS = m_SBus.m_nRamcu - m_nRamcuPred;
		m_nRamcuPred = m_SBus.m_nRamcu;
		m_nRamcuCas = nTed;
	}

	// chyby linky souhrnne za sekundu (jinak by zaplavily historii)
	if (nTed - m_nLinkaCas >= 1000)
	{
		m_nLinkaCas = nTed;
		unsigned Ted[6] = {m_SBus.m_nChybParita, m_SBus.m_nChybRamec, m_SBus.m_nChybPreteceni,
				   m_SBus.m_nChybBreak, m_SBus.m_nZahozeno, m_SBus.m_nPodezrelych};
		unsigned d[6], nSoucet = 0;
		for (unsigned i = 0; i < 6; i++) { d[i] = Ted[i] - m_LinkaPred[i]; nSoucet += d[i]; m_LinkaPred[i] = Ted[i]; }
		// zahozene bajty pri prvnim zachyceni zacatku ramce nejsou chyba
		if (nSoucet > 0 && m_SBus.m_nRamcu > 100)
		{
			s.Format ("linka SBus za 1 s: chyby parita %u, ramec %u, preteceni %u, break %u, "
				  "zahozeno bajtu %u, prijato ramcu s chybnym bajtem %u",
				  d[0], d[1], d[2], d[3], d[4], d[5]);
			Vypadek (s);
		}
	}
}

void CKernel::ZapisDiag (void)
{
	CString Text, s;
	unsigned nMs = Ms ();
	const TParPrevodovky &P = m_Nast.Prev;
	Text.Format ("SBusSound bare metal, " VERZE ", sestaveno " __DATE__ " " __TIME__ "\n"
		     "bezim %u s\n%s\n%s\n"
		     "vystup %s, reproduktor %s, hlasitost %u %%\n"
		     "kanaly: vez CH%u, motor CH%u, plyn CH%u, kulomet CH%u, delo CH%u, pozadi CH%u, hlasitost CH%u\n"
		     "kalibrace %d/%d/%d, stred plynu %d, veze %d, prepinace zapinaji nad %d\n",
		     nMs / 1000, (const char *) m_NastStav, (const char *) m_ZvukStav,
		     NazevZarizeni (m_Nast.Zarizeni), NazevStrany (m_Nast.Strana), m_Nast.nHlasitost,
		     m_Nast.Kanal[FceVez], m_Nast.Kanal[FceMotor], m_Nast.Kanal[FcePlyn],
		     m_Nast.Kanal[FceKulomet], m_Nast.Kanal[FceDelo], m_Nast.Kanal[FcePozadi],
		     m_Nast.Kanal[FceHlasitost],
		     m_Nast.nMin, m_Nast.nStred, m_Nast.nMax, m_Nast.nStredPlyn, m_Nast.nStredVez,
		     PrahPrepinace (m_Nast));
	s.Format ("prevodovka: %d stupne, pomer %u.%02u, rozjezd %u.%01u s, dobeh %u.%01u s, "
		  "navrat %u ms, propad %u %%, zpatecka %u %%, max %u.%02ux\n\n",
		  P.nStupnu, (unsigned) P.fPrevod, (unsigned) ((P.fPrevod - (unsigned) P.fPrevod) * 100 + 0.5),
		  (unsigned) P.fRozjezd, (unsigned) ((P.fRozjezd - (unsigned) P.fRozjezd) * 10 + 0.5),
		  (unsigned) P.fDobeh, (unsigned) ((P.fDobeh - (unsigned) P.fDobeh) * 10 + 0.5),
		  (unsigned) P.fNavrat, (unsigned) (P.fPropad * 100 + 0.5), (unsigned) (P.fZpatecka * 100 + 0.5),
		  (unsigned) P.fRateMax, (unsigned) ((P.fRateMax - (unsigned) P.fRateMax) * 100 + 0.5));
	Text.Append (s);
	Text.Append (Prehled ());
	s.Format ("\n\nvypadky signalu (poslednich %u, cas od zapnuti):\n", (unsigned) VYPADKU);
	Text.Append (s);
	if (m_nVypadku == 0)
	{
		Text.Append ("  zadne\n");
	}
	for (unsigned i = 0; i < m_nVypadku; i++)
	{
		Text.Append (m_Vypadek[i]);
		Text.Append ("\n");
	}
	Text.Append ("\nposledni udalosti:\n");
	for (unsigned i = 0; i < m_nUdalosti; i++)
	{
		Text.Append (m_Udalost[i]);
		Text.Append ("\n");
	}

	unsigned nZac = Ms ();
	FIL Soubor;
	if (f_open (&Soubor, LOG, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		unsigned n;
		f_write (&Soubor, (const char *) Text, Text.GetLength (), &n);
		f_close (&Soubor);
	}
	unsigned nTrvalo = Ms () - nZac;
	if (nTrvalo > m_nMaxZapisMs) m_nMaxZapisMs = nTrvalo;
}

boolean CKernel::SpustVystup (void)
{
	if (m_pVystup == 0)
	{
		if (m_Nast.Zarizeni == ZarizeniJack)
		{
			m_pVystup = new CVystupPWM (&m_Interrupt, &m_Mixer);
		}
		else
		{
			m_pVystup = new CVystupI2S (&m_Interrupt, &m_Mixer);
		}
	}
	m_bZvukBezi = m_pVystup->Start ();
	if (m_bZvukBezi)
	{
		m_Logger.Write (Zdroj, LogNotice, "zvuk jde do %s",
				m_Nast.Zarizeni == ZarizeniJack ? "3,5mm jacku" : "I2S (GY-PCM5102)");
	}
	else
	{
		m_Logger.Write (Zdroj, LogError, "zvukovy vystup %s nejde spustit",
				NazevZarizeni (m_Nast.Zarizeni));
	}
	return m_bZvukBezi;
}

void CKernel::Chyba (TKodChyby Kod)
{
	m_Logger.Write (Zdroj, LogError, "CHYBA %u - zastaveno", (unsigned) Kod);

	// Kdyz to jde, ohlasi chybu i zvukem - LED v tanku nemusi byt videt.
	if (Kod != ChybaZvuk)
	{
		m_Mixer.Pipani (Kod);
		if (!m_bZvukBezi)
		{
			SpustVystup ();
		}
	}

	// Jedno DLOUHE bliknuti a pak N kratkych. Dlouhe bliknuti odlisuje nase
	// chyby od chyb firmwaru Raspberry (ten blika jen kratce: 4x = chybi
	// start.elf, 7x = chybi kernel).
	while (1)
	{
		m_ActLED.On ();
		m_Timer.MsDelay (1000);
		m_ActLED.Off ();
		m_Timer.MsDelay (500);
		m_ActLED.Blink (Kod, 150, 250);
		m_Timer.MsDelay (1500);
	}
}
