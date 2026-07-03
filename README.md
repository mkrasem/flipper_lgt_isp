# LGT ISP (SWD) — Flipper Zero App

**Version 0.4.0** · Category: GPIO · Bilingual (English / Deutsch)

Flashes an **LGT8F328P** over its proprietary SWD protocol (GPIO bit-bang),
straight from the Flipper Zero — standalone from the SD card **or** over **USB
with avrdude**. Handling modelled on the official AVR ISP Programmer app.

The UI language (English / German) is switchable in the menu and remembered on
the SD card. All drawn screens follow the selected language.

---

## English

### Status
- The **SWD core** (`lgt_swd.c`) is a faithful port of the hardware-verified
  `ft2232_lgtisp` (C232HD) — the framing sequences are byte-for-byte identical.
- **SD flashing** and **USB flashing** are hardware-verified: avrdude
  (`-c stk500v1 -p m328p`) writes and verifies an LGT8F328P over the Flipper.
- The bilingual UI, drawn graphics, and everything else are built on top of that
  proven core.

### Wiring (Flipper GPIO → LGT8F328P, 3.3 V)
| Signal | Flipper pin | Port | → LGT |
|--------|-------------|------|-------|
| SWC    | 2           | PA7  | SWC   |
| SWD    | 3           | PA6  | SWD (bidirectional) |
| RESET  | 4           | PA4  | RSTN  |
| +3V3   | 9           | —    | VCC   |
| GND    | 11          | —    | GND   |

3.3 V targets only. Pins can be changed at the top of `lgt_swd.c`. The app also
draws this pinout under **Wiring**.

### Build & install
With **ufbt** (recommended) or `fbt` in a firmware tree:

    ufbt              # builds lgt_isp.fap
    ufbt launch       # build + run on the attached Flipper

Or copy the built `lgt_isp.fap` to `SD:/apps/GPIO/`. The app shows up under
**Apps → GPIO → LGT ISP (SWD)**.

### Usage
Menu: Flash from SD · Flash + verify · Read chip ID · USB (avrdude) · Wiring ·
About · Language.

- **Flash from SD** — pick a `.hex`, then unlock + erase + write.
- **Flash + verify** — additionally reads back and compares (same session).
- **Read chip ID** — reads the SWDID (no erase). `3E`/`3F` = LGT detected.
- **USB (avrdude)** — turns the Flipper into a virtual COM port. A **second**
  COM port appears — use that one, not the CLI port:

      avrdude -c stk500v1 -p m328p -P <COM> -U flash:w:sketch.hex:i

  The Flipper reports a 328P signature (`1E 95 0F`) because the LGT8F328P is
  flash-compatible. **Back** ends USB mode.
- **Language** — toggles English/German; stored on the SD card.

### No dump / read-out (LGT limitation)
Unlike AVR ISP, existing firmware **cannot be read back** from the LGT8F328P.
The read (EEE) engine is only enabled by the full unlock sequence, which
**erases the chip** — the enable step sits after the chip-erase in the protocol.
Reading therefore only works to *verify* what was just written in the same
session (which is what avrdude uses when flashing over USB). A dump was tested
and consistently returns `FF`, so the feature is intentionally absent. (In
effect a copy protection built into the LGT.)

### Versioning
- App version in `application.fam` as `fap_version=(MAJOR, MINOR)`.
- Patch level + on-screen version: `LGT_ISP_VERSION` in `lgt_isp.c` (About).
- SemVer 2.0.0: MAJOR = incompatible, MINOR = feature, PATCH = fix.

### Files
| File | Purpose |
|------|---------|
| `application.fam` | Flipper app manifest (version, category, icon) |
| `lgt_isp.c` | App: GUI (menu / progress / drawn screens / USB) + worker + i18n |
| `lgt_swd.c/.h` | LGT-SWD protocol (GPIO bit-bang), port of ft2232_lgtisp |
| `stk500.c/.h` | STK500v1 slave (avrdude commands → LGT), SDK-independent |
| `usb_isp.c/.h` | USB-CDC glue + STK500 worker |
| `ihex.c/.h` | Intel HEX parser/builder |
| `lgt_isp_10px.png` | App icon (10×10, 1-bit) |

---

## Deutsch

### Status
- Der **SWD-Kern** (`lgt_swd.c`) ist ein originalgetreuer Port des hardware-
  bestätigten `ft2232_lgtisp` (C232HD) — die Framing-Sequenzen sind Byte für
  Byte identisch.
