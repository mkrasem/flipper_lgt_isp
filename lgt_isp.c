/* ============================================================
 * lgt_isp.c  —  Flipper Zero App: LGT8F328P Flasher (proprietaeres SWD)
 * Handling im Stil der AVR-ISP-Programmer-App: Menue -> .hex von SD waehlen ->
 * flashen (+Verify), Chip-ID lesen, Verdrahtung/About mit Version.
 *
 * NICHT hier baubar/testbar (kein Flipper-SDK/Hardware im Sandkasten). Bauen mit
 * ufbt/fbt gegen die Ziel-Firmware; einzelne API-Signaturen koennen je nach
 * Firmware-Version minimal abweichen. Der SWD-Kern (lgt_swd.c) ist ein faithful
 * Port des hardware-bestaetigten ft2232_lgtisp.
 * ============================================================ */
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/view.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <gui/elements.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include "lgt_swd.h"
#include "ihex.h"
#include "usb_isp.h"
#include "stk500.h"

#define LGT_ISP_VERSION "0.5.0"
#define TAG             "LgtIsp"
#define HEX_MAX         (128 * 1024)   /* max. HEX-Dateigroesse */
#define HEX_DIR         "/ext"

typedef enum { ViewMenu, ViewWork, ViewInfo, ViewUsb, ViewWiring } ViewId;
typedef enum { EvProgress = 100, EvDone } CustomEvent;
typedef enum { OpFlash, OpFlashVerify, OpReadId } Op;

typedef enum {
    ItemFlash,
    ItemFlashVerify,
    ItemReadId,
    ItemUsb,
    ItemWiring,
    ItemAbout,
    ItemChip,
    ItemLang,
} MenuItem;

/* ---------- Chip-Auswahl (LGT8FX8P-Familie) ---------- */
typedef struct {
    const char* name;    /* "LGT8F328P" */
    const char* shortn;  /* "328P" */
    const char* part;    /* avrdude -p, z.B. "m328p" */
    uint16_t kb;         /* Flash in KB */
    uint8_t sig[3];      /* an avrdude gemeldete Signatur */
    bool untested;
} Chip;

static const Chip CHIPS[] = {
    {"LGT8F328P", "328P", "m328p", 32, {0x1E, 0x95, 0x0F}, false},
    {"LGT8F168P", "168P", "m168p", 16, {0x1E, 0x94, 0x0B}, true},
    {"LGT8F88P",  "88P",  "m88p",  8,  {0x1E, 0x93, 0x0A}, true},
};
#define N_CHIPS ((int)(sizeof(CHIPS) / sizeof(CHIPS[0])))
static int g_chip = 0;

/* ---------- Zweisprachigkeit (DE / EN-GB) ---------- */
typedef struct {
    const char *menu_flash, *menu_flash_verify, *menu_id, *menu_usb;
    const char *menu_wiring, *menu_about, *menu_lang, *menu_chip;
    const char *untested;
    const char *phase_start, *phase_write, *phase_verify;
    const char *back_menu;
    const char *detected, *not_detected;
    const char *res_no_lgt, *res_unlock, *res_ok_verified, *res_ok_written;
    const char *res_diff;                   /* printf-Format mit %d */
    const char *res_bad_hex;
    const char *usb_title, *usb_hint;
    const char *wiring_title, *wiring_note;
    const char *about;
} Strings;

