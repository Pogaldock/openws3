#include "ui.h"
#include "layout.h"
#include "theme.h"
#include "lcd.h"
#include "cal.h"
#include "fmt.h"
#include "chg.h"
#include "dbgscr.h"
#include "portset.h"
#include "version.h"
#include "version_build.h"
#include "sprites.h"
#include "logo.h"

#define NELEM (3 + NPORTS * 3)           /* 3 header boxes, then per port: voltage, status text, dot */
#define NBOX  (3 + NPORTS)               /* the 3 header boxes, then the port cells */

/* What the screen shows, so that only changes are drawn.  An element is its text and colour (a dot or frame has no text: it is
 * its colour and a mark that it is drawn); a box is its colour.  Repainting a box wipes its elements, which box_draw() marks as
 * not drawn.  Marking an element not drawn (shown[e][0] = 0) is how anything is made to repaint. */
static char    XD shown[NELEM][8];
static uint16_t XD shown_fg[NELEM];
static uint16_t XD box_bg[NBOX];
static uint8_t XD next_el;
static uint16_t XD anim;
static uint8_t XD last_st[NPORTS];               /* ROT: each port's state at its last visit, 0xFF = not known yet */
static uint8_t XD burst[NPORTS];                 /* ROT: visits of static left on a cell that just went full */
#define BURST_VISITS 8                           /* one visit per port per 21 control passes: about 0.75 s */
static uint16_t XD last_cv[NPORTS];              /* ROT: the voltage last shown on a charging port, centivolts */
static uint8_t XD pulse[NPORTS];                 /* ROT: visits left with that voltage in green, after it rose */

/* ROT screensaver: no battery anywhere, nothing running, no key changing anything for SAVER_AFTER control passes (about 3 min) */
#define SAVER_AFTER  40000u
#define SAVER_EMPTY  910                         /* below 1.0 V (vu) a port counts as empty */
static uint16_t XD quiet_n;
static uint8_t XD saver, saver_row, saver_iset, saver_volt;                        /* counts main-screen steps (one per control pass): the ROT eye animation clock */

static uint8_t box_x(uint8_t i)  { return UI_BOX_X0 + i * UI_BOX_PITCH; }
static uint8_t cell_y(uint8_t p) { return UI_CELL_Y0 + (p / 3) * UI_CELL_PITCH_Y; }

static uint8_t slen(const char *s) { uint8_t n = 0; while (s[n]) n++; return n; }
static void scopy(char XD *d, const char *s) { while ((*d++ = *s++) != 0) ; }

/* Only the characters that changed are drawn: at the bus rate a whole row takes ~13 ms, a 1x2 glyph ~1 ms.  diff() finds the span
 * [d_first, d_last) of `s` (n characters) that differs from `was`, what the screen shows; 0 = nothing changed. */
static uint8_t XD d_first, d_last;
static uint8_t diff(const char XD *was, const char *s, uint8_t n)
{
    d_first = 0;
    while (d_first < n && was[d_first] == s[d_first]) d_first++;
    if (d_first == n && was[n] == 0) return 0;
    d_last = n;
    if (slen(was) == n) while (d_last > d_first && was[d_last - 1] == s[d_last - 1]) d_last--;
    return 1;
}

/* draw that span of `s`, whose first character sits at x */
static void draw_span(uint8_t x, uint8_t y, uint8_t sy, const char *s, uint16_t fg, uint16_t bg)
{
    static char XD part[ROW_COLS + 1];
    uint8_t i;
    for (i = d_first; i < d_last; i++) part[i - d_first] = s[i];
    part[d_last - d_first] = 0;
    lcd_text((uint8_t)(x + d_first * 6), y, part, 1, sy, fg, bg);
}

