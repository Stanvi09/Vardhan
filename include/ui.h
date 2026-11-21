/* ui.h - Vardhan widget kit (raylib), dark theme. */
#ifndef UI_H
#define UI_H

#include "raylib.h"
#include <stdbool.h>

/* Colours come from the active theme (see theme_set). */
extern Color C_BG, C_BG2, C_CARD, C_CARD2, C_HOVER, C_BORDER, C_TEXT, C_DIM, C_FAINT,
             C_GAIN, C_LOSS, C_WARN, C_VIOLET, C_CYAN, C_BLUE, C_PINK, C_PRIMARY, C_ON_PRIMARY;
enum { THEME_PAPER = 0, THEME_NIGHT = 1, THEME_AMETHYST = 2 };
extern int THEME;
void theme_set(int t);              /* switches colours and fonts */

#define RS "\xe2\x82\xb9"

typedef struct {
    Font r12, r13, r14, r16, r18;
    Font m13, m14, m16;
    Font s13, s14, s16, s18, s20, s24;
    Font b28, b36, b48, b64;
    Font n12, n14, n18, n24, n32;          /* numbers: JetBrains Mono */
    Font p14, p15, i15, i18, pb18, pb38, d56; /* newspaper: Source Serif 4 */
    Font bl60, hd44, hd22, hd17, hi18, hd64;   /* newspaper: blackletter nameplate, Old Standard headlines */
} Fonts;
extern Fonts F;

enum { B_PRIMARY = 0, B_SECONDARY, B_GHOST, B_DANGER, B_LINK };
enum { I_TEXT = 0, I_MONEY, I_PASSWORD, I_NUMBER };

void  ui_init(void);
void  ui_shutdown(void);
void  ui_begin(void);
void  ui_end(void);
void  ui_focus(void *buf);
bool  ui_focused(void *buf);
void  ui_unfocus(void);
float ui_time(void);

void  text(Font f, const char *s, float x, float y, Color c);
void  text_r(Font f, const char *s, float r, float y, Color c);
void  text_c(Font f, const char *s, float cx, float y, Color c);
float text_w(Font f, const char *s);
float text_wrap(Font f, const char *s, float x, float y, float w, Color c, float lh);

void  rrect(Rectangle r, float rad, Color c);
void  rline(Rectangle r, float rad, Color c);
void  card(Rectangle r);
void  glow(float cx, float cy, float rad, Color c, float a);
void  grad_rect(Rectangle r, float rad, Color a, Color b);       /* horizontal gradient */
bool  hover(Rectangle r);
void  ui_modal(bool on, Rectangle r);
void  ui_sound(int k);        /* 0 click, 1 ka-ching, 2 alert */
extern bool UI_SOUND;
void  grad_card(Rectangle r, float rad, Color c0, Color c1);   /* rounded gradient card */
/* scale: the app draws on a logical canvas, zoomed to fit the window */
float ui_scale(void); float ui_w(void); float ui_h(void); float fsz(Font f);
void  ui_cam_begin(void); void ui_cam_end(void); void ui_scissor(float x, float y, float w, float h);
float ui_fit_scale(void); void ui_zoom(float z); float ui_zoom_get(void);   /* while on, only clicks inside r count */
bool  clicked(Rectangle r);
Color fade(Color c, float a);
Color mix(Color a, Color b, float t);

bool  button(Rectangle r, const char *label, int style);
bool  chip(Rectangle r, const char *label, bool on);
bool  input(Rectangle r, char *buf, int cap, const char *ph, int kind);
bool  slider_i(Rectangle r, int *v, int lo, int hi);
void  pill(float x, float y, const char *s, Color fg);
float pill_w(const char *s);
void  ring(float cx, float cy, float r, float thick, float frac, Color c, Color bg);
void  donut(float cx, float cy, float r, float thick, const float *fr, const Color *col, int n, float anim);
void  gauge(float cx, float cy, float r, float v, float anim);
void  progress(Rectangle r, float f, Color c);
void  sparkline(Rectangle r, const float *v, int n, Color c);
void  spinner(float cx, float cy, float r, Color c);  /* the revolving rupee coin */
void  coin(float cx, float cy, float r, float t);

void  toast(Color c, const char *fmt, ...);
void  toasts_draw(void);

const char *inr(double v);                 /* "₹1,23,456" */
const char *inr_signed(double v);          /* "+₹1,234" / "−₹1,234" */
const char *inr_short(double v);           /* "₹12.4L", "₹1.2Cr" */
const char *pct(double v);                 /* "+12.3%" */
Color risk_color(float vol);
const char *risk_word(float vol);

#endif
