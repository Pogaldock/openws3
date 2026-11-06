#include "cal.h"

uint16_t cal_port_mv(uint16_t raw)
{
    return (uint16_t)(((uint32_t)raw * 1125u) >> 10);          /* 4500/4096 */
}

uint16_t cal_port_ma(uint16_t raw)
{
    return (uint16_t)(((uint32_t)raw * 4000u + 136u) / 273u);   /* 1000/68.25 */
}

uint16_t cal_ma_to_port(uint16_t ma)
{
    return (uint16_t)((uint32_t)ma * 273u / 4000u);
}

uint16_t cal_vin_mv(uint16_t raw)
{
    /* (raw+1)*4/61 in 0.1 V  ->  mV = (raw+1) * 400 / 61 */
    return (uint16_t)((((uint32_t)raw + 1u) * 400u) / 61u);
}

uint16_t cal_set_to_ma(uint8_t units)
{
    return (uint16_t)((uint32_t)units * 100u / CUR_UNITS_PER_100MA);
}

uint8_t cal_cur_valid(uint8_t units)
{
    return units >= CUR_MIN && units <= CUR_MAX && (units - CUR_MIN) % CUR_UNITS_PER_100MA == 0;
}

uint8_t cal_ma_step_up(uint8_t units)
{
    uint16_t n = (uint16_t)units + CUR_UNITS_PER_100MA;
    if (units < CUR_MIN || n > CUR_MAX) return CUR_MIN;   /* wraps; also repairs an erased/garbage setting */
    return (uint8_t)n;
}

uint16_t cal_vcc_gain(uint16_t bg8)
{
    uint16_t g;
    if (bg8 < BG8_MIN) bg8 = BG8_MIN;
    if (bg8 > BG8_MAX) bg8 = BG8_MAX;
    g = (uint16_t)((((uint32_t)BG_NOM << 15) + bg8 / 2) / bg8);
    /* Only ever up (0.2-beta.6).  The internal reference varies from chip to chip (OpenWS3's author found it unreliable on some
       units; on the tested unit it reads 1.5 % high), while the raw readings against the board's regulated supply are accurate as they
       come.  A correction downwards would make a cell read lower than it is and charge it higher (4.41 V on the 4.35 V setting on
       the tested unit); upwards it only ever makes the charger stop earlier.  So: the raw readings, as OpenWS3 uses them, unless the
       reference says the supply has risen, which would make raw readings read low. */
    return g < 4096 ? 4096 : g;
}

uint16_t cal_vcc_fix(uint16_t raw, uint16_t gain)
{
    return (uint16_t)(((uint32_t)raw * gain + 2048) >> 12);
}