/* centre `text` on the 160 px wide screen at scale sx,sy */
static void centered(uint8_t y, const char *text, uint8_t sx, uint8_t sy, uint16_t fg, uint16_t bg)
{
    uint8_t w = (uint8_t)(slen(text) * 6 * sx);
    lcd_text(w >= LCD_PX_W ? 0 : (uint8_t)((LCD_PX_W - w) / 2), y, text, sx, sy, fg, bg);
}

#define STR_(x) #x
#define STR(x)  STR_(x)
#if FW_PRE
#define FW_STAGE "-BETA." STR(FW_PRE)
#else
#define FW_STAGE ""
#endif

#define ROT (th_get() == TH_ROT)

/* ROT: a thorned line.  A 1-pixel line with a barb of `len` pixels every `step` pixels, pointing down (dir 1) or up (dir -1). */
static void thorns(uint8_t x, uint8_t y, uint8_t w, int8_t dir, uint8_t len, uint8_t step, uint16_t c)
{
    uint8_t i, k;
    lcd_fill(x, y, w, 1, c);
    for (i = 2; i + 2 < w; i += step)
        for (k = 1; k <= len; k++) lcd_fill((uint8_t)(x + i), (uint8_t)(y + dir * (int8_t)k), 1, 1, c);
}

/* ROT: the border of a box, thorns pointing inward on the top and bottom edges */
static void border(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t c)
{
    thorns(x, y, w, 1, 1, 5, c);
    thorns(x, (uint8_t)(y + h - 1), w, -1, 1, 5, c);
    lcd_fill(x, y, 1, h, c);
    lcd_fill((uint8_t)(x + w - 1), y, 1, h, c);
}

/* ROT splash: static, a black band with barbed edges, the wordmark between two skulls, the version on its own strip */
static void rot_splash(void)
{
    uint8_t lx = (uint8_t)((LCD_PX_W - LOGO_W) / 2), sw = spr_w[SPR_SKULL], sh = spr_h[SPR_SKULL];
    lcd_noise(0, 0, LCD_PX_W, LCD_PX_H, 0x5EED);
    lcd_fill(0, 9, LCD_PX_W, 50, th(R_BG));
    thorns(0, 9, LCD_PX_W, -1, 2, 6, th(R_DIM));
    thorns(0, 58, LCD_PX_W, 1, 2, 6, th(R_DIM));
    (void)lx; (void)sw; (void)sh;
    lcd_fill(0, 68, LCD_PX_W, 10, th(R_BG));
}

/* One frame of the ROT boot: the static strips crawl; frames 0-1 the band is empty (no signal), 2-5 the wordmark comes in torn
 * (rows shifted sideways, early rows dropping out), from 6 it is clean and the skulls are there. */
void ui_splash_frame(uint8_t n)
{
    uint8_t j, lx = (uint8_t)((LCD_PX_W - LOGO_W) / 2), sw = spr_w[SPR_SKULL], sh = spr_h[SPR_SKULL];
    int8_t off;
    uint16_t s;
    if (!ROT) return;
    lcd_noise(0, 0, LCD_PX_W, 7, (uint16_t)(0x1234 + n * 977u));
    lcd_noise(0, 61, LCD_PX_W, 7, (uint16_t)(0x4321 + n * 613u));
    if (n < 2) return;
    lcd_fill((uint8_t)(lx - 4), 12, LOGO_W + 8, LOGO_H, th(R_BG));
    s = (uint16_t)(0xACE1 + n * 31u);
    for (j = 0; j < LOGO_H; j++) {
        s = LCD_LFSR(s);
        off = 0;
        if (n < 6 && (s & 3) == 0) off = (int8_t)((s >> 4) & 7) - 3;     /* a quarter of the rows torn by up to 3 pixels */
        if (n < 4 && (s & 0x300) == 0) continue;                       /* and some missing while the signal is weak */
        lcd_bitmap((uint8_t)(lx + off), (uint8_t)(12 + j), LOGO_W, 1, logo_bits + j * LOGO_STRIDE, UI_C_NEON, th(R_BG));
    }
    if (n == 6 || n == 8) {
        uint8_t k, sx[2], sy = (uint8_t)(34 - sh / 2);
        sx[0] = (uint8_t)((lx - sw) / 2); sx[1] = (uint8_t)(lx + LOGO_W + (LCD_PX_W - lx - LOGO_W - sw) / 2);
        for (k = 0; k < 2; k++) {
            if (n == 6) lcd_bitmap(sx[k], sy, sw, sh, SPR_BITS(SPR_SKULL), th(R_TEXT), th(R_BG));
            else {                                    /* the pupils (sprite row 8, columns 4 and 12) light up as green crosses */
                lcd_fill((uint8_t)(sx[k] + 3), (uint8_t)(sy + 8), 3, 1, UI_C_NEON); lcd_fill((uint8_t)(sx[k] + 4), (uint8_t)(sy + 7), 1, 3, UI_C_NEON);
                lcd_fill((uint8_t)(sx[k] + 11), (uint8_t)(sy + 8), 3, 1, UI_C_NEON); lcd_fill((uint8_t)(sx[k] + 12), (uint8_t)(sy + 7), 1, 3, UI_C_NEON);
            }
        }
    }
}

