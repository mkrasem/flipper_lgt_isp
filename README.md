# LGT ISP (SWD) — Flipper Zero App

Flasht einen **LGT8F328P** über dessen proprietäres SWD-Protokoll (GPIO-Bitbang)
direkt vom Flipper Zero — standalone von SD **oder** per **USB mit avrdude** vom
PC. Handling im Stil der offiziellen AVR-ISP-Programmer-App.

**Version 0.2.1** · Kategorie: GPIO

## Status — ehrlich
- Der **SWD-Kern** (`lgt_swd.c`) ist ein *faithful port* aus dem hardware-
  bestätigten `ft2232_lgtisp` (C232HD) — die Framing-Sequenzen sind Byte-für-Byte
  identisch. Die **SD-Flash-Funktion läuft** (vom Nutzer gebaut & getestet).
- **Der USB-Modus (v0.2.0) ist neu und noch nicht auf Hardware bestätigt.** Er
  ist die Stelle mit den meisten „unsicheren" Punkten:
  1. Die **USB-CDC-API-Namen** (`usb_cdc_single`, `furi_hal_cdc_*`) können je
     Firmware-Zweig minimal abweichen — siehe Kommentar oben in `usb_isp.c`, das
     ist die einzige anzupassende Stelle.
  2. Ggf. **CDC-Sendehäppchen** (64 B) / Timing feinjustieren.
  Der STK500-/LGT-Teil selbst ist SDK-unabhängig und wurde syntaxgeprüft.

## Verdrahtung (Flipper GPIO → LGT8F328P, 3,3 V)
| Signal | Flipper-Pin | Port | → LGT |
|--------|-------------|------|-------|
| SWC    | 2           | PA7  | SWC   |
| SWD    | 3           | PA6  | SWD (bidirektional) |
| RESET  | 4           | PA4  | RSTN  |
| +3V3   | 9           | —    | VCC   |
| GND    | 8 / 11 / 18 | —    | GND   |

Nur für **3,3-V**-LGT-Targets. Header-Belegung ggf. gegen die offizielle
Flipper-Pinout-Karte prüfen. Pins lassen sich in `lgt_swd.c` (oben) ändern.

## Bauen & Installieren
Mit **ufbt** (empfohlen) oder im Firmware-Baum mit `fbt`:

    # Ordner in eine ufbt-Umgebung legen, dann:
    ufbt                      # baut lgt_isp.fap
    ufbt launch               # baut + startet auf dem angeschlossenen Flipper

Oder die gebaute `lgt_isp.fap` nach `SD:/apps/GPIO/` kopieren. App erscheint unter
**Apps → GPIO → LGT ISP (SWD)**.

## Bedienung
Menü:
- **Flash von SD** — `.hex` von der SD wählen → Unlock + Erase + Write.
- **Flash + Verify** — zusätzlich Rücklesen & Vergleich (in derselben Session).
- **Chip-ID lesen** — SWDID auslesen (ohne Erase). `3E`/`3F` = LGT erkannt.
- **USB (avrdude)** — Flipper wird virtueller COM-Port (siehe unten).
- **Verdrahtung / About** — Pinout bzw. Version.

### USB-Modus (wie bei der AVR-ISP-App)
Menüpunkt **USB (avrdude)** wählen → der Flipper meldet sich als serielles Gerät.
Am PC dann mit **avrdude** flashen (der Flipper meldet sich als 328P, weil der
LGT8F328P flash-kompatibel ist):

    avrdude -c stk500v1 -p m328p -P <COM-Port> -U flash:w:sketch.hex:i

- `<COM-Port>` = der neue Port (Windows: `COMxx`, Linux: `/dev/ttyACMx`).
- **Zurück** auf dem Flipper beendet den USB-Modus.

Wichtig: Der LGT-**Unlock löscht den Chip immer**. avrdude macht Write + Verify in
*einer* Session (Erase einmal, dann Schreiben, dann Rücklesen) — das passt. Ein
separater `-U flash:r` (Dump) oder ein zweiter `-U` löscht dagegen erneut; darum
gibt es bewusst keinen Dump-Modus.

## Versionierung
- App-Version in `application.fam` als `fap_version=(MAJOR, MINOR)`.
- Patch-Stand + Anzeige: `LGT_ISP_VERSION` in `lgt_isp.c` (About-Screen).
- SemVer: MAJOR = inkompatibel, MINOR = Feature, PATCH = Fix.

## Dateien
| Datei | Zweck |
|-------|-------|
| `application.fam` | Flipper-App-Manifest (Version, Kategorie, Icon) |
| `lgt_isp.c` | App: GUI (Menü/Progress/Info/USB) + Worker |
| `lgt_swd.c/.h` | LGT-SWD-Protokoll (GPIO-Bitbang), Port aus ft2232_lgtisp |
| `stk500.c/.h` | STK500v1-Slave (avrdude-Kommandos → LGT), SDK-unabhängig |
| `usb_isp.c/.h` | USB-CDC-Glue + STK500-Worker |
| `ihex.c/.h` | Intel-HEX Parser/Builder |
| `lgt_isp_10px.png` | App-Icon (10×10, 1-bit) |

## Changelog
- **0.2.1** — Build-Fix: korrektes CDC-Header (`furi_hal_usb_cdc.h`, kein
  `furi_hal_cdc.h`), `furi_hal_cdc_receive` als int32_t; deprecated
  `view_dispatcher_enable_queue` entfernt.
- **0.2.0** — USB-Modus: STK500v1-Slave über USB-CDC, avrdude-flashbar
  (`-c stk500v1 -p m328p`); übersetzt auf LGT-SWD, meldet 328P-Signatur.
- **0.1.0** — Erstwurf: SD-Flash/Verify + Chip-ID, LGT-SWD-Kern portiert.