static const Strings STR_DE = {
    .menu_flash = "Flash von SD",
    .menu_flash_verify = "Flash + Verify",
    .menu_id = "Chip-ID lesen",
    .menu_usb = "USB (avrdude)",
    .menu_wiring = "Verdrahtung",
    .menu_about = "About",
    .menu_lang = "Language: English",       /* zeigt die ZIEL-Sprache */
    .menu_chip = "Chip:",
    .untested = "ungetestet!",
    .phase_start = "Start",
    .phase_write = "Schreiben",
    .phase_verify = "Verify",
    .back_menu = "Zurueck = Menue",
    .detected = "LGT erkannt!",
    .not_detected = "Kein LGT",
    .res_no_lgt = "Kein LGT erkannt",
    .res_unlock = "Kein LGT / Unlock-Fehler",
    .res_ok_verified = "OK: verifiziert",
    .res_ok_written = "OK: geschrieben",
    .res_diff = "FEHLER: %d Byte diff",
    .res_bad_hex = "HEX-Datei ungueltig",
    .usb_title = "ISP aktiv",
    .usb_hint = "avrdude: 2. COM-Port",
    .wiring_title = "Verdrahtung",
    .wiring_note = "3V3 (Pin9) empfohlen",
    .about = "LGT ISP (SWD)  v" LGT_ISP_VERSION "\n"
             "\n"
             "Flasht LGT8F328P ueber das\n"
             "proprietaere LGT-SWD (GPIO-\n"
             "Bitbang). Kern portiert aus\n"
             "ft2232_lgtisp (hardware-\n"
             "bestaetigt am C232HD).\n"
             "\n"
             "Gebaut/getestet fuer 328P\n"
             "(32K). Gleiche SWD-Familie:\n"
             "88P/168P (8K/16K) ungetestet.\n"
             "\n"
             "Achtung: Unlock loescht den\n"
             "Chip immer. Verify nur direkt\n"
             "nach dem Flashen sinnvoll.",
};

static const Strings STR_EN = {
    .menu_flash = "Flash from SD",
    .menu_flash_verify = "Flash + verify",
    .menu_id = "Read chip ID",
    .menu_usb = "USB (avrdude)",
    .menu_wiring = "Wiring",
    .menu_about = "About",
    .menu_lang = "Sprache: Deutsch",        /* zeigt die ZIEL-Sprache */
    .menu_chip = "Chip:",
    .untested = "untested!",
    .phase_start = "Start",
    .phase_write = "Writing",
    .phase_verify = "Verifying",
    .back_menu = "Back = menu",
    .detected = "LGT detected!",
    .not_detected = "No LGT",
    .res_no_lgt = "No LGT detected",
    .res_unlock = "No LGT / unlock failed",
    .res_ok_verified = "OK: verified",
    .res_ok_written = "OK: written",
    .res_diff = "ERROR: %d bytes differ",
    .res_bad_hex = "Invalid HEX file",
    .usb_title = "ISP mode active",
    .usb_hint = "avrdude: use 2nd COM port",
    .wiring_title = "Wiring",
    .wiring_note = "3V3 (pin 9) preferred",
    .about = "LGT ISP (SWD)  v" LGT_ISP_VERSION "\n"
             "\n"
             "Flashes the LGT8F328P via its\n"
             "proprietary SWD protocol (GPIO\n"
             "bit-bang). Core ported from\n"
             "ft2232_lgtisp (hardware-\n"
             "verified on a C232HD).\n"
             "\n"
             "Built/tested for the 328P\n"
             "(32K). Same SWD family:\n"
             "88P/168P (8K/16K) untested.\n"
             "\n"
             "Note: unlocking always erases\n"
             "the chip. Verify is only mean-\n"
             "ingful right after flashing.",
};

static const Strings* S = &STR_DE;
static bool g_lang_en = false;

#define LANG_DIR  "/ext/apps_data/lgt_isp"
#define LANG_PATH LANG_DIR "/lang"

typedef struct {
    Gui* gui;
    ViewDispatcher* vd;
    Submenu* menu;
    View* work;                 /* eigener Live-Progress-/Ergebnis-View */
    Widget* info;               /* scrollbarer Text (About) */
    View* usb_view;             /* USB-Modus-Screen (eigener enter/exit-Lifecycle) */
    View* wiring_view;          /* gezeichneter Verdrahtungsplan */
    UsbIsp* usb;                /* != NULL wenn USB-Modus aktiv */
    DialogsApp* dialogs;
    Storage* storage;
    NotificationApp* notif;
    FuriThread* worker;
    FuriMutex* mtx;

    uint8_t* img;               /* 32K Flash-Abbild (Heap) */
    uint32_t img_len;
    Op op;
    char chip_label[24];        /* "Chip: 328P" fuer das Menue */

    /* Fortschritt (Worker -> GUI, via mtx) */
    uint32_t w_done, w_total;
    char w_phase[16];
    char w_result[64];
    bool w_finished;
    bool w_ok;
} App;