void ui_splash(void)
{
    if (ROT) {
        rot_splash();
        centered(69, "V" STR(FW_MAJOR) "." STR(FW_MINOR) FW_STAGE " " FW_BUILD_STR, 1, 1, th(R_DIM), th(R_BG));
        return;
    }
    lcd_fill(0, 0, LCD_PX_W, LCD_PX_H, th(R_BG));
    lcd_fill(0, 14, LCD_PX_W, 3, th(R_BAD));
    lcd_fill(0, 63, LCD_PX_W, 3, th(R_BAD));
    centered(24, "OPENWS3", 2, 2, th(R_TEXT), th(R_BG));
    centered(44, "OPEN FW FOR WHOOPSTOR V3", 1, 1, th(R_INFO), th(R_BG));
    centered(54, "V" STR(FW_MAJOR) "." STR(FW_MINOR) FW_STAGE " " FW_BUILD_STR, 1, 1, th(R_DIM), th(R_BG));
}

static char XD mbig[8];                              /* the big line of the message on the screen */

void ui_message(uint8_t role, const char *title, const char *line2, const char *big, const char *small)
{
    lcd_fill(0, 0, LCD_PX_W, LCD_PX_H, th(R_BG));
    if (ROT) { lcd_noise(0, 0, LCD_PX_W, 4, 0xB0B0); lcd_noise(0, 76, LCD_PX_W, 4, 0x0666); }
    centered(6, title, 1, 2, th(role), th(R_BG));
    centered(28, line2, 1, 1, th(R_TEXT), th(R_BG));
    if (big) { centered(42, big, 2, 2, th(R_WARN), th(R_BG)); scopy(mbig, big); } else mbig[0] = 0;
    if (small) centered(60, small, 1, 1, th(R_DIM), th(R_BG));
}

/* The big line is drawn again in place, only the characters that changed: clearing it first would flash it. */
void ui_message_big(const char *big)
{
    static uint8_t XD i, n, x0;                      /* external RAM: direct RAM is full */
    static char XD c[2];
    n = slen(big);
    if (n != slen(mbig)) { lcd_fill(0, 42, LCD_PX_W, 16, th(R_BG)); mbig[0] = 0; }     /* another length: all of it */
    x0 = (uint8_t)((LCD_PX_W - n * 12) / 2);
    for (i = 0; i < n; i++) {
        if (mbig[0] && mbig[i] == big[i]) continue;
        c[0] = big[i]; c[1] = 0;
        lcd_text((uint8_t)(x0 + i * 12), 42, c, 2, 2, th(R_WARN), th(R_BG));
    }
    scopy(mbig, big);
}

