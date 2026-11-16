# SBusSound – bare metal

Zvukový modul pro RC tank na **Raspberry Pi 3B+ bez operačního systému**.
Čte SBus z přijímače a přehrává motor s převodovkou, kulomet, dělo, věž
a zvuky okolí. Postaveno na knihovně [Circle](https://github.com/rsta2/circle).

## Hotový obsah karty

Po každém pushi ho GitHub sestaví sám: **Actions → poslední běh → Artifacts →
SBusSound_karta**. Obsah ZIPu nakopíruj do kořene SD karty (FAT32, schéma MBR)
a do složky `sounds/` přidej zvuky:

    motor_start.wav  motor_run.wav  motor_stop.wav
    machinegun.wav   turret.wav     cannon.wav     background.wav

Vše ostatní se nastavuje v `nastaveni.txt` na kartě (výstup, kanály vysílače,
kalibrace, převodovka). Když je na kartě soubor `diag.txt`, zapisuje se každé
2 s `log.txt` – jen pro ladění, v běžném provozu ho na kartě nenechávej.

## Sestavení u sebe

    git clone --recursive https://github.com/Luboshort/sbussound-baremetal.git
    cd sbussound-baremetal
    ./sestav.sh          # potřebuje arm-none-eabi-gcc v PATH

Výsledek je ve složce `vystup/karta/`.

## Struktura

    src/        zdrojový kód (C++)
    karta/      config.txt, vzorové nastaveni.txt
    circle/     knihovna Circle (git submodule, pevná verze)
    sestav.sh   sestavení kernelu a složení obsahu karty

## Úprava nastavení a zvuků

Na kartě je `nastaveni.html`. Otevři ho v Chromu nebo Edge přímo z karty,
klikni na **Otevřít SD kartu**, vyber kořen karty, uprav kanály, výstup,
kalibraci, převodovku (se simulátorem) nebo vyber nové zvuky v libovolném
běžném formátu a klikni na **Uložit na kartu**. V Safari se změny místo
toho stáhnou jako ZIP, jehož obsah se nakopíruje do kořene karty.
