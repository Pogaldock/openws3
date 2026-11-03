/*
 * Hardware layer — the ONLY code that touches SFRs/XFRs.  Port numbers are 0..5 = physical ports 1..6.
 * Everything defaults to the safe state: all enables low, all PWM 0, rail actuator 0.
 */
#ifndef HAL_H
#define HAL_H
#include "compat.h"
#include "board.h"

void     hal_init(void);                    /* port modes, PWM, ADC, 0.5 ms tick; interrupts stay off */
void     hal_irq_on(void);
uint8_t  hal_stack_free(void);              /* bytes at the top of the stack never used since boot (painted in hal_init) */

void     hal_pwm(uint8_t port, uint16_t duty);   /* per-port current control, 0..PWM_PERIOD */
void     hal_rail(uint16_t duty);                /* rail actuator (P3.3), 0..PWM_PERIOD */
void     hal_sw(uint8_t port, uint8_t on);       /* per-port switch pin: 0 = charge path, 1 = bleed path (see board.h) */
void     hal_global(uint8_t on);                 /* P5.2 */
void     hal_power(uint8_t on);                  /* P4.7 */
void     hal_profile(uint8_t on);                /* P5.3: never on before hal_profile_allow() or with DISABLE USB POWER set */
void     hal_profile_allow(void);                /* the settings are loaded: cfg_load calls it */
void     hal_bypass_ok(uint8_t ok);              /* adc.c input guard: 0 holds the bypass off whatever is requested */
uint8_t  hal_profile_on(void);                   /* the pin as driven */
void     hal_reboot_isp(void);                   /* outputs off, then a software reset into the USB ISP loader; does not return */
void     hal_all_off(void);                      /* every enable low, every duty 0, P5.2 low */

uint16_t hal_adc(uint8_t ch);                    /* one 12-bit conversion */
/* One byte of the chip's read-only CHIPID block (STC8H manual 7.4: FDE0H + n, n = 0..31).  Only some chips have it (STC8H8K64U
 * version B, not A): byte 31 = 5AH and byte 25 = 00H when it is there.  Bytes 7..8 = the factory-measured internal reference (mV, high
 * byte first), 27..29 = the chip's test date (BCD year, month, day).  Read only, for the diagnostics. */
uint8_t  hal_chip_byte(uint8_t n);
/* The chip's measured internal reference as its loader left it in RAM (idata EFH-F0H, mV), read at the first instruction of start-up:
 * the way a chip without the CHIPID block (version A) gives it.  Not validated: shown on the diagnostics, not used for any decision. */
uint16_t hal_ram_bgv(void);
/* Watchdog: started at the end of hal_init (2.5 s); feed it at least that often: every control pass, and between the steps of the boot */
void     hal_wdt_feed(void);

/* Settings flash (IAP): one page, see cfg.h. */
uint8_t  hal_ee_read(uint16_t addr);
void     hal_ee_erase(uint16_t addr);            /* erases the 512-byte sector containing addr */
void     hal_ee_write(uint16_t addr, uint8_t v); /* program one byte (sector must have been erased) */
uint32_t hal_ms(void);                           /* ms since boot, atomic read */
uint32_t hal_ticks(void);                        /* 0.5 ms ticks since boot, atomic read */
void     hal_delay_ms(uint16_t ms);              /* busy wait on the tick; interrupts must be on */


void     hal_tick(void);                         /* called from the 0.5 ms interrupt (defined in main.c: the vector must live with main()) */
void     hal_tone_note(uint8_t cmd);             /* P4.6 piezo: buzz_tick's command (buzz.h), from the 0.5 ms interrupt only */
void     hal_fan(uint8_t on);                    /* P0.7 */
uint8_t  hal_btn(uint8_t n);                     /* 1 = button 1 (P3.2) pressed, 2 = button 2 (P3.6); active low pins */
uint8_t  hal_btns(void);                         /* both buttons, b0 = B1, b1 = B2 (pressed = 1), for the main loop */
uint8_t  hal_strap(void);                        /* P3.4 */

#endif