/* The one rule for text: draw `s` (at most `max` characters) at x,y in fg on bg, only the characters that differ from `was`, the text
 * the screen shows in the colour *was_fg; a new colour redraws it all.  Then `was` holds `s`. */
static void show(char XD *was, uint16_t XD *was_fg, uint8_t max, uint8_t x, uint8_t y, uint8_t sy, uint16_t fg, uint16_t bg, const char *s)
{
    uint8_t i, n = slen(s);
    if (n > max) n = max;
    if (*was_fg != fg) { *was_fg = fg; was[0] = 0; }
    if (!diff(was, s, n)) return;
    draw_span(x, y, sy, s, fg, bg);
    for (i = 0; i < n; i++) was[i] = s[i];
    was[n] = 0;
}

static void put(uint8_t e, uint8_t x, uint8_t y, uint8_t sy, uint16_t fg, uint16_t bg, const char *s)
{
    show(shown[e], &shown_fg[e], 7, x, y, sy, fg, bg, s);
}

/* the same for a coloured square (a dot, a frame): true when it has to be drawn, in colour c */
static uint8_t changed(uint8_t e, uint16_t c)
{
    if (shown[e][0] && shown_fg[e] == c) return 0;
    shown[e][0] = 1; shown_fg[e] = c;
    return 1;
}

/* box b (a header box, or port b - 3) painted in bg, its elements marked as wiped */
static void box_draw(uint8_t b, uint16_t bg)
{
    uint8_t p, x, y, k;
    char d[2];
    box_bg[b] = bg;
    if (b < 3) {
        lcd_fill(box_x(b), UI_HDR_Y, UI_BOX_W, UI_HDR_H, bg);
        if (ROT) border(box_x(b), UI_HDR_Y, UI_BOX_W, UI_HDR_H, th(R_DIM));
        shown[b][0] = 0;
        return;
    }
    p = b - 3; x = box_x(p % 3); y = cell_y(p);
    lcd_fill(x, y, UI_BOX_W, UI_CELL_H, bg);
    lcd_fill(x + UI_BADGE_X, y + 2, UI_BADGE_W, UI_CELL_H - 4, th(R_BADGE0 + p));
    d[0] = (char)('1' + p); d[1] = 0;
    lcd_text(x + UI_BADGE_X + 2, y + 6, d, 1, 2, th(R_BG), th(R_BADGE0 + p));
    if (ROT) border(x, y, UI_BOX_W, UI_CELL_H, bg == UI_C_BLOOD ? th(R_BAD) : th(R_DIM));
    for (k = 0; k < 3; k++) shown[3 + p * 3 + k][0] = 0;
}

static void box(uint8_t b, uint16_t bg) { if (box_bg[b] != bg) box_draw(b, bg); }

void ui_main_init(void)
{
    uint8_t b;
    lcd_fill(0, 0, LCD_PX_W, LCD_PX_H, th(R_BG));
    for (b = 0; b < NBOX; b++) box_draw(b, th(R_BOX));
    for (b = 0; b < NPORTS; b++) { last_st[b] = 0xFF; burst[b] = 0; last_cv[b] = 0; pulse[b] = 0; }
    quiet_n = 0; saver = 0;
    next_el = 0;
}

/* pad with spaces to n characters, so a shorter text overwrites the previous one */
static void pad(char XD *t, uint8_t n) { uint8_t i = slen(t); while (i < n) t[i++] = ' '; t[i] = 0; }

static void frame(uint8_t x, uint8_t y, uint16_t c)
{
    lcd_fill(x, y, UI_BOX_W, UI_FRAME, c);
    lcd_fill(x, (uint8_t)(y + UI_CELL_H - UI_FRAME), UI_BOX_W, UI_FRAME, c);
    lcd_fill(x, y, UI_FRAME, UI_CELL_H, c);
    lcd_fill((uint8_t)(x + UI_BOX_W - UI_FRAME), y, UI_FRAME, UI_CELL_H, c);
    if (ROT && c == th(R_BOX)) border(x, y, UI_BOX_W, UI_CELL_H, th(R_DIM));     /* taking the frame off puts the thorns back */
}

