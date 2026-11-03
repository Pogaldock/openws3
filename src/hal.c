/*
 * STC8H8K64U @ 40 MHz.  Init values for PWM/ADC/ports are those of a bench firmware that has run on this board.
 */
#include "hal.h"
#include "buzz.h"
#include "cfg.h"

__sfr __at (0x80) P0;   __sfr __at (0x90) P1;   __sfr __at (0xA0) P2;
__sfr __at (0xB0) P3;   __sfr __at (0xC0) P4;   __sfr __at (0xC8) P5;
__sfr __at (0x93) P0M1; __sfr __at (0x94) P0M0; __sfr __at (0x91) P1M1; __sfr __at (0x92) P1M0;
__sfr __at (0x95) P2M1; __sfr __at (0x96) P2M0; __sfr __at (0xB1) P3M1; __sfr __at (0xB2) P3M0;
__sfr __at (0xB3) P4M1; __sfr __at (0xB4) P4M0; __sfr __at (0xC9) P5M1; __sfr __at (0xCA) P5M0;
__sfr __at (0xBA) P_SW2;
__sfr __at (0x88) TCON; __sfr __at (0x89) TMOD; __sfr __at (0x8A) TL0; __sfr __at (0x8C) TH0;
__sfr __at (0xF4) USBCON;
__sfr __at (0x8E) AUXR; __sfr __at (0xA8) IE;
__sfr __at (0xD1) T4T3M; __sfr __at (0xD4) T3H; __sfr __at (0xD5) T3L; __sfr __at (0xAF) IE2;   /* Timer 3: the buzzer's notes */
__sfr __at (0xEF) AUXINTIF;                                   /* bit 1 T3IF: Timer 3 overflow pending */
__sfr __at (0xC1) WDT_CONTR;   /* STC8H manual 6.3.1: watchdog */
__sfr __at (0xFF) RSTCFG;      /* STC8H manual 6.3.3: reset configuration (low-voltage reset) */
__sfr __at (0x8B) TL1; __sfr __at (0x8D) TH1;   /* Timer 1 is not used: its registers carry the RAM copy of the reference (below) */
__sfr __at (0xC2) IAP_DATA; __sfr __at (0xC3) IAP_ADDRH; __sfr __at (0xC4) IAP_ADDRL;
__sfr __at (0xC5) IAP_CMD;  __sfr __at (0xC6) IAP_TRIG;  __sfr __at (0xC7) IAP_CONTR; __sfr __at (0xF5) IAP_TPS;
__sfr __at (0xBC) ADC_CONTR; __sfr __at (0xBD) ADC_RES; __sfr __at (0xBE) ADC_RESL; __sfr __at (0xDE) ADCCFG;

__sbit __at (0xC5) EN_P1;    /* P4.5 */
__sbit __at (0xA7) EN_P2;    /* P2.7 */
__sbit __at (0xA5) EN_P3;    /* P2.5 */
__sbit __at (0xC2) EN_P4;    /* P4.2 */
__sbit __at (0xC3) EN_P5;    /* P4.3 */
__sbit __at (0xC4) EN_P6;    /* P4.4 */
__sbit __at (0xCA) GLOBAL_EN;/* P5.2 */
__sbit __at (0xC7) POWER_EN; /* P4.7 */
__sbit __at (0xCB) PROFILE;  /* P5.3 */
__sfr __at (0x87) PCON;
__sfr __at (0x81) SP;
__sbit __at (0xC6) TONE;     /* P4.6 */
__sbit __at (0x87) FAN;      /* P0.7 */
__sbit __at (0xB2) BTN1;     /* P3.2 */
__sbit __at (0xB6) BTN2;     /* P3.6 */
__sbit __at (0xB4) STRAP;    /* P3.4 */

#define XB(a)  (*(volatile uint8_t __xdata *)(a))
#define EAXFR  0x80

static const __code uint16_t ccr_addr[NPORTS] = PORT_CCR_INIT;
static volatile uint32_t __xdata g_ticks;       /* 0.5 ms ticks since boot */

/* STARTUP RULE: no initialised XRAM variables anywhere.  SDCC's startup copies them with `movx @r0` and uses P2 as
 * the high address byte, finishing with P2 = 0xFF - which would drive every PWM pin and two port enables high.
 * The build fails on any violation (Makefile: the map must show an empty XINIT).
 *
 * Runs before the C runtime: drive every power-path pin low while they are still quasi-bidirectional
 * (a strong 0 even before the port mode registers are written).  */
static uint8_t XD profile_req;  /* what the firmware asked for: the pin follows it once the conditions below allow */
static uint8_t XD byp_ok;       /* 0 while the input guard (adc.c) holds the bypass off */
static uint8_t XD profile_ok;  /* 1 once the settings are loaded (hal_profile_allow): until then the input bypass cannot be turned on */

