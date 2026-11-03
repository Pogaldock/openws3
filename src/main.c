/*
 * WhoopStor V3 firmware entry point: boot, then one control pass every PASS_TICKS.
 */
#include "hal.h"
#include "app.h"
#include "input.h"
#include "buzz.h"
#include "usb.h"

/* 0.5 ms tick (20000 clocks at 40 MHz).  The vector must be compiled together with main(). */
void timer0_isr(void) __interrupt(1)
{
    hal_tick();
    input_tick(hal_btn(1), hal_btn(2));
    {
        uint8_t c = buzz_tick();
        if (c != TONE_KEEP) hal_tone_note(c);
    }
}

/* Timer 3 (interrupt 19): one half period of the note playing.  Toggles the buzzer pin P4.6 (bit address C6H) and clears the
   interrupt flag T3IF (AUXINTIF EFH, bit 1), whether or not the hardware already did.  Uses no register and changes no flag, so it
   saves nothing: about 2 microseconds per toggle, at most 8000 per second. */
void timer3_isr(void) __interrupt(19) __naked
{
    __asm
        cpl     0xC6
        anl     0xEF, #0xFD
        reti
    __endasm;
}

/* USB device (interrupt 25).  Moves bytes between the SIE and two buffers only; see usb.h. */
void usb_vector(void) __interrupt(25)
{
    usb_isr();
}

void main(void)
{
    hal_init();               /* __sdcc_external_startup already drove the power path low */
    usb_init();
    app_boot();
    for (;;) app_pass();
}