static uint16_t status_color(uint8_t state)
{
    switch (state) {
    case PS_CHARGE: return th(R_VOLT);
    case PS_FULL:   return th(R_FULLTX);
    case PS_FAULT:  return th(R_BAD);
    case PS_BLEED:  return th(R_INFO);
    default:        return th(R_WARN);
    }
}

static void header(const ui_state_t *s, uint8_t e, char XD *t) REENT
{
    static const CODE char *const ALARM[] = { "", "VIN!", "VCC!", "RAIL!", "", "V LIMIT" };   /* by STOP_* */
    uint16_t col = th(R_TEXT), bg = s->run ? th(R_RUN) : th(R_BOX);
    if (s->ps) {                                     /* port settings: what this is, the port, what the keys do */
        if (e == 0) scopy(t, "PORTS");
        else if (e == 1) {
            if (s->locked) { scopy(t, "LOCKED"); col = th(R_WARN); }
            else { scopy(t, "PORT "); t[5] = (char)('1' + s->sel); t[6] = 0; }
        } else scopy(t, s->ps == 2 ? "- + OK" : "< > OK");
    } else if (e == 0) {
        fmt_a1(t, cal_set_to_ma(s->iset));
        if (s->mixed) { uint8_t n = slen(t); t[n] = '*'; t[n + 1] = 0; }
    } else if (e == 1) {
        if (s->alarm) { scopy(t, ALARM[s->alarm]); col = th(R_WARN); }
        else if (s->usb_off && s->tphase) { scopy(t, "USB OFF"); col = th(R_GOOD); }   /* 1 s in 4, like the targets in the port cells */
        else { fmt_vin(t, cal_vin_mv(s->vin)); col = s->usb_off ? th(R_GOOD) : th(R_VIN); }
    } else fmt_v2(t, cal_port_mv(s->tgt));
    if (col == bg) col = th(R_BG);                   /* a theme whose running header is the text colour (ROT): ink the text */
    pad(t, 7);
    box(e, bg);
    put(e, box_x(e) + UI_HDR_TX, UI_HDR_Y + UI_HDR_TY, 2, col, bg, t);
}

/* "0.5A", the current a port charges at */
static void amps1(char XD *t, uint8_t iu) { fmt_a1(t, cal_set_to_ma(iu)); }

/* where the two text fields of a port cell sit, by part: the voltage (1x2), the line under it (1x1) */
static const CODE struct { uint8_t dx, dy, sy; } FLD[2] = { { UI_CELL_VX, UI_CELL_VY, 2 }, { UI_CELL_IX, UI_CELL_IY, 1 } };

/* One element of the screen: each branch decides the text t, its colour fg and the box colour bg, and the one put() at the end
 * draws it.  A dot or frame is not text: it is drawn by its branch, which returns. */
