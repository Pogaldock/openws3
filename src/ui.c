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
#include "stats.h"

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
static uint8_t XD look_p, look_n;                /* ROT: the port that just finished or faulted, and visits left that the eyes look at it */

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

/* ROT borders.  Their arguments are shared variables in external RAM, not parameters: these run deep in the screen code (inside a cell
 * repaint), where every byte of stack counts, and plain functions with no parameters keep both the stack and the direct RAM free. */
static uint8_t XD tn_x, tn_y, tn_w, tn_h, tn_len, tn_step, tn_i, tn_k;
static int8_t  XD tn_dir;
static uint16_t XD tn_c;

/* a thorned line at tn_x, tn_y, tn_w long: a 1-pixel line with a barb of tn_len pixels every tn_step pixels, down (tn_dir 1) or up (-1) */
static void thorns(void)
{
    lcd_fill(tn_x, tn_y, tn_w, 1, tn_c);
    for (tn_i = 2; tn_i + 2 < tn_w; tn_i += tn_step)
        for (tn_k = 1; tn_k <= tn_len; tn_k++) lcd_fill((uint8_t)(tn_x + tn_i), (uint8_t)(tn_y + tn_dir * (int8_t)tn_k), 1, 1, tn_c);
}

/* the border of the box at tn_x, tn_y, tn_w x tn_h in tn_c: thorns pointing inward on the top and bottom edges */
static void border(void)
{
    uint8_t y0 = tn_y;
    tn_len = 1; tn_step = 5;
    tn_dir = 1; thorns();
    tn_y = (uint8_t)(y0 + tn_h - 1); tn_dir = -1; thorns();
    lcd_fill(tn_x, y0, 1, tn_h, tn_c);
    lcd_fill((uint8_t)(tn_x + tn_w - 1), y0, 1, tn_h, tn_c);
}

/* ROT splash: static, a black band with barbed edges, the wordmark between two skulls, the version on its own strip */
static void rot_splash(void) REENT
{
    uint8_t lx = (uint8_t)((LCD_PX_W - LOGO_W) / 2), sw = spr_w[SPR_SKULL], sh = spr_h[SPR_SKULL];
    lcd_noise(0, 0, LCD_PX_W, LCD_PX_H, 0x5EED);
    lcd_fill(0, 9, LCD_PX_W, 50, th(R_BG));
    tn_x = 0; tn_w = LCD_PX_W; tn_len = 2; tn_step = 6; tn_c = th(R_DIM);
    tn_y = 9; tn_dir = -1; thorns();
    tn_y = 58; tn_dir = 1; thorns();
    (void)lx; (void)sw; (void)sh;
    lcd_fill(0, 68, LCD_PX_W, 10, th(R_BG));
}

/* One frame of the ROT boot: the static strips crawl; frames 0-1 the band is empty (no signal), 2-5 the wordmark comes in torn
 * (rows shifted sideways, early rows dropping out), from 6 it is clean and the skulls are there. */
