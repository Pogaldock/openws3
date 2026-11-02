/* Renders what the charger's screen shows, for the user guide: the firmware's own screen code (ui.c, menu.c, portset.c, dbgscr.c)
 * runs on the PC against a pretend panel, and each screen is written as a PPM file.  tools/render_screens.py turns them into PNGs.
 *   build/render_screens OUTDIR */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ui.h"
#include "lcd.h"
#include "font.h"
#include "theme.h"
#include "menu.h"
#include "portset.h"
#include "input.h"
#include "dbgscr.h"
#include "cfg.h"
#include "cal.h"
#include "sup.h"
#include "limits.h"
#include "layout.h"

/* ---- the pretend panel ---- */
static uint16_t fb[LCD_PX_H][LCD_PX_W];

void lcd_fill(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color)
{
    unsigned i, j;
    for (j = y; j < (unsigned)y + h && j < LCD_PX_H; j++)
        for (i = x; i < (unsigned)x + w && i < LCD_PX_W; i++) fb[j][i] = color;
}

void lcd_text(uint8_t x, uint8_t y, const char *s, uint8_t sx, uint8_t sy, uint16_t fg, uint16_t bg)
{
    for (; *s; s++, x += FONT_W * sx) {
        uint8_t c = (uint8_t)*s, r, i;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (c < FONT_FIRST || c > FONT_LAST) c = ' ';
        for (r = 0; r < FONT_H_PX * sy; r++)
            for (i = 0; i < FONT_W * sx; i++) {
                uint8_t col = i / sx, k = r / sy;
                int on = col < 5 && k < 7 && (((th_get() == TH_ROT ? font_rot : font5x7)[(c - FONT_FIRST) * 5 + col] >> k) & 1);
                if (on && th_get() == TH_ROT && sx > 1 && sy > 1 && !LCD_POKE(r % sy, i % sx)) on = 0;
                if (x + i < LCD_PX_W && y + r < LCD_PX_H) fb[y + r][x + i] = on ? fg : bg;
            }
    }
}

void lcd_noise(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t seed)
{
    unsigned i, j;
    uint16_t s = seed ? seed : 1;
    for (j = y; j < (unsigned)y + h; j++)
        for (i = x; i < (unsigned)x + w; i++) { s = LCD_LFSR(s); if (i < LCD_PX_W && j < LCD_PX_H) fb[j][i] = LCD_STATIC(s); }
}

void lcd_bitmap(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *bits, uint16_t fg, uint16_t bg)
{
    unsigned i, j, stride = (w + 7) / 8;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < LCD_PX_W && y + j < LCD_PX_H) fb[y + j][x + i] = (bits[j * stride + i / 8] & (0x80 >> (i % 8))) ? fg : bg;
}

void lcd_noise_row(uint8_t y, uint16_t seed, const uint8_t *bits, uint8_t bx, uint8_t bw, uint8_t keep, uint16_t fg)
{
    unsigned i;
    uint16_t s = seed ? seed : 1, c;
    for (i = 0; i < LCD_PX_W; i++) {
        s = LCD_LFSR(s);
        c = LCD_STATIC(s);
        if (bits && i >= bx && i - bx < bw && LCD_SURFACE(bits, i - bx, s, keep)) c = fg;
        if (y < LCD_PX_H) fb[y][i] = c;
    }
}

static const char *outdir;

static void save(const char *name)
{
    char path[512];
    FILE *f;
    int x, y;
    snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", LCD_PX_W, LCD_PX_H);
    for (y = 0; y < LCD_PX_H; y++)
        for (x = 0; x < LCD_PX_W; x++) {
            uint16_t c = fb[y][x];
            fputc(((c >> 11) & 31) * 255 / 31, f); fputc(((c >> 5) & 63) * 255 / 63, f); fputc((c & 31) * 255 / 31, f);
        }
    fclose(f);
}

/* ---- readings in the units the screens take ---- */
static uint16_t vu(unsigned mv)  { uint16_t v = 0; while (v < 4095 && cal_port_mv(v) < mv) v++; return v; }
static uint8_t  iu(unsigned ma)  { uint8_t i = 0; while (i < 255 && cal_port_ma(i) < ma) i++; return i; }
static uint16_t vin(unsigned mv) { uint16_t v = 0; while (v < 4095 && cal_vin_mv(v) < mv) v++; return v; }