static void element(const ui_state_t *s, uint8_t e) REENT
{
    static char XD t[10];                      /* external RAM: direct RAM is full */
    uint8_t k, p, part, x, y, st, off, full;
    uint16_t fg, bg;
    if (e < 3) { header(s, e, t); return; }
    k = e - 3; p = k / 3; part = k % 3;
    x = box_x(p % 3); y = cell_y(p);
    st = s->state[p]; off = s->set[p] == PV_OFF;
    full = !s->ps && st == PS_FULL;                  /* a full port's cell is tinted, so it shows from across the room */
    bg = full ? th(R_FULL) : (ROT && !s->ps && st == PS_FAULT) ? UI_C_BLOOD : th(R_BOX);
    if (ROT && !s->ps) {
        /* a port that has just gone full dissolves into static for a moment, then is stamped white (the box is repainted) */
        if (part == 0) {
            if (st == PS_FULL && last_st[p] != 0xFF && last_st[p] != PS_FULL) burst[p] = BURST_VISITS;
            last_st[p] = st;
            if (burst[p]) {
                lcd_noise((uint8_t)(x + 1), (uint8_t)(y + 2), UI_BOX_W - 2, UI_CELL_H - 4, (uint16_t)(anim * 7u + p));
                if (--burst[p] == 0) box_draw(3 + p, bg);
                return;
            }
        } else if (burst[p]) return;
    }
    box(3 + p, bg);
    if (s->ps) {                                     /* port settings: the value big, what kind of value small, a frame round the selection */
        uint8_t edit = s->ps == 2 && p == s->sel;
        if (part == 0) {
            if (off) scopy(t, "OFF"); else amps1(t, s->want[p]);
            if (edit) { uint8_t n = slen(t), i; for (i = n; i > 0; i--) t[i] = t[i - 1]; t[0] = '<'; t[n + 1] = '>'; t[n + 2] = 0; }
            pad(t, 6);
            fg = edit ? th(R_WARN) : off ? th(R_DIM) : th(R_TEXT);
        } else if (part == 1) {
            scopy(t, off ? "OFF" : s->set[p] == PV_AUTO ? "AUTO" : "SET");
            pad(t, 5);
            fg = off || s->set[p] == PV_AUTO ? th(R_DIM) : th(R_INFO);
        } else {
            uint8_t f = p != s->sel ? 0 : !edit ? 1 : s->blink ? 2 : 3;      /* none, selected, editing (blinks) */
            uint16_t fc = f == 1 ? th(R_INFO) : f == 2 ? th(R_WARN) : th(R_BOX);
            if (changed(e, fc)) frame(x, y, fc);
            return;
        }
    } else if (part == 0) {
        fmt_v2(t, cal_port_mv(s->v[p]));
        fg = off ? th(R_DIM) : full ? th(R_FULLTX) : th(R_VOLT);
        if (ROT && !off && st == PS_CHARGE) {         /* a charging voltage that rises flashes green for a moment */
            uint16_t cv = cal_port_mv(s->v[p]) / 10;
            if (last_cv[p] && cv > last_cv[p]) pulse[p] = 2;
            last_cv[p] = cv;
            if (pulse[p]) { pulse[p]--; fg = UI_C_NEON; }
        } else last_cv[p] = 0;
    } else if (part == 1) {
        /* charging: the measured current, 1 s in 4 the target; idle: the target; an OFF port says so; else the state */
        static const CODE char *const NAME[] = { "", "TEST", "", "REST", "FULL", "BLEED", "ERR" };   /* by PS_* */
        fg = status_color(st);
        if (st == PS_IDLE && off) { scopy(t, "OFF"); fg = th(R_DIM); }
        else if ((st == PS_CHARGE && s->tphase) || st == PS_IDLE) { t[0] = '>'; amps1(t + 1, s->want[p]); fg = th(R_INFO); }
        else if (st == PS_CHARGE && s->parked[p]) { scopy(t, "LIMIT"); fg = th(R_WARN); }      /* waiting for room under a user limit */
        else if (st == PS_CHARGE) fmt_a2(t, cal_port_ma(s->i[p]));
        else scopy(t, NAME[st]);
        pad(t, 5);
    } else {
        st &= 7;
        fg = th(R_DOT0 + st);
        if (st == PS_FAULT && !s->blink) fg = bg;              /* errors blink */
        if (ROT) {
            /* sprites 0..6 are the PS_* icons; a charging eye looks around and blinks, each port out of step with the others */
            static const CODE uint8_t GAZE[8] = { SPR_EYE_OPEN, SPR_EYE_OPEN, SPR_EYE_LEFT, SPR_EYE_OPEN, SPR_EYE_OPEN, SPR_EYE_RIGHT, SPR_EYE_OPEN, SPR_EYE_HALF };
            uint8_t spr = st == PS_CHARGE ? GAZE[(uint8_t)((anim >> 8) + p * 3) & 7] : st;
            if (changed(e, fg) | (shown[e][1] != (char)(spr + 1))) {
                shown[e][1] = (char)(spr + 1);
                lcd_bitmap(x + UI_DOT_X, y + UI_DOT_Y - 1, spr_w[spr], spr_h[spr], SPR_BITS(spr), fg, bg);
            }
        } else if (changed(e, fg)) lcd_fill(x + UI_DOT_X, y + UI_DOT_Y, UI_DOT_S, UI_DOT_S, fg);
        return;
    }
    put(e, x + FLD[part].dx, y + FLD[part].dy, FLD[part].sy, fg, bg, t);
}