void ui_splash_frame(uint8_t n) REENT
{
    uint8_t j, lx = (uint8_t)((LCD_PX_W - LOGO_W) / 2), sw = spr_w[SPR_SKULL], sh = spr_h[SPR_SKULL];
    int8_t off;
    uint16_t s;
    if (!ROT) return;
    lcd_noise(0, 0, LCD_PX_W, 7, (uint16_t)(0x1234 + n * 977u));
    lcd_noise(0, 61, LCD_PX_W, 7, (uint16_t)(0x4321 + n * 613u));
    if (n < 2) return;
    if (n >= 20) {                                    /* a neon flash, then the clean wordmark */
        lcd_bitmap(lx, 12, LOGO_W, LOGO_H, logo_bits, n == 20 ? th(R_BG) : UI_C_NEON, n == 20 ? UI_C_NEON : th(R_BG));
        return;
    }
    if (n < 10) lcd_fill((uint8_t)(lx - 4), 12, LOGO_W + 8, LOGO_H, th(R_BG));
    s = (uint16_t)(0xACE1 + n * 31u);
    for (j = 0; n < 10 && j < LOGO_H; j++) {
        s = LCD_LFSR(s);
        off = 0;
        if (n < 6 && (s & 3) == 0) off = (int8_t)((s >> 4) & 7) - 3;     /* a quarter of the rows torn by up to 3 pixels */
        if (n < 4 && (s & 0x300) == 0) continue;                       /* and some missing while the signal is weak */
        lcd_bitmap((uint8_t)(lx + off), (uint8_t)(12 + j), LOGO_W, 1, logo_bits + j * LOGO_STRIDE, UI_C_NEON, th(R_BG));
    }
    if (n >= 10 && n <= 14) {                         /* a scan line sweeps the wordmark (44 rows): 11 rows lit white, then neon again */
        j = (uint8_t)((n - 10) * 11);
        if (n < 14) lcd_bitmap(lx, (uint8_t)(12 + j), LOGO_W, 11, logo_bits + j * LOGO_STRIDE, th(R_TEXT), th(R_BG));
        if (j) { j -= 11; lcd_bitmap(lx, (uint8_t)(12 + j), LOGO_W, 11, logo_bits + j * LOGO_STRIDE, UI_C_NEON, th(R_BG)); }
    }
    if (n == 6 || n == 16 || n == 18) {
        uint8_t k, sx[2], sy = (uint8_t)(34 - sh / 2);
        sx[0] = (uint8_t)((lx - sw) / 2); sx[1] = (uint8_t)(lx + LOGO_W + (LCD_PX_W - lx - LOGO_W - sw) / 2);
        for (k = 0; k < 2; k++) {
            if (n == 6) lcd_bitmap(sx[k], sy, sw, sh, SPR_BITS(SPR_SKULL), th(R_TEXT), th(R_BG));
            else if ((n == 16) == (k == 0)) {         /* the eyes open one by one: the pupils (sprite row 8, columns 4 and 12) light green */
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

/* ROT: the end of a run.  Static floods the screen, the wordmark burns through it in neon, then green flashes; the summary is
 * drawn over the last frame by the app. */
void ui_finale_frame(uint8_t n)
{
    static uint8_t XD lx, y;
    lx = (uint8_t)((LCD_PX_W - LOGO_W) / 2);
    if (n < 8) { lcd_noise(0, (uint8_t)(n * 10), LCD_PX_W, 10, (uint16_t)(0x51CA + n * 733u)); return; }   /* static floods down */
    y = (uint8_t)((n - 8) * 6);                                       /* then the wordmark burns through, 6 rows a frame */
    if (y >= LOGO_H) return;
    lcd_fill(0, (uint8_t)(18 + y), LCD_PX_W, 6, th(R_BG));
    lcd_bitmap(lx, (uint8_t)(18 + y), LOGO_W, (uint8_t)(LOGO_H - y < 6 ? LOGO_H - y : 6), logo_bits + y * LOGO_STRIDE, UI_C_NEON, th(R_BG));
}

static char XD mbig[8];                              /* the big line of the message on the screen */

/* A message screen in parts: 0-3 clear the screen in four strips of 20 rows, 4 the title, 5 the line under it, 6 the big line, 7 the
 * small one.  Each part is under 40 ms on the panel, so a screen drawn one part per control pass never stalls the control loop
 * (ui_message, which draws them all at once, is for screens shown while nothing is charging). */
uint8_t XD ui_msg_later;
static uint8_t XD m_role;                             /* the message being drawn (ui_message_set): external RAM, no parameters */
static const char *XD m_title, *XD m_line2, *XD m_big, *XD m_small;

void ui_message_part(uint8_t part)
{
    if (part < 4) {
        lcd_fill(0, (uint8_t)(part * 20), LCD_PX_W, 20, th(R_BG));
        if (ROT && part == 0) lcd_noise(0, 0, LCD_PX_W, 4, 0xB0B0);
        if (ROT && part == 3) lcd_noise(0, 76, LCD_PX_W, 4, 0x0666);
    } else if (part == 4) centered(6, m_title, 1, 2, th(m_role), th(R_BG));
    else if (part == 5) centered(28, m_line2, 1, 1, th(R_TEXT), th(R_BG));
    else if (part == 6) { if (m_big) { centered(42, m_big, 2, 2, th(R_WARN), th(R_BG)); scopy(mbig, m_big); } else mbig[0] = 0; }
    else if (m_small) centered(60, m_small, 1, 1, th(R_DIM), th(R_BG));
}

/* store the message, then draw all its parts at once (a screen shown while nothing charges) */
void ui_message(uint8_t role, const char *title, const char *line2, const char *big, const char *small)
{
    static uint8_t XD part;
    m_role = role; m_title = title; m_line2 = line2; m_big = big; m_small = small;
    if (ui_msg_later) return;                        /* the caller draws the parts one per pass */
    for (part = 0; part < UI_MSG_PARTS; part++) ui_message_part(part);
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

/* ROT: the thorned border of box b (red round a fault cell), on its own so box_draw keeps no extra temporaries in direct RAM */
static void box_thorns(uint8_t b, uint16_t bg)
{
    if (!ROT) return;
    tn_w = UI_BOX_W; tn_c = th(R_DIM);
    if (b < 3) { tn_x = box_x(b); tn_y = UI_HDR_Y; tn_h = UI_HDR_H; }
    else { tn_x = box_x((b - 3) % 3); tn_y = cell_y(b - 3); tn_h = UI_CELL_H; if (bg == UI_C_BLOOD) tn_c = th(R_BAD); }
    border();
}

/* box b (a header box, or port b - 3) painted in bg, its elements marked as wiped */
static void box_draw(uint8_t b, uint16_t bg)
{
    uint8_t p, x, y, k;
    char d[2];
    box_bg[b] = bg;
    if (b < 3) {
        lcd_fill(box_x(b), UI_HDR_Y, UI_BOX_W, UI_HDR_H, bg);
        shown[b][0] = 0;
        return;
    }
    p = b - 3; x = box_x(p % 3); y = cell_y(p);
    lcd_fill(x, y, UI_BOX_W, UI_CELL_H, bg);
    lcd_fill(x + UI_BADGE_X, y + 2, UI_BADGE_W, UI_CELL_H - 4, th(R_BADGE0 + p));
    d[0] = (char)('1' + p); d[1] = 0;
    lcd_text(x + UI_BADGE_X + 2, y + 6, d, 1, 2, th(R_BG), th(R_BADGE0 + p));
    for (k = 0; k < 3; k++) shown[3 + p * 3 + k][0] = 0;
}

static void box(uint8_t b, uint16_t bg) { if (box_bg[b] != bg) { box_draw(b, bg); box_thorns(b, bg); } }

void ui_main_init(void)
{
    uint8_t b;
    lcd_fill(0, 0, LCD_PX_W, LCD_PX_H, th(R_BG));
    for (b = 0; b < NBOX; b++) { box_draw(b, th(R_BOX)); box_thorns(b, th(R_BOX)); }
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
    if (ROT && c == th(R_BOX)) { tn_x = x; tn_y = y; tn_w = UI_BOX_W; tn_h = UI_CELL_H; tn_c = th(R_DIM); border(); }   /* the thorns come back */
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

/* text colour col on a background bg: when they are the same (ROT's white running header), the text is inked in the background colour */
static uint16_t ink(uint16_t col, uint16_t bg) { return col == bg ? th(R_BG) : col; }

static void header(const ui_state_t *s, uint8_t e, char XD *t) REENT
{
    static const CODE char *const ALARM[] = { "", "VIN!", "VCC!", "RAIL!", "", "V LIMIT", "HOT!" };   /* by STOP_* */
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
        static const CODE char *const VIEW[] = { "CURRENT", "MAH IN", "TO FULL", "HEALTH" };   /* by UV_* */
        if (s->alarm) { scopy(t, ALARM[s->alarm]); col = th(R_WARN); }
        else if (s->view_lbl) { scopy(t, VIEW[s->view]); col = th(R_INFO); }
        else if (s->usb_off && s->tphase) { scopy(t, "USB OFF"); col = th(R_GOOD); }   /* 1 s in 4, like the targets in the port cells */
        else { fmt_vin(t, cal_vin_mv(s->vin)); col = s->usb_off ? th(R_GOOD) : th(R_VIN); }
    } else fmt_v2(t, cal_port_mv(s->tgt));
    col = ink(col, bg);                              /* a theme whose running header is the text colour (ROT): ink the text */
    pad(t, 7);
    box(e, bg);
    put(e, box_x(e) + UI_HDR_TX, UI_HDR_Y + UI_HDR_TY, 2, col, bg, t);
}

/* ROT: the icon in each header box's left margin (current: a bolt that crackles while charging, input: a skull, red on an alarm,
 * target: a spike, green once every battery is full), drawn on every visit: 35 pixels. */
static void header_icon(const ui_state_t *s, uint8_t e, uint16_t bg) REENT
{
    uint8_t spr, p, cells = 0, full = 0;
    uint16_t c = bg == th(R_TEXT) ? th(R_BG) : th(R_DIM);           /* ink on the white running header, grey on the dark one */
    if (e == 0) spr = s->run && ((anim >> 6) & 1) ? SPR_BOLT_SPARK : SPR_BOLT;
    else if (e == 1) { spr = SPR_SKULL5; if (s->alarm) c = th(R_BAD); }
    else {
        spr = SPR_SPIKE5;
        for (p = 0; p < NPORTS; p++) if (s->state[p] != PS_IDLE) { cells++; if (s->state[p] == PS_FULL) full++; }
        if (cells && full == cells) c = bg == th(R_TEXT) ? th(R_BG) : UI_C_NEON;
    }
    lcd_bitmap((uint8_t)(box_x(e) + 2), UI_HDR_Y + 7, spr_w[spr], spr_h[spr], SPR_BITS(spr), c, bg);
}

/* ROT: the 2-pixel strip between the header and the cells (y 20-21) is a charge bar for the whole charger: how close the cells are
 * to the target on average (a full cell counts as there; 0.9 V away counts as nothing), green while running with a white spark
 * running along it, grey when stopped.  Redrawn on each visit of the target box, about 3 ms. */
#define BAR_Y     20
#define BAR_SPAN  900                            /* mV from the target that counts as an empty cell */
static void charge_bar(const ui_state_t *s) REENT
{
    uint8_t p, n = 0, w, k;
    uint16_t tgt = cal_port_mv(s->tgt), v, d;
    uint32_t sum = 0;
    for (p = 0; p < NPORTS; p++) {
        if (s->state[p] == PS_IDLE || s->set[p] == PV_OFF) continue;
        n++;
        if (s->state[p] == PS_FULL) { sum += BAR_SPAN; continue; }
        v = cal_port_mv(s->v[p]);
        d = v > tgt ? v - tgt : tgt - v;
        sum += d >= BAR_SPAN ? 0 : BAR_SPAN - d;
    }
    w = n ? (uint8_t)(sum * (LCD_PX_W - 2) / ((uint32_t)n * BAR_SPAN)) : 0;
    lcd_fill(1, BAR_Y, w, 2, s->run ? UI_C_NEON : th(R_DIM));
    lcd_fill((uint8_t)(1 + w), BAR_Y, (uint8_t)(LCD_PX_W - 2 - w), 2, th(R_BOX));
    if (s->run && w > 4) {
        k = (uint8_t)((anim >> 2) % (w - 3));
        lcd_fill((uint8_t)(1 + k), BAR_Y, 3, 2, th(R_TEXT));
    }
}

/* "0.5A", the current a port charges at */
static void amps1(char XD *t, uint8_t iu) { fmt_a1(t, cal_set_to_ma(iu)); }

/* ROT: a port's eye.  Charging, it looks around (faster the more current flows), and for a while after a port finishes or faults
 * every eye looks at that port; it fills with green from the bottom as the cell nears the target (0.9 V below it = empty).
 * One plain step per statement, all in external RAM: element(), which calls this, is the deepest stack frame of the firmware. */
#define EYE_H 7                                    /* the eye sprites are 7 x 7, one byte a row (sprites.txt) */
static uint8_t XD eye_spr, eye_lvl;
static void eye(const ui_state_t *s, uint8_t p, uint8_t st)
{
    static const CODE uint8_t GAZE[8] = { SPR_EYE_OPEN, SPR_EYE_OPEN, SPR_EYE_LEFT, SPR_EYE_OPEN, SPR_EYE_OPEN, SPR_EYE_RIGHT, SPR_EYE_OPEN, SPR_EYE_HALF };
    static uint16_t XD q, t;
    static uint8_t XD c, lc;
    eye_spr = st; eye_lvl = 0;
    if (st != PS_CHARGE) return;
    q = s->i[p]; q /= 16u; q += 1u; q *= anim; q >>= 8; q += p * 3u;
    eye_spr = GAZE[(uint8_t)q & 7];
    if (look_n) {
        c = p % 3; lc = look_p % 3;
        eye_spr = lc < c ? SPR_EYE_LEFT : lc > c ? SPR_EYE_RIGHT : SPR_EYE_OPEN;
    }
    t = cal_port_mv(s->tgt); q = cal_port_mv(s->v[p]);
    q = t > q ? t - q : 0;
    if (q >= 900u) return;
    q = 900u - q; q *= EYE_H; q /= 900u;
    eye_lvl = (uint8_t)q;
}

/* the lower line of a cell in the views other than the current: 6 characters; 0 = nothing to say yet (the current is shown) */
static uint8_t view_text(const ui_state_t *s, uint8_t p, char XD *t) REENT
{
    static uint16_t XD q;
    static uint8_t XD n;
    if (s->view == UV_MAH) { n = fmt_u(t, stats.mah[p]); scopy(t + n, "MAH"); return 1; }
    if (s->view == UV_ETA) {
        q = stats.eta_s[p];
        if (!q) { scopy(t, "DONE"); return 1; }
        if (q == ETA_UNKNOWN) { scopy(t, "@?MIN"); return 1; }
        q = (uint16_t)((q + 59u) / 60u);                                   /* minutes, rounded up: "~12MIN", from 100 on "~123M" */
        t[0] = '@';                                                   /* drawn as a tilde: an estimate */
        n = (uint8_t)(1 + fmt_u(t + 1, q));
        scopy(t + n, q < 100 ? "MIN" : "M");
        return 1;
    }
    q = stats.r_mohm[p];                                                   /* UV_HEALTH: the grade, 1 s in 4 the number */
    if (!q) { scopy(t, "--"); return 1; }
    if (s->tphase) { n = fmt_u(t, q); scopy(t + n, "MR"); return 1; }
    { static const CODE char *const G[] = { "", "GOOD", "TIRED", "RETIRE" }; scopy(t, G[stats_grade(q)]); }
    return 1;
}

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
            if ((st == PS_FULL || st == PS_FAULT) && last_st[p] != 0xFF && last_st[p] != st) { burst[p] = BURST_VISITS; look_p = p; look_n = 40; }
            last_st[p] = st;
            if (burst[p]) {
                lcd_noise((uint8_t)(x + 1), (uint8_t)(y + 2), UI_BOX_W - 2, UI_CELL_H - 4, (uint16_t)(anim * 7u + p));
                if (--burst[p] == 0) { box_draw(3 + p, bg); box_thorns(3 + p, bg); }
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
            static uint16_t XD cv;                    /* external RAM: element's stack frame stays at its stock size */
            cv = cal_port_mv(s->v[p]) / 10;
            if (last_cv[p] && cv > last_cv[p]) pulse[p] = 2;
            last_cv[p] = cv;
            if (pulse[p]) { pulse[p]--; fg = UI_C_NEON; }
        } else last_cv[p] = 0;
    } else if (part == 1) {
        /* charging: the measured current, 1 s in 4 the target; idle: the target; an OFF port says so; else the state */
        static const CODE char *const NAME[] = { "", "TEST", "", "REST", "FULL", "BLEED", "ERR" };   /* by PS_* */
        fg = status_color(st);
        if (st == PS_IDLE && off) { scopy(t, "OFF"); fg = th(R_DIM); }
        else if ((st == PS_CHARGE && s->tphase && !s->view) || st == PS_IDLE) { t[0] = '>'; amps1(t + 1, s->want[p]); fg = th(R_INFO); }
        else if (st == PS_CHARGE && s->parked[p]) { scopy(t, "LIMIT"); fg = th(R_WARN); }      /* waiting for room under a user limit */
        else if (s->view && (st == PS_CHARGE || st == PS_FULL) && view_text(s, p, t)) fg = full ? fg : th(R_INFO);   /* a full cell keeps its own colours */
        else if (st == PS_CHARGE) fmt_a2(t, cal_port_ma(s->i[p]));
        else scopy(t, NAME[st]);
        pad(t, 6);
    } else {
        st &= 7;
        fg = th(R_DOT0 + st);
        if (st == PS_FAULT && !s->blink) fg = bg;              /* errors blink */
        if (ROT) {
            /* sprites 0..6 are the PS_* icons; a charging eye looks around and blinks, each port out of step with the others */
            static uint8_t XD spr, lvl, h;            /* external RAM: element's stack frame stays at its stock size */
            eye(s, p, st);                            /* eye_spr, eye_lvl, eye_h: worked out outside this deep frame */
            spr = eye_spr; lvl = eye_lvl;
            h = (uint8_t)((spr << 4) + lvl + 1);      /* what is drawn: the sprite and its green level, in one byte */
            if (changed(e, fg) | (shown[e][1] != (char)h)) {
                shown[e][1] = (char)h;
                lcd_bitmap(x + UI_DOT_X, y + UI_DOT_Y - 1, spr_w[spr], spr_h[spr], SPR_BITS(spr), fg, bg);
                if (lvl) lcd_bitmap(x + UI_DOT_X, (uint8_t)(y + UI_DOT_Y - 1 + EYE_H - lvl), EYE_H, lvl, SPR_BITS(spr) + EYE_H - lvl, UI_C_NEON, bg);
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

static uint8_t quiet(const ui_state_t *s) REENT
{
    uint8_t p;
    if (s->run || s->ps || s->alarm || s->iset != saver_iset || s->volt != saver_volt) return 0;
    for (p = 0; p < NPORTS; p++) if (s->v[p] >= SAVER_EMPTY) return 0;
    return 1;
}

/* Four rows of the screensaver: static crawling down the screen, the green wordmark surfacing through it and sinking back
 * (a triangle wave of about 18 s).  Drawn one control pass in eight, about 7 ms each. */
static void saver_strip(void) REENT
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
    /* three small skulls drift across at their own speeds; static fills in behind them */
    for (r = 0; r < 3; r++) {
        y = (uint8_t)(((anim >> 3) * (r + 1u) + r * 53u) % (LCD_PX_W - 7));
        if (y > r) lcd_noise((uint8_t)(y - r - 1), (uint8_t)(6 + r * 30), (uint8_t)(r + 1), 7, anim);   /* static over where it just was */
        lcd_bitmap(y, (uint8_t)(6 + r * 30), 7, 7, SPR_BITS(SPR_SKULL_S), UI_C_NEON, th(R_BG));
    }
}

void ui_main_step(const ui_state_t *s)
{
    uint8_t e = next_el;
    anim++;
    if (look_n && e == 0) look_n--;                   /* counted in rounds of the screen (21 passes): about 4 s */
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
    if (ROT && e < 3 && !s->ps) { header_icon(s, e, box_bg[e]); if (e == 2) charge_bar(s); }   /* after element: less stack */
}