/* ---------- Live-Progress-View ---------- */
typedef struct {
    uint32_t done, total;
    char phase[16];
    char result[64];
    bool finished;
    bool is_id;   /* Ergebnis eines Chip-ID-Laufs -> Chip-Grafik */
    bool ok;
} WorkModel;

/* niedlicher Chip-Mascot (Fortschritts-Screen) */
static void draw_chip(Canvas* c, int x, int y) {
    canvas_draw_rframe(c, x, y, 28, 24, 3);
    for(int i = 0; i < 4; i++) {
        canvas_draw_line(c, x - 3, y + 4 + i * 5, x - 1, y + 4 + i * 5);
        canvas_draw_line(c, x + 28, y + 4 + i * 5, x + 30, y + 4 + i * 5);
    }
    canvas_draw_line(c, x + 11, y + 1, x + 17, y + 1);
    canvas_draw_disc(c, x + 9, y + 10, 2);
    canvas_draw_disc(c, x + 19, y + 10, 2);
    canvas_draw_line(c, x + 10, y + 17, x + 18, y + 17);
    canvas_draw_dot(c, x + 9, y + 16);
    canvas_draw_dot(c, x + 19, y + 16);
}

/* DIP-Chip mit Beinchen oben+unten (wie "chip detected") */
static void draw_ic(Canvas* c, int x, int y, int w, int h) {
    int i, n = (w - 6) / 7;
    canvas_draw_rframe(c, x, y, w, h, 3);
    for(i = 0; i < n; i++) {
        int px = x + 6 + i * 7;
        canvas_draw_line(c, px, y - 2, px, y - 1);
        canvas_draw_line(c, px, y + h + 1, px, y + h + 2);
    }
    canvas_draw_disc(c, x + 5, y + h / 2, 1);   /* Orientierungspunkt */
}

/* "Chip erkannt"-Screen (Chip-ID-Ergebnis) */
static void draw_chip_detected(Canvas* c, bool ok, const char* line) {
    const Chip* ch = &CHIPS[g_chip];
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 64, 8, AlignCenter, AlignCenter, ok ? S->detected : S->not_detected);
    draw_ic(c, 30, 18, 68, 20);
    canvas_set_font(c, FontSecondary);
    if(ok) {
        char kb[16];
        snprintf(kb, sizeof(kb), "%s  %u Kb", ch->shortn, ch->kb);
        canvas_draw_str_aligned(c, 64, 28, AlignCenter, AlignCenter, kb);
    }
    canvas_draw_str_aligned(c, 64, 44, AlignCenter, AlignCenter, line);
    if(ok && ch->untested)
        canvas_draw_str_aligned(c, 64, 53, AlignCenter, AlignCenter, S->untested);
    canvas_draw_str_aligned(c, 64, 62, AlignCenter, AlignCenter, S->back_menu);
}

static void work_draw(Canvas* canvas, void* model) {
    WorkModel* m = model;
    canvas_clear(canvas);
    if(m->finished && m->is_id) {          /* Chip-ID -> hübsche Chip-Grafik */
        draw_chip_detected(canvas, m->ok, m->result);
        return;
    }
    draw_chip(canvas, 6, 30);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 44, 12, "LGT ISP");
    canvas_set_font(canvas, FontSecondary);
    if(!m->finished) {
        char buf[16];
        int pct = m->total ? (int)((uint64_t)m->done * 100u / m->total) : 0;
        canvas_draw_str(canvas, 44, 26, m->phase);
        snprintf(buf, sizeof(buf), "%d%%", pct);
        elements_progress_bar_with_text(canvas, 44, 32, 82, (float)pct / 100.0f, buf);
    } else {
        elements_multiline_text_aligned(canvas, 44, 24, AlignLeft, AlignTop, m->result);
        canvas_draw_str(canvas, 44, 62, S->back_menu);
    }
}

/* Back auf dem Work-View -> zurueck ins Menue */
static uint32_t nav_to_menu(void* ctx) {
    UNUSED(ctx);
    return ViewMenu;
}
/* Back im Menue -> App beenden */
static bool nav_exit(void* ctx) {
    App* app = ctx;
    view_dispatcher_stop(app->vd);
    return true;
}

