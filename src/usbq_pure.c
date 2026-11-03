/* the one-command USB slot (usbq.h).  Runs inside the USB interrupt: no compiler helpers here (tools/imgcheck.py checks it). */
#include "usbq.h"

static uint8_t XD q[USB_CMD];
static volatile uint8_t XD full;

void usbq_reset(void) { full = 0; }

uint8_t XD *usbq_open(void)
{
    uint8_t i;
    if (full) return 0;                    /* one is waiting: the first one wins, this one is dropped */
    for (i = 0; i < USB_CMD; i++) q[i] = 0;
    return q;
}

void usbq_close(void) { full = 1; }

uint8_t usbq_take(uint8_t *cmd)
{
    uint8_t i;
    if (!full) return 0;
    for (i = 0; i < USB_CMD; i++) cmd[i] = q[i];
    full = 0;
    return 1;
}