/* ---- the main screen ---- */
static ui_state_t base(void)
{
    ui_state_t s;
    uint8_t k;
    memset(&s, 0, sizeof s);
    s.volt = settings.volt; s.iset = settings.cur; s.vin = vin(5000); s.tgt = sup_target(settings.volt);
    for (k = 0; k < NPORTS; k++) { s.set[k] = PV_AUTO; s.want[k] = settings.cur; }
    return s;
}

static void cell(ui_state_t *s, uint8_t k, uint8_t state, unsigned mv, unsigned ma)
{
    s->state[k] = state; s->v[k] = vu(mv); s->i[k] = iu(ma);
}

static void draw_main(const ui_state_t *s)
{
    uint8_t i;
    ui_main_init();
    for (i = 0; i < 3 + NPORTS * 3 + 3; i++) ui_main_step(s);
}

static ui_state_t idle_state(void)
{
    ui_state_t s = base();
    cell(&s, 0, PS_IDLE, 3620, 0); cell(&s, 1, PS_IDLE, 3910, 0); cell(&s, 2, PS_IDLE, 0, 0);
    cell(&s, 3, PS_IDLE, 4050, 0); cell(&s, 4, PS_IDLE, 3480, 0); cell(&s, 5, PS_IDLE, 0, 0);
    return s;
}

static ui_state_t charging_state(void)
{
    ui_state_t s = base();
    s.run = 1;
    cell(&s, 0, PS_CHARGE, 3780, 600); s.want[0] = iu(600);
    cell(&s, 1, PS_CHARGE, 4090, 300); s.want[1] = iu(600);
    cell(&s, 2, PS_IDLE, 0, 0);
    cell(&s, 3, PS_FULL, 4200, 0);
    cell(&s, 4, PS_REST, 4150, 0);
    cell(&s, 5, PS_CHARGE, 3650, 600); s.want[5] = iu(600);
    return s;
}

static void themed(const char *name, uint8_t theme)
{
    ui_state_t s = charging_state();
    char n[64];
    th_set(theme);
    draw_main(&s);
    snprintf(n, sizeof n, "main_%s", name);
    save(n);
    th_set(TH_CLASSIC);
}

/* ---- the text screens ---- */
static void draw_menu(void)
{
    uint8_t r;
    ui_text_enter();
    for (r = 0; r < MENU_ROWS; r++) {
        const char *t = menu_text(r);
        ui_text_row(r, t, t[0] == '>' ? th(R_GOOD) : t[0] == '*' ? th(R_WARN) : r == 0 ? th(R_INFO) : r == MENU_ROWS - 1 ? th(R_DIM) : th(R_TEXT));
    }
}

static void keys(int n, uint8_t ev) { while (n--) menu_event(ev); }

static void menu_screens(void)
{
    menu_open(0);
    draw_menu(); save("menu_categories");
    keys(1, EV_OK);                                                  /* CHARGING */
    draw_menu(); save("menu_charging");
    keys(1, EV_BACK); keys(1, EV_B2); keys(1, EV_OK);                /* LIMITS */
    draw_menu(); save("menu_limits");
    keys(1, EV_B2); keys(1, EV_OK);                                  /* editing MIN IN VOLTS */
    draw_menu(); save("menu_edit");
    keys(1, EV_BACK); keys(1, EV_BACK); keys(1, EV_B2); keys(1, EV_OK);   /* PREFERENCES */
    draw_menu(); save("menu_preferences");

    menu_open(1);                                                    /* charging: voltage and current are locked */
    keys(1, EV_OK); keys(1, EV_OK);
    draw_menu(); save("menu_locked");
}

static void diag_screen(void)
{
    uint8_t r;
    memset(&dbg_view, 0, sizeof dbg_view);
    dbg_view.held[0] = 0; dbg_view.held[1] = 1; dbg_view.held[2] = 2; dbg_view.stop = STOP_NONE;
    ui_text_enter();
    for (r = 0; r < DBG_ROWS; r++) ui_text_row(r, dbg_text(6, r), r == 0 ? th(R_INFO) : th(R_TEXT));
    save("diag_limits");
}

/* ---- port settings ---- */
static void ports_screens(void)
{
    ui_state_t s = idle_state();
    uint8_t k;
    portset_open(0);
    for (k = 0; k < NPORTS; k++) { s.set[k] = PV_AUTO; s.want[k] = settings.cur; }
    s.set[1] = iu(600); s.want[1] = iu(600);
    s.set[4] = PV_OFF;
    s.ps = 1; s.sel = 1;
    draw_main(&s); save("ports_select");
    s.ps = 2; s.blink = 1;
    draw_main(&s); save("ports_edit");
    s = charging_state();
    s.ps = 1; s.sel = 0; s.locked = 1;
    draw_main(&s); save("ports_locked");
}

