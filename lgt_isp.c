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

#define LGT_ISP_VERSION "0.3.1"
#define TAG             "LgtIsp"
#define HEX_MAX         (128 * 1024)   /* max. HEX-Dateigroesse */
#define HEX_DIR         "/ext"

typedef enum { ViewMenu, ViewWork, ViewInfo, ViewUsb } ViewId;
typedef enum { EvProgress = 100, EvDone } CustomEvent;
typedef enum { OpFlash, OpFlashVerify, OpReadId } Op;

typedef enum {
    ItemFlash,
    ItemFlashVerify,
    ItemReadId,
    ItemUsb,
    ItemWiring,
    ItemAbout,
} MenuItem;

typedef struct {
    Gui* gui;
    ViewDispatcher* vd;
    Submenu* menu;
    View* work;                 /* eigener Live-Progress-/Ergebnis-View */
    Widget* info;               /* scrollbarer Text (Verdrahtung/About) */
    View* usb_view;             /* USB-Modus-Screen (eigener enter/exit-Lifecycle) */
    UsbIsp* usb;                /* != NULL wenn USB-Modus aktiv */
    DialogsApp* dialogs;
    Storage* storage;
    NotificationApp* notif;
    FuriThread* worker;
    FuriMutex* mtx;

    uint8_t* img;               /* 32K Flash-Abbild (Heap) */
    uint32_t img_len;
    Op op;

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
} WorkModel;

/* niedlicher Chip-Mascot (Canvas-Primitive, kein PNG noetig) */
static void draw_chip(Canvas* c, int x, int y) {
    canvas_draw_rframe(c, x, y, 28, 24, 3);            /* IC-Koerper */
    for(int i = 0; i < 4; i++) {                       /* Beinchen */
        canvas_draw_line(c, x - 3, y + 4 + i * 5, x - 1, y + 4 + i * 5);
        canvas_draw_line(c, x + 28, y + 4 + i * 5, x + 30, y + 4 + i * 5);
    }
    canvas_draw_line(c, x + 11, y + 1, x + 17, y + 1); /* Kerbe */
    canvas_draw_disc(c, x + 9, y + 10, 2);             /* Augen */
    canvas_draw_disc(c, x + 19, y + 10, 2);
    canvas_draw_line(c, x + 10, y + 17, x + 18, y + 17); /* Laecheln */
    canvas_draw_dot(c, x + 9, y + 16);
    canvas_draw_dot(c, x + 19, y + 16);
}

static void work_draw(Canvas* canvas, void* model) {
    WorkModel* m = model;
    canvas_clear(canvas);
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
        canvas_draw_str(canvas, 44, 62, "Zurueck = Menue");
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
static void usb_draw(Canvas* canvas, void* model) {
    UNUSED(model);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "USB aktiv");
    draw_chip(canvas, 8, 30);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 48, 28, "avrdude:");
    canvas_draw_str(canvas, 48, 39, "stk500v1 / 328P");
    canvas_draw_str(canvas, 48, 50, "2. COM-Port!");
    canvas_draw_str(canvas, 2, 63, "Zurueck = beenden");
}
static void usb_enter(void* ctx) {
    App* app = ctx;
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

static void progress_cb(void* ctx, uint32_t done, uint32_t total, const char* phase) {
    App* app = ctx;
    furi_mutex_acquire(app->mtx, FuriWaitForever);
    app->w_done = done;
    app->w_total = total;
    strncpy(app->w_phase, phase, sizeof(app->w_phase) - 1);
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
            snprintf(app->w_result, sizeof(app->w_result), "Kein LGT erkannt");
        app->w_ok = ok;
        app->w_finished = true;
        furi_mutex_release(app->mtx);
    } else {
        bool verify = (app->op == OpFlashVerify);
        int r = lgt_flash(app->img, app->img_len, verify, progress_cb, app);
        furi_mutex_acquire(app->mtx, FuriWaitForever);
        if(r < 0)
            snprintf(app->w_result, sizeof(app->w_result), "Kein LGT / Unlock-Fehler");
        else if(r == 0)
            snprintf(app->w_result, sizeof(app->w_result), verify ? "OK: verifiziert" : "OK: geschrieben");
        else
            snprintf(app->w_result, sizeof(app->w_result), "FEHLER: %d Byte diff", r);
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
            strncpy(m->phase, "Start", sizeof(m->phase) - 1);
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

/* ---------- Info-Texte ---------- */
static const char* WIRING_TEXT =
    "Verdrahtung (Flipper GPIO -> LGT)\n"
    "SWC : Pin 2 (PA7) -> SWC\n"
    "SWD : Pin 3 (PA6) -> SWD\n"
    "RST : Pin 4 (PA4) -> RSTN\n"
    "3V3 : Pin 9       -> VCC\n"
    "GND : Pin 8/11/18 -> GND\n"
    "\n"
    "Nur fuer 3,3-V-LGT-Targets.\n"
    "Header-Pins ggf. gegen die\n"
    "offizielle Flipper-Belegung\n"
    "pruefen.";

static const char* ABOUT_TEXT =
    "LGT ISP (SWD)  v" LGT_ISP_VERSION "\n"
    "\n"
    "Flasht LGT8F328P ueber das\n"
    "proprietaere LGT-SWD (GPIO-\n"
    "Bitbang). Kern portiert aus\n"
    "ft2232_lgtisp (hardware-\n"
    "bestaetigt am C232HD).\n"
    "\n"
    "Achtung: Unlock loescht den\n"
    "Chip immer. Verify nur direkt\n"
    "nach dem Flashen sinnvoll.";

static void show_info(App* app, const char* text) {
    widget_reset(app->info);
    widget_add_text_scroll_element(app->info, 0, 0, 128, 64, text);
    view_dispatcher_switch_to_view(app->vd, ViewInfo);
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
                snprintf(app->w_result, sizeof(app->w_result), "HEX-Datei ungueltig");
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
        show_info(app, WIRING_TEXT);
        break;
    case ItemAbout:
        show_info(app, ABOUT_TEXT);
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

    /* Menue */
    app->menu = submenu_alloc();
    submenu_add_item(app->menu, "Flash von SD", ItemFlash, menu_cb, app);
    submenu_add_item(app->menu, "Flash + Verify", ItemFlashVerify, menu_cb, app);
    submenu_add_item(app->menu, "Chip-ID lesen", ItemReadId, menu_cb, app);
    submenu_add_item(app->menu, "USB (avrdude)", ItemUsb, menu_cb, app);
    submenu_add_item(app->menu, "Verdrahtung", ItemWiring, menu_cb, app);
    submenu_add_item(app->menu, "About", ItemAbout, menu_cb, app);
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
    submenu_free(app->menu);
    view_free(app->work);
    widget_free(app->info);
    view_free(app->usb_view);
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