static void pin_modes(void)
{
    P0M1 = 0x7F; P0M0 = 0x80;  P1M1 = 0xFF; P1M0 = 0x00;
    P2M1 = 0x00; P2M0 = 0xFF;  P3M1 = 0xB3; P3M0 = 0xA8;
    P4M1 = 0x02; P4M0 = 0xFE;  P5M1 = 0x13; P5M0 = 0x0F;
}

unsigned char __sdcc_external_startup(void)
{
    /* The STC8H's loader leaves the chip's measured internal reference (mV, high byte first) at idata EFH-F0H (manual 7.3, 7.5.3).  The
       C start-up clears the internal RAM after this function, so the two bytes are parked in Timer 1's registers (Timer 1 is not used)
       for hal_ram_bgv().  Read only, before anything else touches that RAM. */
    TH1 = *(__idata uint8_t *)0xEF;
    TL1 = *(__idata uint8_t *)0xF0;
    profile_ok = 0;            /* not left to whatever the RAM holds */
    byp_ok = 0; profile_req = 0;
    P2 = 0x00;                 /* PWM pins + two enables low */
    P4 = 0x02;                 /* enables, beep, power low; LCD CS (P4.1) idle high */
    P5 = 0x03;                 /* P5.2/P5.3 low; LCD RST/DC idle high */
    P0 = 0x00;                 /* P0.7 low (P0.0-6 are analog inputs) */
    P3 &= ~0x08;               /* P3.3 rail actuator low */
    /* The STC8H's pins are high-impedance after power-on (manual 2.5.1, 9: "All I/O ports except for P3.0 and P3.1 are in high-impedance
       input state after power-on"), so the latches above drive nothing until the port modes are set.  Set them here, the first thing
       the program does, so the power-path pins float for as short a time as the chip allows (hal_init sets the same values again). */
    pin_modes();
    return 0;
}

static void pwm_init(void)
{
    P_SW2 |= EAXFR;
    XB(0xFECC) = 0; XB(0xFECD) = 0;
    XB(0xFEC8) = 0x78; XB(0xFEC9) = 0x78; XB(0xFECA) = 0x78; XB(0xFECB) = 0x78;
    XB(0xFECC) = 0x33; XB(0xFECD) = 0x33;
    XB(0xFEEC) = 0; XB(0xFEED) = 0;
    XB(0xFEE8) = 0x78; XB(0xFEE9) = 0x78; XB(0xFEEA) = 0x78; XB(0xFEEB) = 0x78;
    XB(0xFEEC) = 0x33; XB(0xFEED) = 0x33;
    XB(0xFED2) = PWM_PERIOD >> 8; XB(0xFED3) = PWM_PERIOD & 0xFF;      /* PWMA_ARR */
    XB(0xFEF2) = PWM_PERIOD >> 8; XB(0xFEF3) = PWM_PERIOD & 0xFF;      /* PWMB_ARR */
    XB(0xFED0) = 0; XB(0xFED1) = 0; XB(0xFEF0) = 0; XB(0xFEF1) = 0;    /* prescalers */
    XB(0xFEB1) = 0x54;                                                  /* PWMA_ENO: PWM2P/3P/4P */
    XB(0xFEB5) = 0x55;                                                  /* PWMB_ENO: PWM5..8 */
    XB(0xFEB2) = 0x54;                                                  /* PWMA_PS */
    XB(0xFEB6) = 0x10;                                                  /* PWMB_PS: PWM7 on P3.3 */
    XB(0xFEDD) = 0x80; XB(0xFEFD) = 0x80;                               /* BKR: main output enable */
    XB(0xFEC0) |= 0x01; XB(0xFEE0) |= 0x01;                             /* CR1: counters on */
}

/* Stack use: the stack grows up from the end of the data in internal RAM to 0xFF.  Everything above the stack pointer is painted at
 * boot; the painted bytes still left at the top are what the deepest call (with interrupts on top) has never reached. */
#define STACK_PAINT 0xA5

static void stack_paint(void)
{
    uint8_t a = (uint8_t)(SP + 1);
    do { *(__idata uint8_t *)a = STACK_PAINT; } while (++a);
}

uint8_t hal_stack_free(void)
{
    uint8_t a = 0xFF, n = 0, sp = SP;
    while (a > sp && *(__idata uint8_t *)a == STACK_PAINT) { a--; n++; }
    return n;
}