/* USB-View mit eigenem Lifecycle — GENAU wie die offizielle AVR-ISP-App:
 * Start/Stop passieren in den view_dispatcher enter/exit-Callbacks, NICHT im
 * Navigations-Callback. Das synchrone Stop (join + USB-Config zurueck) im
 * Back-Handler war die Ursache, warum der Flipper beim Verlassen haengen blieb. */
static void usb_draw(Canvas* c, void* model) {
    UNUSED(model);
    canvas_clear(c);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 76, 8, AlignCenter, AlignCenter, S->usb_title);
    /* Stecker + Ribbon-Kabel links */
    canvas_draw_rframe(c, 4, 24, 14, 20, 2);
    for(int i = 0; i < 7; i++) canvas_draw_line(c, 18, 27 + i * 2, 25, 27 + i * 2);
    /* SWD-Box Mitte */
    canvas_draw_rframe(c, 40, 24, 42, 20, 5);
    canvas_draw_str_aligned(c, 61, 35, AlignCenter, AlignCenter, "SWD");
    /* Verbindung -> USB-Symbol (angedeuteter Dreizack) */
    canvas_draw_line(c, 82, 34, 92, 34);
    canvas_draw_disc(c, 94, 34, 2);
    canvas_draw_line(c, 96, 34, 113, 34);
    canvas_draw_line(c, 101, 34, 101, 29);
    canvas_draw_disc(c, 101, 28, 1);
    canvas_draw_line(c, 106, 34, 106, 39);
    canvas_draw_box(c, 105, 39, 3, 3);
    canvas_draw_box(c, 110, 31, 5, 6);
    canvas_set_font(c, FontSecondary);
    const Chip* ch = &CHIPS[g_chip];
    char hint[32];
    snprintf(hint, sizeof(hint), "-p %s  %s", ch->part, ch->untested ? S->untested : "2.COM");
    canvas_draw_str(c, 2, 62, hint);
}
static void usb_enter(void* ctx) {
    App* app = ctx;
    const Chip* ch = &CHIPS[g_chip];
    stk500_set_signature(ch->sig[0], ch->sig[1], ch->sig[2]);   /* gewaehlter Chip -> Signatur */
    if(!app->usb) app->usb = usb_isp_start();
}
static void usb_exit(void* ctx) {
    App* app = ctx;
    if(app->usb) {
        usb_isp_stop(app->usb);
        app->usb = NULL;
    }
}

/* ---------- HEX von SD laden ---------- */
static bool load_hex_file(App* app, const char* path) {
    File* f = storage_file_alloc(app->storage);
    bool ok = false;
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t sz = storage_file_size(f);
        if(sz > 0 && sz <= HEX_MAX) {
            char* buf = malloc((size_t)sz + 1);
            if(buf) {
                size_t rd = storage_file_read(f, buf, (size_t)sz);
                if(rd == (size_t)sz) {
                    buf[sz] = 0;
                    ihex_parse(buf, (size_t)sz, app->img, LGT_FLASH_BYTES, &app->img_len);
                    ok = (app->img_len > 0);
                }
                free(buf);
            }
        }
    }
    storage_file_close(f);
    storage_file_free(f);
    return ok;
}

static void progress_cb(void* ctx, uint32_t done, uint32_t total, int phase) {
    App* app = ctx;
    const char* p = (phase == LGT_PHASE_VERIFY) ? S->phase_verify : S->phase_write;
    furi_mutex_acquire(app->mtx, FuriWaitForever);
    app->w_done = done;
    app->w_total = total;
    strncpy(app->w_phase, p, sizeof(app->w_phase) - 1);
    app->w_phase[sizeof(app->w_phase) - 1] = 0;
    furi_mutex_release(app->mtx);
    view_dispatcher_send_custom_event(app->vd, EvProgress);
}

