/* ============================================================
 * lgt_swd.h  —  LGT8F328P SWD-Programmierprotokoll (GPIO-Bitbang)
 * Portiert 1:1 aus dem hardware-bestaetigten ft2232_lgtisp (nur die
 * unterste "ein SWD-Bit takten/lesen"-Ebene ist furi_hal_gpio statt MPSSE).
 * Das ist das proprietaere, reverse-engineerte LGT-Protokoll — NICHT ARM-SWD.
 * ============================================================ */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define LGT_FLASH_BYTES 32768u
#define LGT_PAGE_BYTES  128u

/* Fortschritts-Callback: done/total in Bytes, phase = Text ("Schreiben"/"Verify"/"Lesen"). */
typedef void (*LgtProgressCb)(void* ctx, uint32_t done, uint32_t total, const char* phase);

/* Halbbit-Verzoegerung in Mikrosekunden (Timing-Tuning; Default 2). Kleiner = schneller. */
void lgt_set_delay_us(uint32_t us);

/* GPIO einrichten / freigeben (SWC/SWD/RSTN als Ausgang, SWD spaeter bidirektional). */
void lgt_gpio_init(void);
void lgt_gpio_deinit(void);

/* --- High-Level (machen Reset/Unlock/Erase selbst) ---
 * Rueckgabe: >=0 = Zahl abweichender Bytes (0 = ok), -1 = kein LGT erkannt. */
int  lgt_flash(const uint8_t* img, uint32_t img_len, bool verify, LgtProgressCb cb, void* ctx);
int  lgt_verify(const uint8_t* img, uint32_t img_len, LgtProgressCb cb, void* ctx);
int  lgt_dump(uint8_t* out /* >= LGT_FLASH_BYTES */, LgtProgressCb cb, void* ctx);

/* Nur die Chip-ID lesen (kein Erase). true = LGT erkannt, id[0]=0x3E/0x3F. */
bool lgt_read_id(uint8_t id[4]);
