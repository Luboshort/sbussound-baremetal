[Česky](README.md) | **English**

# SBusSound – bare metal

Sound module for an RC tank on a **Raspberry Pi 3B+ without an operating
system**. It reads SBus from the receiver and plays the engine with a gearbox,
turret, machine gun, cannon and ambient sounds. It is ready a few seconds after
power-up, there is nothing to install, and cutting the power cannot damage the
card. Built on the [Circle](https://github.com/rsta2/circle) library.

## Ready-made card contents

GitHub builds them after every push: **Actions → latest run → Artifacts →
SBusSound_karta**. Copy the contents of the ZIP to the root of the SD card
(FAT32, MBR partition scheme) and you are done – a set of tank sounds is
included in the `sounds/` folder. Replace any of them with your own under the
same name:

| file | when it plays |
|---|---|
| `motor_start.wav` | once when the engine is switched on |
| `motor_run.wav` | looped while driving, playback rate follows the revs |
| `motor_stop.wav` | once when the engine is switched off |
| `turret.wav` | looped while the turret stick is off-centre |
| `machinegun.wav` | looped while the switch is up |
| `cannon.wav` | once on each flick of the switch |
| `background.wav` | looped while the switch is up |

Sounds can be WAV 8/16/24/32 bit or float, mono or stereo, at any sample rate –
they are converted to 44100 Hz mono when loaded. A missing sound is simply not
played. The easiest way to change them is the `nastaveni.html` page.

**Updating** = replace `kernel8-32.img` on the card with the new one. Settings
and sounds stay.

## Settings

Everything is set in `nastaveni.txt` on the card: output, transmitter
channels, calibration and gearbox. The easier way is the **`nastaveni.html`**
page, which is on the card as well (CZ/EN switch at the top right):

1. Put the card into your computer and open `nastaveni.html` in Chrome or Edge.
2. **Open SD card** → choose the root of the card.
3. Set the channels, choose or edit the sounds (any common format – MP3, M4A,
   FLAC, WAV…), try the gearbox in the engine simulator.
4. **Save to card** writes `nastaveni.txt` and the new sounds straight to the
   card.

The page works in Safari too, but instead of writing to the card it downloads
a ZIP whose contents you copy to the card.

The **Tank / Truck** switch at the top changes the look of the page and the
function names: for a truck the turret becomes **Direction (turn signal)**, the
machine gun **Horn 1** and the cannon **Horn 2**. The engine and
`nastaveni.txt` stay the same.

### Sound sets

**Save set** stores the current sounds in a subfolder `sounds/<set name>/`,
**Load set** offers these subfolders and copies the chosen set's sounds into
`sounds/` in place of the current ones – the engine always plays only the files
directly in `sounds/`. The card comes with the **King Tiger II** set holding
the same sounds as `sounds/`. The page's choices (Tank/Truck, language, last
set) are kept on the card in `nastaveni_stranky.json`.

The setting names and values in `nastaveni.txt` are Czech (the engine reads
them); the comments are written in the language the page is switched to.

| setting | meaning |
|---|---|
| `[vystup] zarizeni` | output: `i2s` (PCM5102 module) or `jack` |
| `[vystup] reproduktor` | speaker channel: `oba` (both), `levy` (left), `pravy` (right) |
| `[vystup] hlasitost` | master volume, 0–100 % |
| `[kanaly]` | channel for `motor` (engine), `plyn` (throttle), `vez` (turret), `kulomet` (machine gun), `delo` (cannon), `pozadi` (ambient), `hlasitost` (volume) |
| `[kalibrace]` | `min`, `stred` (centre), `max`; optional `stred_plyn`, `stred_vez` |
| `[prevodovka]` | gearbox: `stupnu` (gears), `pomer` (ratio), `rozjezd`, `dobeh`, `navrat`, `propad`, `zpatecka` (reverse), `max_rychlost` |
| `[diagnostika] mapovat` | channel mapping at the next power-up: `ano` (yes) or `ne` (no) |
| `[diagnostika] log` | write `log.txt`: `ano` (yes) or `ne` (no) |

## Wiring