static int32_t worker_thread(void* ctx) {
    App* app = ctx;
    if(app->op == OpReadId) {
        uint8_t id[4] = {0};
        bool ok = lgt_read_id(id);
        furi_mutex_acquire(app->mtx, FuriWaitForever);
        if(ok)
            snprintf(app->w_result, sizeof(app->w_result), "ID: %02X %02X %02X %02X", id[0], id[1], id[2], id[3]);
        else
            snprintf(app->w_result, sizeof(app->w_result), "%s", S->res_no_lgt);
        app->w_ok = ok;
        app->w_finished = true;
        furi_mutex_release(app->mtx);
    } else {
        bool verify = (app->op == OpFlashVerify);
        int r = lgt_flash(app->img, app->img_len, verify, progress_cb, app);
        furi_mutex_acquire(app->mtx, FuriWaitForever);
        if(r < 0)
            snprintf(app->w_result, sizeof(app->w_result), "%s", S->res_unlock);
        else if(r == 0)
            snprintf(app->w_result, sizeof(app->w_result), "%s", verify ? S->res_ok_verified : S->res_ok_written);
        else
            snprintf(app->w_result, sizeof(app->w_result), S->res_diff, r);
        app->w_ok = (r == 0);
        app->w_finished = true;
        furi_mutex_release(app->mtx);
    }
    view_dispatcher_send_custom_event(app->vd, EvDone);
    return 0;
}

static void start_work(App* app) {
    /* Work-View zuruecksetzen */
    with_view_model(
        app->work, WorkModel * m,
        {
            m->done = 0;
            m->total = 1;
            m->finished = false;
            m->is_id = (app->op == OpReadId);
            m->ok = false;
            strncpy(m->phase, S->phase_start, sizeof(m->phase) - 1);
            m->result[0] = 0;
        },
        true);
    furi_mutex_acquire(app->mtx, FuriWaitForever);
    app->w_finished = false;
    app->w_done = 0;
    app->w_total = 1;
    app->w_result[0] = 0;
    furi_mutex_release(app->mtx);

    view_dispatcher_switch_to_view(app->vd, ViewWork);
    furi_thread_start(app->worker);
}

/* ---------- Custom-Events (GUI-Thread) ---------- */
static bool custom_cb(void* ctx, uint32_t event) {
    App* app = ctx;
    if(event == EvProgress || event == EvDone) {
        with_view_model(
            app->work, WorkModel * m,
            {
                furi_mutex_acquire(app->mtx, FuriWaitForever);
                m->done = app->w_done;
                m->total = app->w_total;
                strncpy(m->phase, app->w_phase, sizeof(m->phase) - 1);
                m->phase[sizeof(m->phase) - 1] = 0;
                strncpy(m->result, app->w_result, sizeof(m->result) - 1);
                m->result[sizeof(m->result) - 1] = 0;
                m->finished = app->w_finished;
                m->ok = app->w_ok;
                furi_mutex_release(app->mtx);
            },
            true);
        if(event == EvDone) {
            furi_thread_join(app->worker);      /* Worker ist zurueckgekehrt */
            if(app->notif)
                notification_message(app->notif, app->w_ok ? &sequence_success : &sequence_error);
        }
        return true;
    }
    return false;
}

/* ---------- gezeichneter Verdrahtungsplan (Flipper GPIO -> LGT) ---------- */
static void draw_wiring(Canvas* c, void* model) {
    UNUSED(model);
    static const char* sig[5] = {"SWC", "SWD", "RST", "3V3", "GND"};
    static const char* pin[5] = {"P2", "P3", "P4", "P9", "P11"};
    canvas_clear(c);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 2, 9, S->wiring_title);
    canvas_set_font(c, FontSecondary);
    for(int i = 0; i < 5; i++) {
        int x = 2 + i * 25;
        canvas_draw_rframe(c, x, 15, 23, 13, 2);                        /* Signal-Box */
        canvas_draw_str_aligned(c, x + 11, 22, AlignCenter, AlignCenter, sig[i]);
        canvas_draw_str_aligned(c, x + 11, 34, AlignCenter, AlignCenter, pin[i]);
        canvas_draw_line(c, x + 11, 38, x + 11, 45);                    /* Leitung zum Header */
    }
    canvas_draw_frame(c, 2, 45, 123, 9);                                /* Header-Leiste */
    for(int i = 0; i < 5; i++) canvas_draw_box(c, 11 + i * 25, 47, 5, 5);
    canvas_draw_str(c, 2, 63, S->wiring_note);
}