/* ---- the ROT theme: every screen it draws differently ---- */
static void rot_screens(void)
{
    ui_state_t s;
    th_set(TH_ROT);
    {
        uint8_t n;
        char nm[32];
        int anim = getenv("ROT_ANIM") != 0;
        ui_splash();
        for (n = 0; n < UI_SPLASH_FRAMES; n++) {
            ui_splash_frame(n);
            if (anim) { snprintf(nm, sizeof nm, "anim_splash_%02u", n); save(nm); }
        }
        save("rot_splash");
        if (anim) {
            unsigned k;
            ui_state_t a = charging_state();
            draw_main(&a);
            for (k = 0; k < 48; k++) {                 /* 64 control passes (~0.3 s) per frame */
                unsigned i;
                if (k == 14) { a.state[5] = PS_FULL; a.v[5] = a.v[3]; a.i[5] = 0; }      /* port 6 finishes: static burst */
                if (k == 30) { a.state[1] = PS_FAULT; }                                  /* port 2 faults: blood cell, blinking skull */
                a.blink = (k / 3) & 1;
                for (i = 0; i < 64; i++) {
                    if (i == 40 && (k % 3) == 0) { a.v[0] += 10; a.v[4 + (k & 1)] += 0; a.v[5] += k < 14 ? 10 : 0; }   /* rising cells pulse */
                    ui_main_step(&a);
                }
                snprintf(nm, sizeof nm, "anim_main_%02u", k); save(nm);
            }
            a = idle_state();                              /* nothing plugged in: the screensaver after about 3 minutes */
            for (k = 0; k < NPORTS; k++) { a.v[k] = 0; a.state[k] = PS_IDLE; }
            draw_main(&a);
            for (k = 0; k < 40000u; k++) ui_main_step(&a);
            for (k = 0; k < 64; k++) {
                unsigned i;
                for (i = 0; i < 64; i++) ui_main_step(&a);
                snprintf(nm, sizeof nm, "anim_saver_%02u", k); save(nm);
            }
        }
    }
    ui_message(R_WAIT, "INPUT LOW", "CHECK THE SUPPLY", "4.52V", 0); save("rot_input_low");
    s = idle_state(); draw_main(&s); save("rot_idle");
    s = charging_state(); draw_main(&s); save("rot_charging");
    s.state[2] = PS_FAULT; s.blink = 1; s.state[4] = PS_BLEED; draw_main(&s); save("rot_fault");
    s = idle_state(); s.ps = 1; s.sel = 1; draw_main(&s); save("rot_ports_select");
    settings.theme = TH_ROT; menu_open(0); keys(2, EV_B2); keys(1, EV_OK); keys(1, EV_B2); draw_menu(); save("rot_menu");
    settings.theme = TH_CLASSIC;
    th_set(TH_CLASSIC);
}

int main(int argc, char **argv)
{
    ui_state_t s;
    if (argc != 2) { fprintf(stderr, "usage: %s OUTDIR\n", argv[0]); return 1; }
    outdir = argv[0 + 1];

    memset(&settings, 0xFF, sizeof settings);                       /* a unit with nothing saved: the defaults */
    cfg_validate();
    th_set(settings.theme);

    ui_splash(); save("splash");
    ui_message(R_WAIT, "INPUT LOW", "CHECK THE SUPPLY", "4.52V", 0); save("input_low");
    th_set(settings.theme);

    s = idle_state(); draw_main(&s); save("main_idle");
    s = charging_state(); draw_main(&s); save("main_charging");
    s.tphase = 1; draw_main(&s); save("main_charging_target");
    s = charging_state();
    s.state[1] = PS_CHARGE; s.parked[1] = 1; s.i[1] = 0;
    s.set[2] = PV_OFF; s.mixed = 1; s.set[3] = iu(900);
    draw_main(&s); save("main_limit");
    s = idle_state(); s.alarm = STOP_VLIM; s.vin = vin(4300); draw_main(&s); save("main_stopped");

    ports_screens();
    menu_screens();
    diag_screen();

    themed("calm", TH_CALM); themed("amber", TH_AMBER); themed("contrast", TH_CONTRAST);
    rot_screens();
    return 0;
}
