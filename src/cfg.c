#include "cfg.h"
#include "hal.h"

#define BYTES ((uint8_t XD *)&settings)
#define SUM_ADDR (CFG_ADDR + 1 + sizeof settings)     /* after the fields: a check byte over them */

static uint8_t XD cfg_sum;                          /* XRAM: the internal RAM is full */

static void sum_bytes(void) { uint8_t k; cfg_sum = 0x5A; for (k = 0; k < sizeof settings; k++) cfg_sum = (uint8_t)((cfg_sum << 1 | cfg_sum >> 7) ^ BYTES[k]); }

void cfg_load(void)
{
    uint8_t k, ok = hal_ee_read(CFG_ADDR) == CFG_LAYOUT;
    for (k = 0; k < sizeof settings; k++) BYTES[k] = ok ? hal_ee_read(CFG_ADDR + 1 + k) : 0xFF;
    sum_bytes();
    if (ok && hal_ee_read(SUM_ADDR) != cfg_sum) for (k = 0; k < sizeof settings; k++) BYTES[k] = 0xFF;   /* a damaged page: the defaults */
    cfg_validate();
    hal_profile_allow();                          /* the input bypass may follow the settings from here on */
}

void cfg_save(void)
{
    uint8_t k, same = hal_ee_read(CFG_ADDR) == CFG_LAYOUT;
    cfg_validate();
    for (k = 0; k < sizeof settings && same; k++) same = hal_ee_read(CFG_ADDR + 1 + k) == BYTES[k];
    sum_bytes();
    if (same && hal_ee_read(SUM_ADDR) == cfg_sum) return;   /* spare the flash */
    hal_ee_erase(CFG_ADDR);
    hal_delay_ms(6);                              /* wait ~6 ms after an erase */
    for (k = 0; k < sizeof settings; k++) hal_ee_write(CFG_ADDR + 1 + k, BYTES[k]);
    hal_ee_write(SUM_ADDR, cfg_sum);
    hal_ee_write(CFG_ADDR, CFG_LAYOUT);           /* last: a write cut short leaves no valid page */
}