static void show_info(App* app, const char* text) {
    widget_reset(app->info);
    widget_add_text_scroll_element(app->info, 0, 0, 128, 64, text);
    view_dispatcher_switch_to_view(app->vd, ViewInfo);
}

/* ---------- Sprache + Menueaufbau ---------- */
static void menu_cb(void* ctx, uint32_t index);   /* fwd */

static void lang_apply(void) {
    S = g_lang_en ? &STR_EN : &STR_DE;
}

static void menu_build(App* app) {
    snprintf(app->chip_label, sizeof(app->chip_label), "%s %s", S->menu_chip, CHIPS[g_chip].shortn);
    submenu_reset(app->menu);
    submenu_add_item(app->menu, S->menu_flash, ItemFlash, menu_cb, app);
    submenu_add_item(app->menu, S->menu_flash_verify, ItemFlashVerify, menu_cb, app);
    submenu_add_item(app->menu, S->menu_id, ItemReadId, menu_cb, app);
    submenu_add_item(app->menu, S->menu_usb, ItemUsb, menu_cb, app);
    submenu_add_item(app->menu, S->menu_wiring, ItemWiring, menu_cb, app);
    submenu_add_item(app->menu, S->menu_about, ItemAbout, menu_cb, app);
    submenu_add_item(app->menu, app->chip_label, ItemChip, menu_cb, app);
    submenu_add_item(app->menu, S->menu_lang, ItemLang, menu_cb, app);
}

static void lang_load(App* app) {
    File* f = storage_file_alloc(app->storage);
    char b[4] = {0};
    if(storage_file_open(f, LANG_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_read(f, (uint8_t*)b, 3);
        if(b[0] == 'e' && b[1] == 'n') g_lang_en = true;
        if(b[2] >= '0' && b[2] < '0' + N_CHIPS) g_chip = b[2] - '0';
    }
    storage_file_close(f);
    storage_file_free(f);
    lang_apply();
}

static void lang_save(App* app) {
    storage_common_mkdir(app->storage, LANG_DIR);
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, LANG_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char b[3] = {(char)(g_lang_en ? 'e' : 'd'), (char)(g_lang_en ? 'n' : 'e'), (char)('0' + g_chip)};
        storage_file_write(f, b, 3);
    }
    storage_file_close(f);
    storage_file_free(f);
}

/* ---------- Menue ---------- */
static void menu_cb(void* ctx, uint32_t index) {
    App* app = ctx;
    switch(index) {
    case ItemFlash:
    case ItemFlashVerify: {
        FuriString* path = furi_string_alloc_set(HEX_DIR);
        DialogsFileBrowserOptions opt;
        dialog_file_browser_set_basic_options(&opt, ".hex", NULL);
        opt.base_path = HEX_DIR;
        bool picked = dialog_file_browser_show(app->dialogs, path, path, &opt);
        if(picked) {
            if(load_hex_file(app, furi_string_get_cstr(path))) {
                app->op = (index == ItemFlash) ? OpFlash : OpFlashVerify;
                start_work(app);
            } else {
                furi_mutex_acquire(app->mtx, FuriWaitForever);
                snprintf(app->w_result, sizeof(app->w_result), "%s", S->res_bad_hex);
                app->w_ok = false;
                app->w_finished = true;
                furi_mutex_release(app->mtx);
                view_dispatcher_switch_to_view(app->vd, ViewWork);
                view_dispatcher_send_custom_event(app->vd, EvDone);
            }
        }
        furi_string_free(path);
        break;
    }
    case ItemReadId:
        app->op = OpReadId;
        start_work(app);
        break;
    case ItemUsb:
        view_dispatcher_switch_to_view(app->vd, ViewUsb);   /* enter-Callback startet USB */
        break;
    case ItemWiring:
        view_dispatcher_switch_to_view(app->vd, ViewWiring);
        break;
    case ItemAbout:
        show_info(app, S->about);
        break;
    case ItemChip:
        g_chip = (g_chip + 1) % N_CHIPS;
        lang_save(app);
        menu_build(app);       /* Label aktualisieren */
        break;
    case ItemLang:
        g_lang_en = !g_lang_en;
        lang_apply();
        lang_save(app);
        menu_build(app);       /* Menue in neuer Sprache neu aufbauen */
        break;
    default:
        break;
    }
}