void hal_init(void)
{
    byp_ok = 0; profile_req = 0;   /* bypass held off until the input guard has run once (adc_prime) */
    pin_modes();

    pwm_init();
    hal_all_off();
    stack_paint();             /* interrupts are still off */

    XB(0xFEA8) = 0x3F;         /* ADC sample timing */
    ADCCFG = 0x2A;             /* right-aligned result, speed 0xA */
    ADC_CONTR = 0x80;          /* ADC power on */

    AUXR |= 0x80;              /* Timer0 1T */
    TMOD &= 0xF0;              /* mode 0: 16-bit auto-reload */
    TL0 = (65536UL - F_SYS / TICK_HZ) & 0xFF;
    TH0 = (65536UL - F_SYS / TICK_HZ) >> 8;
    TCON |= 0x10;              /* TR0 */
    IE |= 0x02;                /* ET0 */

    T4T3M &= 0xF0;             /* Timer 3 stopped, timer mode, 12T clock (3.333 MHz), no clock output pin: main.c toggles P4.6 */
    IE2 |= 0x20;               /* ET3: its interrupt (vector 19, same low priority as the others) */

    /* Low-voltage reset (manual 6.3.3, RSTCFG): ENLVR (bit 6) = reset, not just an interrupt; LVDS = 11 (3.0 V).  Its reset value is
       off at 2.0 V: a sagging supply would leave the chip running out of spec instead of resetting.  P54RST (bit 4) stays 0. */
    RSTCFG = 0x43;
    hal_wdt_feed();            /* the watchdog runs from here on (hal.h) */
}

/* Watchdog (manual 6.3.1, WDT_CONTR): EN_WDT (bit 5), CLR_WDT (bit 4), WDT_PS = 111 (divide by 256).  Overflow time
   12 x 32768 x 2^(WDT_PS + 1) / SYSclk (the manual's table: 65.5 ms at 12 MHz for WDT_PS = 000) = 2.5 s at 40 MHz.  Once on, only a
   reset stops it; a software reset (into the ISP loader) returns every register but the clock ones to its initial value, so the
   loader runs without it.  A watchdog reset starts the chip through the ISP loader, then this firmware boots afresh: nothing charges. */
void hal_wdt_feed(void) { WDT_CONTR = 0x37; }

uint16_t hal_ram_bgv(void) { return ((uint16_t)TH1 << 8) | TL1; }

void hal_irq_on(void) { IE |= 0x80; }

void hal_tick(void) { g_ticks++; }

uint32_t hal_ticks(void)
{
    uint32_t t;
    uint8_t ea = IE & 0x80;    /* interrupts back to how they were, not always on: a caller that had them off keeps them off */
    IE &= ~0x80;
    t = g_ticks;
    IE |= ea;
    return t;
}

uint32_t hal_ms(void) { return hal_ticks() >> 1; }       /* TICK_HZ = 2000 */

void hal_delay_ms(uint16_t ms)
{
    uint32_t t0 = hal_ms();
    while ((uint16_t)(hal_ms() - t0) < ms);
}

void hal_pwm(uint8_t port, uint16_t duty)
{
    uint16_t a;
    if (port >= NPORTS) return;
    if (duty > PWM_PERIOD) duty = PWM_PERIOD;
    a = ccr_addr[port];
    XB(a) = duty >> 8; XB(a + 1) = duty & 0xFF;
}

void hal_rail(uint16_t duty)
{
    if (duty > PWM_PERIOD) duty = PWM_PERIOD;
    XB(RAIL_CCR) = duty >> 8; XB(RAIL_CCR + 1) = duty & 0xFF;
}

void hal_sw(uint8_t port, uint8_t on)
{
    switch (port) {
    case 0: EN_P1 = on; break;
    case 1: EN_P2 = on; break;
    case 2: EN_P3 = on; break;
    case 3: EN_P4 = on; break;
    case 4: EN_P5 = on; break;
    case 5: EN_P6 = on; break;
    }
}

void hal_global(uint8_t on) { GLOBAL_EN = on; }
void hal_power(uint8_t on)  { POWER_EN = on; }
/* The one place the input bypass pin is driven.  With DISABLE USB POWER set (cfg.h) it is never on, whoever asks: the boot, developer mode,
   the host. And not before the settings are loaded, so no ordering of the start-up can turn it on first. The bypass conducts backwards, so
   a supply on the XT60 would be fed into the USB-C connector (HARDWARE.md). */
void hal_profile_allow(void) { profile_ok = 1; }
/* Also never while the input guard (adc.c) has seen a second supply arrive on the XT60 / DC jack: the bypass would feed it backwards
   into whatever is on the USB-C connector.  The diode under the bypass blocks that path on its own. */
