/* printf over the simulated 8051's UART for s51, and a clean stop */
#include <stdint.h>
__sfr __at (0x98) SCON; __sfr __at (0x99) SBUF; __sfr __at (0x89) TMOD; __sfr __at (0x8D) TH1; __sfr __at (0x88) TCON;
__sbit __at (0x99) TI;
static char ready;
int putchar(int c)
{
    if (!ready) { SCON = 0x50; TMOD = (TMOD & 0x0F) | 0x20; TH1 = 0xFD; TCON |= 0x40; TI = 1; ready = 1; }
    while (!TI) ;
    TI = 0; SBUF = (char)c;
    return c;
}
