/* The one-command slot between the USB interrupt (usb.c) and the main loop (app.c): pure, so the same code runs in the firmware,
 * in the whole-firmware simulation and in the unit tests (REQUIREMENTS.md SR-C4).
 * Rules: one command waits at a time; a report that arrives while one is waiting is dropped, unacknowledged (the first one wins,
 * so a stop already waiting is never replaced; the host resends what is not acknowledged, USB.md); the bytes a short report
 * leaves out read 0. */
#ifndef USBQ_H
#define USBQ_H
#include "compat.h"
#include "usb.h"

uint8_t XD *usbq_open(void);               /* interrupt: a report arrived; the slot (USB_CMD bytes, zeroed) to copy it into, then
                                              usbq_close; 0 = one is waiting, drop this one */
void    usbq_close(void);                  /* interrupt: the report is complete: it waits for the main loop */
uint8_t usbq_take(uint8_t *cmd);           /* main loop, with the USB interrupt masked: the waiting command into cmd; 0 if none */
void    usbq_reset(void);

#endif