- **SD-Flashen** und **USB-Flashen** sind hardware-bestätigt: avrdude
  (`-c stk500v1 -p m328p`) schreibt und verifiziert einen LGT8F328P über den
  Flipper.
- Die zweisprachige Oberfläche, die gezeichnete Grafik und der Rest bauen auf
  diesem erprobten Kern auf.

### Verdrahtung (Flipper GPIO → LGT8F328P, 3,3 V)
| Signal | Flipper-Pin | Port | → LGT |
|--------|-------------|------|-------|
| SWC    | 2           | PA7  | SWC   |
| SWD    | 3           | PA6  | SWD (bidirektional) |
| RESET  | 4           | PA4  | RSTN  |
| +3V3   | 9           | —    | VCC   |
| GND    | 11          | —    | GND   |

Nur 3,3-V-Targets. Pins oben in `lgt_swd.c` änderbar. Die App zeichnet diesen
Pinout auch unter **Verdrahtung**.

### Bauen & Installieren
Mit **ufbt** (empfohlen) oder `fbt` im Firmware-Baum:

    ufbt              # baut lgt_isp.fap
    ufbt launch       # bauen + auf dem angeschlossenen Flipper starten

Oder die gebaute `lgt_isp.fap` nach `SD:/apps/GPIO/` kopieren. App erscheint
unter **Apps → GPIO → LGT ISP (SWD)**.

### Bedienung
Menü: Flash von SD · Flash + Verify · Chip-ID lesen · USB (avrdude) ·
Verdrahtung · About · Language.

- **Flash von SD** — `.hex` wählen, dann Unlock + Erase + Write.
- **Flash + Verify** — zusätzlich Rücklesen & Vergleich (gleiche Session).
- **Chip-ID lesen** — SWDID auslesen (ohne Erase). `3E`/`3F` = LGT erkannt.
- **USB (avrdude)** — Flipper wird virtueller COM-Port. Es erscheint ein
  **zweiter** COM-Port — den nehmen, nicht den CLI-Port:

      avrdude -c stk500v1 -p m328p -P <COM> -U flash:w:sketch.hex:i

  Der Flipper meldet eine 328P-Signatur (`1E 95 0F`), weil der LGT8F328P
  flash-kompatibel ist. **Zurück** beendet den USB-Modus.
- **Language** — schaltet Deutsch/Englisch um; wird auf der SD gespeichert.

### Kein Dump/Auslesen (LGT-Grenze)
Anders als bei AVR-ISP lässt sich bestehende Firmware vom LGT8F328P **nicht
auslesen**. Die Lese-Engine (EEE) wird erst durch die volle Unlock-Sequenz
freigeschaltet, und die **löscht den Chip** — die Freischaltung steht im
Protokoll hinter dem ChipErase. Lesen funktioniert daher nur zum *Verifizieren*
des gerade Geschriebenen (das nutzt avrdude beim USB-Flashen). Ein Dump wurde
getestet und liefert konsequent `FF` — das Feature ist bewusst nicht enthalten.
(Faktisch ein Kopierschutz des LGT.)

### Versionierung
- App-Version in `application.fam` als `fap_version=(MAJOR, MINOR)`.
- Patch-Stand + Anzeige: `LGT_ISP_VERSION` in `lgt_isp.c` (About-Screen).
- SemVer 2.0.0: MAJOR = inkompatibel, MINOR = Feature, PATCH = Fix.

---

## Changelog
- **0.4.0** — Bilingual UI (English / Deutsch), switchable in the menu and stored
  on the SD card (`/ext/apps_data/lgt_isp/lang`); all drawn screens localised. /
  Zweisprachige Oberfläche, im Menü umschaltbar und auf SD gemerkt.
- **0.3.2** — More drawn graphics: DIP chip on "chip detected", drawn pinout,
  "ISP mode active" screen (connector → SWD → USB). / Mehr gezeichnete Grafik.
- **0.3.1** — Dump removed (impossible on the LGT: read engine only comes up
  after the erasing unlock). / Dump entfernt.
- **0.3.0** — (withdrawn) dump attempt; progress bar + chip mascot added.
- **0.2.4** — Fix: hang when leaving USB mode (start/stop moved to view
  enter/exit callbacks). / Fix Haenger beim Verlassen des USB-Modus.
- **0.2.3** — USB after the official AVR-ISP pattern (usb_cdc_dual +
  furi_hal_usb_lock + CLI-VCP, channel 1); hardware-verified.
- **0.2.0–0.2.2** — USB mode via STK500v1 slave (early, unstable USB handling).
- **0.1.0** — First cut: SD flash/verify + chip ID, LGT-SWD core ported.