static void profile_apply(void) { PROFILE = profile_req && profile_ok && !settings.usb_off && byp_ok; }
void hal_profile(uint8_t on){ profile_req = on ? 1 : 0; profile_apply(); }
void hal_bypass_ok(uint8_t lo) { byp_ok = lo ? 1 : 0; profile_apply(); }
uint8_t hal_profile_on(void) { return PROFILE; }

void hal_all_off(void)
{
    uint8_t i;
    GLOBAL_EN = 0;
    for (i = 0; i < NPORTS; i++) { hal_sw(i, 0); hal_pwm(i, 0); }
    hal_rail(PWM_PERIOD);       /* the rail actuator is inverted (higher duty = lower rail): full duty is the LOWEST rail.  0 was the highest */
}

uint16_t hal_adc(uint8_t ch)
{
    ADC_RES = 0; ADC_RESL = 0;
    ADC_CONTR = (ADC_CONTR & 0xE0) | (ch & 0x0F);
    ADC_CONTR |= 0x40;                 /* start */
    __asm
        nop
        nop
        nop
        nop
    __endasm;
    {
        uint16_t n = 0;                /* a conversion takes a few us; one that never ends (a hardware fault) must not hang the firmware */
        while (!(ADC_CONTR & 0x20)) if (++n == 0) return 4095;   /* full scale: the rail reads saturated, the supervision locks out */
    }
    ADC_CONTR &= ~0x20;
    return ((uint16_t)ADC_RES << 8) | ADC_RESL;
}

uint8_t hal_chip_byte(uint8_t n)
{
    P_SW2 |= EAXFR;                    /* the CHIPID block is an extended SFR area (also set by pwm_init) */
    return XB(0xFDE0 + (n & 0x1F));
}

void hal_reboot_isp(void)
{
    static volatile uint16_t XD d;
    hal_all_off();
    IE &= 0x7F;
    USBCON = 0;                        /* drop the D+ pull-up: the host sees a disconnect, the loader enumerates afresh */
    for (d = 0; d < 60000u; d++) __asm nop __endasm;
    IAP_CONTR = 0x60;                  /* SWBS=1, SWRST=1: reset and start in the ISP area */
    for (;;) ;
}

static void iap(uint8_t cmd, uint16_t addr, uint8_t v)
{
    IAP_CONTR = 0x80;                  /* enable, fastest wait */
    IAP_TPS = 0x28;
    IAP_CMD = cmd;
    IAP_ADDRH = addr >> 8;
    IAP_ADDRL = addr & 0xFF;
    IAP_DATA = v;
    IAP_TRIG = 0x5A;
    IAP_TRIG = 0xA5;
    __asm
        nop
    __endasm;
}

static void iap_idle(void)             /* leave the IAP interface idle */
{
    IAP_CONTR = 0; IAP_CMD = 0; IAP_TRIG = 0;
    IAP_ADDRH = 0x80; IAP_ADDRL = 0;
}

uint8_t hal_ee_read(uint16_t addr)
{
    uint8_t v;
    iap(1, addr, 0);
    v = IAP_DATA;
    iap_idle();
    return v;
}

void hal_ee_erase(uint16_t addr) { iap(3, addr, 0); iap_idle(); }
void hal_ee_write(uint16_t addr, uint8_t v) { iap(2, addr, v); iap_idle(); }

/* From the 0.5 ms interrupt only: TONE_OFF stops Timer 3 and leaves the pin low; a note sets its half period and runs it (each
   overflow toggles the pin in timer3_isr, main.c).  Written while running, T3H/T3L load the next reload value. */
void hal_tone_note(uint8_t cmd)
{
    static uint16_t XD n;
    if (cmd == TONE_OFF) {
        /* timer stopped, its pending flag cleared (an overflow that came while this interrupt ran would otherwise toggle the pin once
           more after this, leaving it high until the next note), then the pin low */
        T4T3M &= (uint8_t)~0x08; AUXINTIF &= (uint8_t)~0x02; TONE = 0;
        return;
    }
    n = (uint16_t)(0u - buzz_note_count((uint8_t)(cmd - 1)));
    T3H = (uint8_t)(n >> 8); T3L = (uint8_t)n;
    T4T3M |= 0x08;             /* T3R */
}
void hal_fan(uint8_t on)      { FAN = on; }
uint8_t hal_btn(uint8_t n)    { return n == 1 ? !BTN1 : !BTN2; }   /* the timer interrupt's: not reentrant (its argument sits in XRAM) */
uint8_t hal_btns(void)        { return (uint8_t)((!BTN1 ? 1 : 0) | (!BTN2 ? 2 : 0)); }   /* main-loop callers: no argument, safe beside the interrupt */
uint8_t hal_strap(void)       { return STRAP; }