/* ---------- App-Lifecycle ---------- */
static App* app_alloc(void) {
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->img = malloc(LGT_FLASH_BYTES);
    app->mtx = furi_mutex_alloc(FuriMutexTypeNormal);

    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->notif = furi_record_open(RECORD_NOTIFICATION);

    app->vd = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->vd, app);
    view_dispatcher_set_custom_event_callback(app->vd, custom_cb);
    view_dispatcher_set_navigation_event_callback(app->vd, nav_exit);

    /* Sprache laden + Menue aufbauen */
    app->menu = submenu_alloc();
    lang_load(app);
    menu_build(app);
    view_dispatcher_add_view(app->vd, ViewMenu, submenu_get_view(app->menu));

    /* Work-View */
    app->work = view_alloc();
    view_allocate_model(app->work, ViewModelTypeLocking, sizeof(WorkModel));
    view_set_context(app->work, app);
    view_set_draw_callback(app->work, work_draw);
    view_set_previous_callback(app->work, nav_to_menu);
    view_dispatcher_add_view(app->vd, ViewWork, app->work);

    /* Info-Widget */
    app->info = widget_alloc();
    view_set_previous_callback(widget_get_view(app->info), nav_to_menu);
    view_dispatcher_add_view(app->vd, ViewInfo, widget_get_view(app->info));

    /* USB-Modus-View mit enter/exit-Lifecycle (Start/Stop wie offizielle App) */
    app->usb_view = view_alloc();
    view_set_context(app->usb_view, app);
    view_set_draw_callback(app->usb_view, usb_draw);
    view_set_enter_callback(app->usb_view, usb_enter);
    view_set_exit_callback(app->usb_view, usb_exit);
    view_set_previous_callback(app->usb_view, nav_to_menu);
    view_dispatcher_add_view(app->vd, ViewUsb, app->usb_view);

    /* Verdrahtungs-View (gezeichnet) */
    app->wiring_view = view_alloc();
    view_set_draw_callback(app->wiring_view, draw_wiring);
    view_set_previous_callback(app->wiring_view, nav_to_menu);
    view_dispatcher_add_view(app->vd, ViewWiring, app->wiring_view);

    /* Worker (einmal allokiert, pro Op gestartet/gejoint) */
    app->worker = furi_thread_alloc();
    furi_thread_set_name(app->worker, "LgtIspWorker");
    furi_thread_set_stack_size(app->worker, 2048);
    furi_thread_set_context(app->worker, app);
    furi_thread_set_callback(app->worker, worker_thread);

    view_dispatcher_attach_to_gui(app->vd, app->gui, ViewDispatcherTypeFullscreen);
    return app;
}

static void app_free(App* app) {
    if(app->usb) {
        usb_isp_stop(app->usb);
        app->usb = NULL;
    }
    view_dispatcher_remove_view(app->vd, ViewMenu);
    view_dispatcher_remove_view(app->vd, ViewWork);
    view_dispatcher_remove_view(app->vd, ViewInfo);
    view_dispatcher_remove_view(app->vd, ViewUsb);
    view_dispatcher_remove_view(app->vd, ViewWiring);
    submenu_free(app->menu);
    view_free(app->work);
    widget_free(app->info);
    view_free(app->usb_view);
    view_free(app->wiring_view);
    view_dispatcher_free(app->vd);
    furi_thread_free(app->worker);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);

    furi_mutex_free(app->mtx);
    free(app->img);
    free(app);
}

int32_t lgt_isp_app(void* p) {
    UNUSED(p);
    App* app = app_alloc();
    view_dispatcher_switch_to_view(app->vd, ViewMenu);
    view_dispatcher_run(app->vd);
    app_free(app);
    return 0;
}
