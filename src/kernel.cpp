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
// Kdyz je na karte soubor diag.txt, zapisuje se kazde 2 s na kartu log.txt.
//
#include "kernel.h"
#include <circle/util.h>

#define VERZE		"krok 5c"
#define DRIVE		"SD:"
#define NASTAVENI	DRIVE "/nastaveni.txt"
#define DIAG		DRIVE "/diag.txt"
#define LOG		DRIVE "/log.txt"

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
	m_nUdalosti (0)
{
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

	FILINFO Info;
	m_bDiag = f_stat (DIAG, &Info) == FR_OK;

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
		Udalost ("diag.txt nalezen - zapisuji log.txt kazde 2 s");
	}

	boolean bRamcePred = FALSE, bFailsafePred = FALSE;
	TStavMotoru StavPred = MotorVypnuty;
	unsigned nRazeniPred = 0, nVystreluPred = 0;
	int nSmerPred = 1;
	TVstupy Pred = m_V;
	unsigned nLedCas = Ms (), nVypisCas = Ms (), nDiagCas = Ms ();
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
			Udalost (bRamce ? "SBus ramce chodi" : "SBus: zadne ramce uz 0,5 s");
		}
		if (bFailsafe && !bFailsafePred)
		{
			Udalost ("FAILSAFE: prijimac hlasi ztratu vysilace");
		}
		else if (!bFailsafe && bFailsafePred && bRamce)
		{
			// jen kdyz ramce chodi dal - kdyz prestaly, signal se nevratil
			Udalost ("failsafe skoncil, signal obnoven");
		}
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
		}

		m_Timer.MsDelay (1);
	}
}

CString CKernel::Prehled (void)
{
	CString s, t;
	boolean bRamce = m_SBus.ChodiRamce ();
	s.Format ("SBus: %s, ramcu %u, zahozeno bajtu %u, chyby parita %u ramec %u preteceni %u break %u",
		  !bRamce ? "ZADNE RAMCE" : (m_SBus.m_nPriznaky & SBUS_FLAG_FAILSAFE) ? "FAILSAFE" : "signal OK",
		  m_SBus.m_nRamcu, m_SBus.m_nZahozeno, m_SBus.m_nChybParita, m_SBus.m_nChybRamec,
		  m_SBus.m_nChybPreteceni, m_SBus.m_nChybBreak);
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
	Text.Append ("\n\nposledni udalosti:\n");
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