A detailed description (in Czech) is also at
[3dprintedtanks.com/zvukovy-modul](https://www.3dprintedtanks.com/zvukovy-modul/).

### Basic version – sound from the Raspberry Pi jack

[![Wiring](docs/zapojeni.jpg)](docs/zapojeni.pdf)

### DAC version – better and louder sound

[![Wiring with DAC](docs/zapojeni_dac.jpg)](docs/zapojeni_dac.pdf)

Click an image to open the full-resolution PDF.

| Raspberry Pi 3B+ | connect to |
|---|---|
| pin 1 (3.3 V) | through 2k2 to the BC547 collector (leg 1) |
| pin 10 (GPIO15, RXD) | BC547 collector (leg 1) |
| pin 2 (5 V) | step-down converter output **5.0 V** |
| pin 9 (GND) | common ground: step-down, receiver, amplifier, battery |
| pin 4 (5 V) | DAC – VIN |
| pin 6 (GND) | DAC – GND |
| pin 14 (GND) | DAC – SCK |
| pin 12 (GPIO18) | DAC – BCK |
| pin 35 (GPIO19) | DAC – LCK |
| pin 40 (GPIO21) | DAC – DIN |

- **SBus** is inverted, so it goes through a BC547 transistor: the receiver's
  SBUS out through 1k to the base (leg 2), emitter (leg 3) to ground.
- **Power:** a 3- or 4-cell LiPo or Li-Ion battery. Set the step-down converter
  output to **5.0 V before** you connect the Raspberry Pi. The amplifier is
  powered straight from the battery. Use thicker wires for the power lines
  (battery, amplifier, speaker).
- **Sound:** the Raspberry Pi jack (basic version) or the DAC's LINE OUT goes to
  the amplifier input, the speaker to its output (recommended: Visaton EX 60 S,
  screwed or glued firmly to a large flat surface).
- **Solder jumpers on the back of the DAC (GY-PCM5102):** H1L (FLT) = L,
  H2L (DEMP) = L, H3L (XSMT) = **H**, H4L (FMT) = L.
- In `nastaveni.txt`: DAC version `zarizeni = i2s`, basic version `zarizeni = jack`.

## What the LED means

| green LED (ACT) | meaning |
|---|---|
| short blink every second | SBus OK |
| fast blinking | no SBus signal or failsafe |
| 1 long + 2 short | the card cannot be read |
| 1 long + 5 short | not enough memory (sounds too long) |
| 1 long + 6 short | the sound output cannot start |
| 1 long + 7 short | the serial port for SBus cannot be set up |

Errors are also announced by the same number of beeps (when sound works).

## Channel mapping

The transmitter channels can be assigned automatically, without looking up
channel numbers. On the `nastaveni.html` page, in the **Setup** card, switch
**Map channels** on and save (in `nastaveni.txt` it is `mapovat = ano` in the
`[diagnostika]` section). Then put the card into the tank and switch on the
transmitter and the Raspberry Pi:

1. First, rising tones play in a loop (the LED blinks twice a second) – turn
   the volume control. This also checks that the Raspberry Pi reads SBus: if
   nothing happens when you move it, no signal arrives.
2. From then on the volume control sets the volume of the following sounds
   (at least 10 %) and is not assigned to any other function.
3. Then the sound of the next function plays in a loop. Move the control that
   belongs to it clearly – at least 30 % of its range, in any direction. The
   channel is assigned, the sound stops and after 3 s of silence the next one
   starts.
   Order: volume, `motor_start` (engine switch, `motor_stop` uses the same
   channel), `motor_run` (throttle stick), `turret`, `machinegun`, `cannon`,
   `background`. A missing sound is replaced by beeps – as many as its place
   in the order.
4. A control that was already moved may be put back – that does not count.
5. If one channel comes out for two functions, three low tones sound and
   everything starts again.
6. At the end the channels are written to `nastaveni.txt` (comments stay),
   mapping switches off (`mapovat = ne`) and the Raspberry Pi restarts into
   normal operation.

## Log for setting up

When `nastaveni.txt` has `log = ano` in the `[diagnostika]` section (the
**Log** card on the `nastaveni.html` page), `log.txt` is written to the card
every 2 s: values of all channels, how the settings and sounds loaded, audio
timing. The page can show it after opening the card.

Signal dropouts have their own list in the log (the last 40, time from
power-up): gaps between SBus frames over 50 ms, frames lost between
transmitter and receiver, failsafe, channel values jumping away and back
(damaged data) and SBus line errors summed per second. Totals since power-up
are at the top.

It is for setting up and troubleshooting only – cutting the power right during
a write can damage files on the card. When you are done, set `log = ne` again;
then nothing is written to the card and the power can be cut at any time.
(The former `diag.txt` file is no longer used; the page deletes it on saving.)

## Building it yourself

    git clone --recursive https://github.com/Luboshort/sbussound-baremetal.git
    cd sbussound-baremetal
    ./sestav.sh          # needs arm-none-eabi-gcc in PATH

The result is in the `vystup/karta/` folder.

## Layout

    src/        source code (C++)
    karta/      config.txt, sample nastaveni.txt, nastaveni.html, sounds/
    docs/       wiring diagrams
    circle/     the Circle library (git submodule, pinned version)
    sestav.sh   builds the kernel and assembles the card contents
