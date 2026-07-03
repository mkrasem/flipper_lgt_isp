/* ============================================================
 * usb_isp.c  —  USB-CDC (virtuelles COM) + STK500-Worker fuer den Flipper.
 *
 * ACHTUNG (nicht hier testbar): die USB-CDC-Namen koennen je nach Firmware
 * leicht abweichen. Gepruefte Kandidaten:
 *   - Interface-Record:  usb_cdc_single        (furi_hal_usb_cdc.h)
 *   - Callbacks setzen:  furi_hal_cdc_set_callbacks(if, CdcCallbacks*, ctx)
 *   - Empfangen:         furi_hal_cdc_receive(if, buf, max) -> size_t
 *   - Senden:            furi_hal_cdc_send(if, buf, len)
 * Falls dein Firmware-Zweig andere Signaturen hat: hier ist die einzige Stelle,
 * die angepasst werden muss. Der STK500-/LGT-Teil bleibt unveraendert.
 * ============================================================ */
#include "usb_isp.h"
#include "stk500.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_usb.h>
#include <furi_hal_usb_cdc.h>
#include <furi_hal_cdc.h>

#define CDC_IF 0

struct UsbIsp {
    FuriHalUsbInterface* usb_prev;
    FuriStreamBuffer* rx;
    FuriThread* worker;
    volatile bool run;
    volatile uint32_t activity;
};

/* --- CDC-Callbacks (RX laeuft im IRQ-Kontext -> nur in den Stream schieben) --- */
static void cdc_rx(void* ctx) {
    UsbIsp* u = ctx;
    uint8_t tmp[64];
    size_t n = furi_hal_cdc_receive(CDC_IF, tmp, sizeof(tmp));
    if(n) furi_stream_buffer_send(u->rx, tmp, n, 0);
}
static CdcCallbacks cdc_cb = {
    .tx_ep_callback = NULL,
    .rx_ep_callback = cdc_rx,
    .state_callback = NULL,
    .ctrl_line_callback = NULL,
    .config_callback = NULL,
};

/* --- STK500-I/O ueber CDC/Stream --- */
static bool io_get(void* ctx, uint8_t* b, uint32_t to) {
    UsbIsp* u = ctx;
    return furi_stream_buffer_receive(u->rx, b, 1, to) == 1;
}
static void io_send(void* ctx, const uint8_t* buf, size_t n) {
    (void)ctx;
    size_t off = 0;
    while(n) {
        size_t chunk = n > 64 ? 64 : n;            /* CDC-Bulk in 64-B-Haeppchen */
        furi_hal_cdc_send(CDC_IF, (uint8_t*)buf + off, chunk);
        off += chunk;
        n -= chunk;
    }
}
static void io_activity(void* ctx) {
    UsbIsp* u = ctx;
    u->activity++;
}

static int32_t usb_worker(void* ctx) {
    UsbIsp* u = ctx;
    Stk500Io io = {
        .ctx = u,
        .get = io_get,
        .send = io_send,
        .run = &u->run,
        .activity = io_activity,
    };
    stk500_run(&io);
    return 0;
}

UsbIsp* usb_isp_start(void) {
    UsbIsp* u = malloc(sizeof(UsbIsp));
    memset(u, 0, sizeof(UsbIsp));
    u->run = true;
    u->rx = furi_stream_buffer_alloc(2048, 1);

    u->usb_prev = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    furi_hal_usb_set_config(&usb_cdc_single, NULL);
    furi_hal_cdc_set_callbacks(CDC_IF, &cdc_cb, u);

    u->worker = furi_thread_alloc_ex("LgtStk500", 2048, usb_worker, u);
    furi_thread_start(u->worker);
    return u;
}

void usb_isp_stop(UsbIsp* u) {
    u->run = false;
    furi_thread_join(u->worker);
    furi_thread_free(u->worker);

    furi_hal_cdc_set_callbacks(CDC_IF, NULL, NULL);
    furi_hal_usb_set_config(u->usb_prev, NULL);

    furi_stream_buffer_free(u->rx);
    free(u);
}

uint32_t usb_isp_activity(UsbIsp* u) {
    return u->activity;
}
