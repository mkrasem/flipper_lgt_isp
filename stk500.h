/* STK500v1-Slave (I/O-abstrahiert). Uebersetzt avrdude-Kommandos auf LGT-SWD. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    void* ctx;
    bool (*get)(void* ctx, uint8_t* b, uint32_t timeout_ms);  /* 1 Byte holen; false = Timeout */
    void (*send)(void* ctx, const uint8_t* buf, size_t n);    /* Bytes senden */
    volatile bool* run;                                       /* false -> Schleife beenden */
    void (*activity)(void* ctx);                              /* optional: Aktivitaet melden */
} Stk500Io;

/* Laeuft bis *io->run == false. Meldet dem PC eine 328P-Signatur (1E 95 0F). */
void stk500_run(Stk500Io* io);