static char XD dbg_shown[DBG_ROWS][DBG_COLS + 1];
static uint16_t XD dbg_col[DBG_ROWS];

void ui_text_enter(void)
{
    uint8_t r;
    lcd_fill(0, 0, LCD_PX_W, LCD_PX_H, th(R_BG));
    for (r = 0; r < DBG_ROWS; r++) dbg_shown[r][0] = 0;
}

void ui_text_row(uint8_t row, const char *text, uint16_t color)
{
    if (row >= DBG_ROWS) return;
    show(dbg_shown[row], &dbg_col[row], DBG_COLS, 0, (uint8_t)(row * 8), 1, color, th(R_BG), text);
}

/* Every REFRESH_EVERY-th step the element is redrawn even if it did not change: a glitch that corrupted a few pixels would
 * otherwise stay on screen forever (only changes are drawn). */
#define REFRESH_EVERY 64         /* must not share a factor with NELEM (21 = 3 x 7), or only some elements would ever be refreshed */
static uint8_t XD refresh_div;

static uint8_t quiet(const ui_state_t *s)
{
    uint8_t p;
    if (s->run || s->ps || s->alarm || s->iset != saver_iset || s->volt != saver_volt) return 0;
    for (p = 0; p < NPORTS; p++) if (s->v[p] >= SAVER_EMPTY) return 0;
    return 1;
}

/* Four rows of the screensaver: static crawling down the screen, the green wordmark surfacing through it and sinking back
 * (a triangle wave of about 18 s).  Drawn one control pass in eight, about 7 ms each. */
static void saver_strip(void)
{
    uint8_t r, y, lx = (uint8_t)((LCD_PX_W - LOGO_W) / 2), ly = (uint8_t)((LCD_PX_H - LOGO_H) / 2), ph = (uint8_t)(anim >> 4);
    uint16_t tri = ph < 128 ? ph * 2u : (255u - ph) * 2u, keep = tri * 3u / 2u;
    if (keep > 255) keep = 255;
    for (r = 0; r < 4; r++) {
        y = (uint8_t)(saver_row * 4 + r);
        lcd_noise_row(y, (uint16_t)(anim * 13u + y * 101u),
                      y >= ly && y < ly + LOGO_H ? logo_bits + (y - ly) * LOGO_STRIDE : 0, lx, LOGO_W, (uint8_t)keep, UI_C_NEON);
    }
    if (++saver_row == LCD_PX_H / 4) saver_row = 0;
}

void ui_main_step(const ui_state_t *s)
{
    uint8_t e = next_el;
    anim++;
    if (ROT) {
        if (quiet(s)) { if (quiet_n < SAVER_AFTER) quiet_n++; }
        else {
            quiet_n = 0; saver_iset = s->iset; saver_volt = s->volt;
            if (saver) ui_main_init();                /* back to the charger screen, all of it repainted */
        }
        if (quiet_n >= SAVER_AFTER) {
            saver = 1;
            if ((anim & 7) == 0) saver_strip();
            return;
        }
    }
    if (++next_el == NELEM) next_el = 0;
    if (++refresh_div >= REFRESH_EVERY) {
        refresh_div = 0;
        shown[e][0] = 0;
    }
    element(s, e);
}
