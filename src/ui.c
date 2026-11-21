/* ui.c - widget kit implementation. */
#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <stdlib.h>

Fonts F;
static void *focus;
static void *order[40]; static int norder;
static float now;
static bool modal_on; static Rectangle modal_r;
static RenderTexture2D coin_face[2]; static bool coin_ok;
static void coin_bake(void);

static int CP[200]; static int NCP;
/* ---------------- UI scale ----------------
 * Everything is laid out on a logical canvas and drawn through a 2D camera with
 * zoom S, so the whole app grows to fill big screens. Fonts are rasterised at
 * S x their size, so text stays sharp instead of being stretched. */
static float S = 1, FS = 1, user_zoom = 1, want_since = -1, S_want = 1;
float ui_scale(void) { return S; }
float ui_w(void) { return GetScreenWidth() / S; }
float ui_h(void) { return GetScreenHeight() / S; }
float fsz(Font f) { return f.baseSize / FS; }
void ui_cam_begin(void) { BeginMode2D((Camera2D){ .zoom = S }); }
void ui_cam_end(void) { EndMode2D(); }
void ui_scissor(float x, float y, float w, float h) { BeginScissorMode((int)(x * S), (int)(y * S), (int)ceilf(w * S), (int)ceilf(h * S)); }

/* -------------------------------- sounds --------------------------------
 * Made from maths at start-up (no sound files): a soft tick for buttons and a
 * two-bell "ka-ching" for money going in. */
static Sound SND[3]; static bool snd_ok; bool UI_SOUND = true;
static Sound make_sound(int kind) {
    int rate = 44100, n = kind == 0 ? rate * 30 / 1000 : rate * 700 / 1000;
    short *d = MemAlloc(n * sizeof(short));
    unsigned seed = 1234;
    for (int i = 0; i < n; i++) {
        float t = i / (float)rate, v = 0;
        if (kind == 0) v = sinf(2 * PI * 1900 * t) * expf(-t * 260) * 0.22f;              /* click */
        else if (kind == 1) {                                                              /* ka-ching */
            float t2 = t - 0.075f;
            v += sinf(2 * PI * 2093 * t) * expf(-t * 7) * 0.20f + sinf(2 * PI * 4186 * t) * expf(-t * 11) * 0.08f;
            if (t2 > 0) v += sinf(2 * PI * 3136 * t2) * expf(-t2 * 6) * 0.22f + sinf(2 * PI * 6272 * t2) * expf(-t2 * 12) * 0.07f;
            seed = seed * 1103515245u + 12345u; float nz = ((seed >> 16) & 0x7fff) / 16384.0f - 1;
            v += nz * expf(-t * 40) * 0.10f;                                                /* the drawer's rattle */
        } else {                                                                           /* alert: two soft notes */
            float f = t < 0.18f ? 988 : 784, tt = t < 0.18f ? t : t - 0.18f;
            v = sinf(2 * PI * f * t) * expf(-tt * 9) * 0.18f;
        }
        v = v > 1 ? 1 : v < -1 ? -1 : v;
        d[i] = (short)(v * 32000);
    }
    Wave w = { (unsigned)n, (unsigned)rate, 16, 1, d };
    Sound s = LoadSoundFromWave(w);
    MemFree(d);
    return s;
}
static void sounds_init(void) {
    if (getenv("RL_SHOT")) return;
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    for (int k = 0; k < 3; k++) SND[k] = make_sound(k);
    SetSoundVolume(SND[0], 0.5f);
    snd_ok = true;
}
void ui_sound(int k) { if (snd_ok && UI_SOUND && k >= 0 && k < 3) PlaySound(SND[k]); }

/* ------------------------------- themes ------------------------------- *
 * PAPER: warm newsprint, ink type, serif headlines, marigold highlights.
 * NIGHT: the original dark look. */
int THEME = THEME_AMETHYST;
Color C_BG, C_BG2, C_CARD, C_CARD2, C_HOVER, C_BORDER, C_TEXT, C_DIM, C_FAINT,
      C_GAIN, C_LOSS, C_WARN, C_VIOLET, C_CYAN, C_BLUE, C_PINK, C_PRIMARY, C_ON_PRIMARY;
static void theme_colors(int t) {
    if (t == THEME_AMETHYST) {                       /* black + purple, big rounded cards, pill buttons */
        C_BG = (Color){ 9, 8, 13, 255 }; C_BG2 = (Color){ 14, 12, 20, 255 }; C_CARD = (Color){ 20, 18, 27, 255 }; C_CARD2 = (Color){ 30, 27, 40, 255 };
        C_HOVER = (Color){ 42, 36, 58, 255 }; C_BORDER = (Color){ 44, 39, 58, 255 }; C_TEXT = (Color){ 247, 245, 255, 255 }; C_DIM = (Color){ 165, 159, 184, 255 };
        C_FAINT = (Color){ 108, 103, 124, 255 }; C_GAIN = (Color){ 52, 211, 153, 255 }; C_LOSS = (Color){ 248, 113, 113, 255 }; C_WARN = (Color){ 251, 191, 36, 255 };
        C_VIOLET = (Color){ 139, 92, 246, 255 }; C_CYAN = (Color){ 192, 132, 252, 255 }; C_BLUE = (Color){ 99, 102, 241, 255 }; C_PINK = (Color){ 232, 121, 249, 255 };
        C_PRIMARY = (Color){ 132, 80, 245, 255 }; C_ON_PRIMARY = WHITE;
    } else if (t == THEME_NIGHT) {
        C_BG = (Color){ 7, 10, 19, 255 }; C_BG2 = (Color){ 11, 15, 27, 255 }; C_CARD = (Color){ 15, 20, 35, 255 }; C_CARD2 = (Color){ 21, 28, 46, 255 };
        C_HOVER = (Color){ 28, 36, 58, 255 }; C_BORDER = (Color){ 32, 41, 64, 255 }; C_TEXT = (Color){ 236, 241, 250, 255 }; C_DIM = (Color){ 141, 151, 176, 255 };
        C_FAINT = (Color){ 88, 98, 124, 255 }; C_GAIN = (Color){ 43, 212, 125, 255 }; C_LOSS = (Color){ 255, 90, 110, 255 }; C_WARN = (Color){ 255, 181, 71, 255 };
        C_VIOLET = (Color){ 124, 92, 255, 255 }; C_CYAN = (Color){ 34, 211, 238, 255 }; C_BLUE = (Color){ 79, 140, 255, 255 }; C_PINK = (Color){ 236, 72, 153, 255 };
        C_PRIMARY = C_VIOLET; C_ON_PRIMARY = WHITE;
    } else {
        C_BG = (Color){ 245, 241, 233, 255 }; C_BG2 = (Color){ 238, 233, 222, 255 }; C_CARD = (Color){ 253, 251, 247, 255 }; C_CARD2 = (Color){ 243, 238, 228, 255 };
        C_HOVER = (Color){ 234, 227, 213, 255 }; C_BORDER = (Color){ 222, 213, 197, 255 }; C_TEXT = (Color){ 28, 26, 23, 255 }; C_DIM = (Color){ 101, 94, 84, 255 };
        C_FAINT = (Color){ 150, 141, 128, 255 }; C_GAIN = (Color){ 27, 124, 72, 255 }; C_LOSS = (Color){ 180, 38, 30, 255 }; C_WARN = (Color){ 184, 112, 20, 255 };
        C_VIOLET = (Color){ 214, 124, 36, 255 };   /* marigold: the highlight colour */
        C_CYAN = (Color){ 34, 92, 140, 255 };      /* ink blue */
        C_BLUE = (Color){ 52, 110, 160, 255 }; C_PINK = (Color){ 150, 70, 110, 255 };
        C_PRIMARY = (Color){ 28, 26, 23, 255 }; C_ON_PRIMARY = (Color){ 253, 251, 247, 255 };
    }
}

static Font load(const char *file, int size) {
    if (size <= 13) size++;                            /* small print was too small */
    size = (int)roundf(size * FS);
    char p[600];
    snprintf(p, sizeof p, "%sassets/fonts/%s", GetApplicationDirectory(), file);
    if (!FileExists(p)) snprintf(p, sizeof p, "assets/fonts/%s", file);
    Font f = LoadFontEx(p, size, CP, NCP);
    if (f.texture.id == 0) return GetFontDefault();
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    return f;
}
static void load_fonts(void);
float ui_fit_scale(void) {
    float s = fminf(GetScreenWidth() / 1360.0f, GetScreenHeight() / 820.0f) * user_zoom;
    return s < 0.8f ? 0.8f : s > 2.4f ? 2.4f : s;
}
void brand_theme_changed(void);
void theme_set(int t) {
    if (t == THEME) return;
    THEME = t; theme_colors(t);
    Font *a = (Font *)&F; for (size_t i = 0; i < sizeof F / sizeof(Font); i++) UnloadFont(a[i]);
    load_fonts();
    brand_theme_changed();
}
void ui_zoom(float z) { user_zoom = z < 0.8f ? 0.8f : z > 1.35f ? 1.35f : z; }
float ui_zoom_get(void) { return user_zoom; }
void ui_init(void) {
    NCP = 0;
    for (int c = 32; c < 127; c++) CP[NCP++] = c;
    int ex[] = { 0x20B9, 0x2212, 0x2022, 0x2014, 0x2013, 0x2192, 0x2190, 0x2191, 0x2193, 0x00B7, 0x2026, 0x2713, 0x00D7, 0x2264, 0x2265, 0x00B1, 0x25B2, 0x25BC, 0x26A0 };
    for (size_t i = 0; i < sizeof ex / sizeof ex[0]; i++) CP[NCP++] = ex[i];
    theme_colors(THEME);
    sounds_init();
    S_want = S = FS = ui_fit_scale();
    load_fonts();
    coin_bake();
}
static const char *HB(const char *dflt) { return THEME == THEME_PAPER ? "OldStandard-Bold.ttf" : dflt; }
static const char *NUM(void) { return THEME == THEME_PAPER ? "SourceSerif4-Bold.ttf" : THEME == THEME_AMETHYST ? "Inter-Bold.ttf" : "JetBrainsMono-Medium.ttf"; }
static void load_fonts(void) {
    F.r12 = load("Inter-Regular.ttf", 12); F.r13 = load("Inter-Regular.ttf", 13); F.r14 = load("Inter-Regular.ttf", 14);
    F.r16 = load("Inter-Regular.ttf", 16); F.r18 = load("Inter-Regular.ttf", 18);
    F.m13 = load("Inter-Medium.ttf", 13); F.m14 = load("Inter-Medium.ttf", 14); F.m16 = load("Inter-Medium.ttf", 16);
    F.s13 = load("Inter-SemiBold.ttf", 13); F.s14 = load("Inter-SemiBold.ttf", 14); F.s16 = load("Inter-SemiBold.ttf", 16);
    F.s18 = load(HB("Inter-SemiBold.ttf"), THEME == THEME_PAPER ? 20 : 18); F.s20 = load(HB("Inter-SemiBold.ttf"), THEME == THEME_PAPER ? 22 : 20);
    F.s24 = load(HB("Inter-SemiBold.ttf"), THEME == THEME_PAPER ? 27 : 24);
    F.b28 = load(HB("Inter-Bold.ttf"), THEME == THEME_PAPER ? 34 : 28); F.b36 = load(HB("Inter-Bold.ttf"), 36); F.b48 = load(HB("Inter-Bold.ttf"), 48); F.b64 = load(HB("Inter-Bold.ttf"), 64);
    F.n12 = load("JetBrainsMono-Medium.ttf", 12); F.n14 = load(NUM(), THEME == THEME_PAPER ? 15 : 14); F.n18 = load(NUM(), THEME == THEME_PAPER ? 19 : 18);
    F.n24 = load(NUM(), THEME == THEME_PAPER ? 26 : 24); F.n32 = load(NUM(), THEME == THEME_PAPER ? 34 : 32);
    F.p14 = load("SourceSerif4-Regular.ttf", 14); F.p15 = load("SourceSerif4-Regular.ttf", 15);
    F.i15 = load("SourceSerif4-Italic.ttf", 15); F.i18 = load("SourceSerif4-Italic.ttf", 18);
    F.pb18 = load("SourceSerif4-Bold.ttf", 18); F.pb38 = load("SourceSerif4-Bold.ttf", 38);
    F.d56 = load("SourceSerif4Display-Black.ttf", 60);
    F.bl60 = load("UnifrakturMaguntia-Book.ttf", 64); F.hd64 = load("OldStandard-Bold.ttf", 66);
    F.hd44 = load("OldStandard-Bold.ttf", 42); F.hd22 = load("OldStandard-Bold.ttf", 21); F.hd17 = load("OldStandard-Bold.ttf", 17);
    F.hi18 = load("OldStandard-Italic.ttf", 18);
}
void ui_shutdown(void) {
    if (snd_ok) { for (int k = 0; k < 3; k++) UnloadSound(SND[k]); CloseAudioDevice(); }
    if (coin_ok) { UnloadRenderTexture(coin_face[0]); UnloadRenderTexture(coin_face[1]); } Font *a = (Font *)&F; for (size_t i = 0; i < sizeof F / sizeof(Font); i++) UnloadFont(a[i]); }

void ui_begin(void) {
    modal_on = false; now = (float)GetTime(); norder = 0; SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    /* Ctrl + / Ctrl - / Ctrl 0 : bigger or smaller text (handy on a projector) */
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) ui_zoom(user_zoom + 0.1f);
        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) ui_zoom(user_zoom - 0.1f);
        if (IsKeyPressed(KEY_ZERO)) ui_zoom(1);
    }
    float w = ui_fit_scale();
    if (fabsf(w - S_want) > 0.01f) { S_want = w; want_since = now; }
    if (fabsf(S_want - S) > 0.01f && now - want_since > 0.25f) {          /* settle after resizing, then re-rasterise */
        S = FS = S_want;
        Font *a = (Font *)&F; for (size_t i = 0; i < sizeof F / sizeof(Font); i++) UnloadFont(a[i]);
        load_fonts();
    }
    SetMouseScale(1 / S, 1 / S);
}
void ui_end(void) {
    if (IsKeyPressed(KEY_TAB) && norder) {
        int i = 0; while (i < norder && order[i] != focus) i++;
        focus = i >= norder ? order[0] : order[(i + 1) % norder];
    }
}
void ui_focus(void *b) { focus = b; }
bool ui_focused(void *b) { return focus == b; }
void ui_unfocus(void) { focus = NULL; }
float ui_time(void) { return now; }

/* ---------------------------------- text ---------------------------------- */
void text(Font f, const char *s, float x, float y, Color c) { DrawTextEx(f, s, (Vector2){ roundf(x * S) / S, roundf(y * S) / S }, fsz(f), 0, c); }
float text_w(Font f, const char *s) { return MeasureTextEx(f, s, fsz(f), 0).x; }
void text_r(Font f, const char *s, float r, float y, Color c) { text(f, s, r - text_w(f, s), y, c); }
void text_c(Font f, const char *s, float cx, float y, Color c) { text(f, s, cx - text_w(f, s) / 2, y, c); }
float text_wrap(Font f, const char *s, float x, float y, float w, Color c, float lh) {
    char line[512] = "", word[200], test[720];
    if (lh <= 0) lh = fsz(f) * 1.45f;
    float yy = y; const char *p = s;
    while (*p) {
        int n = 0; while (*p && *p != ' ' && n < 199) word[n++] = *p++;
        word[n] = 0; while (*p == ' ') p++;
        snprintf(test, sizeof test, "%s%s%s", line, line[0] ? " " : "", word);
        if (line[0] && text_w(f, test) > w) { text(f, line, x, yy, c); yy += lh; snprintf(line, sizeof line, "%s", word); }
        else snprintf(line, sizeof line, "%s", test);
    }
    if (line[0]) { text(f, line, x, yy, c); yy += lh; }
    return yy - y;
}

/* --------------------------------- shapes --------------------------------- */
Color fade(Color c, float a) { a = a < 0 ? 0 : a > 1 ? 1 : a; c.a = (unsigned char)(c.a * a); return c; }
Color mix(Color a, Color b, float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return (Color){ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t) }; }
static float rnd(Rectangle r, float rad) { float m = fminf(r.width, r.height); return m > 0 ? fminf(1, 2 * rad / m) : 0; }
static float RADK(void) { return THEME == THEME_PAPER ? 0.45f : THEME == THEME_AMETHYST ? 1.45f : 1.0f; }
void rrect(Rectangle r, float rad, Color c) { DrawRectangleRounded(r, rnd(r, rad * RADK()), 12, c); }
void rline(Rectangle r, float rad, Color c) { DrawRectangleRoundedLinesEx(r, rnd(r, rad * RADK()), 12, 1, c); }
void card(Rectangle r) {
    if (THEME == THEME_PAPER) {                      /* paper card: soft shadow, hairline border */
        rrect((Rectangle){ r.x + 1, r.y + 3, r.width, r.height }, 16, (Color){ 90, 70, 40, 14 });
        rrect((Rectangle){ r.x, r.y + 1, r.width, r.height }, 16, (Color){ 90, 70, 40, 10 });
    }
    rrect(r, 16, C_CARD);
    if (THEME == THEME_AMETHYST) {                   /* a soft purple sheen across the top of every card */
        float hh = fminf(120, r.height);
        for (int i = 0; i < 10; i++) rrect((Rectangle){ r.x, r.y, r.width, hh * (1 - i / 10.0f) }, 16, (Color){ 139, 92, 246, 4 });
        DrawRectangleGradientV((int)(r.x + 20), (int)r.y + 1, (int)(r.width - 40), 1, fade(C_CYAN, 0.35f), fade(C_CYAN, 0.35f));
    }
    rline(r, 16, C_BORDER);
}
/* A rounded card filled with a diagonal gradient (baked once per size, then reused). */
void grad_card(Rectangle r, float rad, Color c0, Color c1) {
    static struct { int w, h; Color a, b; Texture2D t; } cache[6]; static int nc;
    int W = (int)(r.width * S), H = (int)(r.height * S);
    if (W < 4 || H < 4) return;
    int k = 0;
    for (; k < nc; k++) if (cache[k].w == W && cache[k].h == H && !memcmp(&cache[k].a, &c0, 4) && !memcmp(&cache[k].b, &c1, 4)) break;
    if (k == nc) {
        if (nc == 6) { UnloadTexture(cache[0].t); memmove(cache, cache + 1, 5 * sizeof cache[0]); nc = 5; k = 5; }
        Image im = GenImageColor(W, H, BLANK); Color *px = im.data; float R = rad * S * RADK();
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            float qx = fabsf(x + 0.5f - W / 2.0f) - (W / 2.0f - R), qy = fabsf(y + 0.5f - H / 2.0f) - (H / 2.0f - R);
            float d = sqrtf(fmaxf(qx, 0) * fmaxf(qx, 0) + fmaxf(qy, 0) * fmaxf(qy, 0)) + fminf(fmaxf(qx, qy), 0) - R;
            float al = fminf(1, fmaxf(0, 0.5f - d)); if (al <= 0) continue;
            float g = fminf(1, fmaxf(0, (x / (float)W) * 0.65f + (y / (float)H) * 0.35f));
            float sheen = fmaxf(0, 1 - y / (H * 0.5f)) * 0.08f;
            Color o = { (unsigned char)fminf(255, c0.r + (c1.r - c0.r) * g + 255 * sheen), (unsigned char)fminf(255, c0.g + (c1.g - c0.g) * g + 255 * sheen),
                        (unsigned char)fminf(255, c0.b + (c1.b - c0.b) * g + 255 * sheen), (unsigned char)(255 * al) };
            px[y * W + x] = o;
        }
        cache[k].w = W; cache[k].h = H; cache[k].a = c0; cache[k].b = c1; cache[k].t = LoadTextureFromImage(im); UnloadImage(im);
        SetTextureFilter(cache[k].t, TEXTURE_FILTER_BILINEAR);
        if (k == nc) nc++;
    }
    DrawTexturePro(cache[k].t, (Rectangle){ 0, 0, (float)W, (float)H }, r, (Vector2){ 0 }, 0, WHITE);
}

void glow(float cx, float cy, float rad, Color c, float a) {
    if (THEME == THEME_PAPER) a *= 0.22f;            /* no neon halos on paper */
    DrawCircleGradient((int)cx, (int)cy, rad, fade(c, a), fade(c, 0));
}
void grad_rect(Rectangle r, float rad, Color a, Color b) {
    ui_scissor(r.x, r.y, r.width, r.height);
    int steps = 24;
    for (int i = 0; i < steps; i++) {
        Rectangle s = { r.x + r.width * i / steps, r.y, r.width / steps + 1, r.height };
        DrawRectangleRec(s, mix(a, b, (float)i / (steps - 1)));
    }
    EndScissorMode();
    /* round the corners by painting background-coloured corner masks */
    (void)rad;
}
void ui_modal(bool on, Rectangle r) { modal_on = on; modal_r = r; }
bool hover(Rectangle r) {
    Vector2 m = GetMousePosition();
    if (modal_on && !CheckCollisionPointRec(m, modal_r)) return false;     /* a drawer is open: only it reacts */
    return CheckCollisionPointRec(m, r);
}
bool clicked(Rectangle r) { return hover(r) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT); }

bool button(Rectangle r, const char *label, int style) {
    bool h = hover(r);
    if (h && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { r.x += 1.5f; r.y += 1.5f; r.width -= 3; r.height -= 3; }   /* pressed */
    Font f = r.height >= 44 ? F.s16 : F.s14;
    switch (style) {
    case B_PRIMARY: {
        if (h) glow(r.x + r.width / 2, r.y + r.height / 2, r.width * 0.6f, C_PRIMARY, 0.25f);
        Color pc = C_PRIMARY;
        if (h) pc = THEME == THEME_PAPER ? (Color){ 60, 54, 46, 255 } : (Color){ 142, 114, 255, 255 };
        if (THEME == THEME_AMETHYST) {               /* pill with a purple gradient */
            grad_card(r, r.height / 2 / 1.45f, h ? (Color){ 140, 90, 250, 255 } : (Color){ 118, 64, 238, 255 }, h ? (Color){ 210, 120, 252, 255 } : (Color){ 186, 100, 250, 255 });
        } else rrect(r, 10, pc);
        if (THEME == THEME_PAPER) DrawRectangle((int)(r.x + 14), (int)(r.y + r.height - 3), (int)(r.width - 28), 2, C_VIOLET);   /* marigold underline */
        else rrect((Rectangle){ r.x + 1, r.y + 1, r.width - 2, r.height / 2 }, 9, fade(WHITE, 0.035f));
        text_c(f, label, r.x + r.width / 2, r.y + (r.height - fsz(f)) / 2 - 1, C_ON_PRIMARY);
        break;
    }
    case B_SECONDARY: { float rd = THEME == THEME_AMETHYST ? r.height / 2 / 1.45f : 10; rrect(r, rd, h ? C_HOVER : C_CARD2); rline(r, rd, C_BORDER); } text_c(f, label, r.x + r.width / 2, r.y + (r.height - fsz(f)) / 2 - 1, C_TEXT); break;
    case B_GHOST: if (h) rrect(r, 10, C_CARD2); text_c(f, label, r.x + r.width / 2, r.y + (r.height - fsz(f)) / 2 - 1, h ? C_TEXT : C_DIM); break;
    case B_DANGER: rrect(r, 10, h ? C_LOSS : fade(C_LOSS, 0.18f)); text_c(f, label, r.x + r.width / 2, r.y + (r.height - fsz(f)) / 2 - 1, h ? WHITE : C_LOSS); break;
    default: text_c(f, label, r.x + r.width / 2, r.y + (r.height - fsz(f)) / 2 - 1, h ? C_CYAN : C_VIOLET); break;
    }
    if (h) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    bool clk = h && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    if (clk) ui_sound(0);
    return clk;
}

bool chip(Rectangle r, const char *label, bool on) {
    bool h = hover(r);
    rrect(r, r.height / 2, on ? fade(C_VIOLET, 0.25f) : h ? C_HOVER : C_CARD2);
    rline(r, r.height / 2, on ? C_VIOLET : C_BORDER);
    text_c(F.m13, label, r.x + r.width / 2, r.y + (r.height - 13) / 2 - 1, on ? C_TEXT : C_DIM);
    if (h) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    if (h && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { ui_sound(0); return true; }
    return false;
}

bool input(Rectangle r, char *buf, int cap, const char *ph, int kind) {
    if (norder < 40) order[norder++] = buf;
    bool h = hover(r), foc = focus == buf, enter = false;
    if (h) SetMouseCursor(MOUSE_CURSOR_IBEAM);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { if (h) focus = buf; else if (foc) focus = NULL; foc = focus == buf; }
    rrect(r, 10, C_BG2);
    rline(r, 10, foc ? C_VIOLET : C_BORDER);
    if (foc) { int len = (int)strlen(buf), ch;
        while ((ch = GetCharPressed()) > 0) {
            if (ch < 32 || ch > 126 || len >= cap - 1 || ch == '|') continue;
            if ((kind == I_MONEY || kind == I_NUMBER) && (ch < '0' || ch > '9')) continue;
            buf[len++] = (char)ch; buf[len] = 0;
        }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && len) buf[--len] = 0;
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) enter = true;
        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER)) && IsKeyPressed(KEY_V)) {
            const char *clip = GetClipboardText();
            if (clip) for (const char *c = clip; *c && len < cap - 1; c++) if (*c >= 32 && *c < 127 && *c != '|') { buf[len++] = *c; buf[len] = 0; }
        }
    }
    char shown[200];
    if (kind == I_PASSWORD) { int n = (int)strlen(buf); if (n > 24) n = 24; for (int i = 0; i < n; i++) shown[i] = '*'; shown[n] = 0; }
    else if (kind == I_MONEY && buf[0]) { long long v = atoll(buf); snprintf(shown, sizeof shown, "%s", inr((double)v) + 3); }
    else snprintf(shown, sizeof shown, "%s", buf);
    Font f = r.height >= 48 ? F.m16 : F.r14;
    float ty = r.y + (r.height - fsz(f)) / 2 - 1, pad = 14;
    if (kind == I_MONEY) { text(f, RS, r.x + 14, ty, C_FAINT); pad = 30; }
    ui_scissor(r.x + 4, r.y, r.width - 8, r.height);
    float tw = text_w(f, shown), off = tw > r.width - pad - 18 ? tw - (r.width - pad - 18) : 0;
    if (buf[0]) text(f, shown, r.x + pad - off, ty, C_TEXT); else text(f, ph, r.x + pad, ty, C_FAINT);
    if (foc && fmodf(now, 1.0f) < 0.55f) DrawRectangle((int)(r.x + pad + tw - off + 1), (int)ty, 2, (int)fsz(f) + 2, C_CYAN);
    EndScissorMode();
    return enter;
}

bool slider_i(Rectangle r, int *v, int lo, int hi) {
    static int *dragging;
    float f = (float)(*v - lo) / (hi - lo);
    Rectangle track = { r.x, r.y + r.height / 2 - 3, r.width, 6 };
    rrect(track, 3, C_CARD2);
    rrect((Rectangle){ track.x, track.y, track.width * f, 6 }, 3, C_VIOLET);
    float kx = r.x + r.width * f, ky = r.y + r.height / 2;
    bool h = CheckCollisionPointCircle(GetMousePosition(), (Vector2){ kx, ky }, 14) || hover(r);
    glow(kx, ky, 22, C_VIOLET, h || dragging == v ? 0.5f : 0.25f);
    DrawCircle((int)kx, (int)ky, 10, WHITE);
    DrawCircle((int)kx, (int)ky, 5, C_VIOLET);
    if (h) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    if (h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) dragging = v;
    if (dragging == v) {
        float t = (GetMousePosition().x - r.x) / r.width; t = t < 0 ? 0 : t > 1 ? 1 : t;
        int nv = lo + (int)roundf(t * (hi - lo));
        bool ch = nv != *v; *v = nv;
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) dragging = NULL;
        return ch;
    }
    return false;
}

float pill_w(const char *s) { return text_w(F.s13, s) + 18; }
void pill(float x, float y, const char *s, Color fg) {
    float w = pill_w(s);
    rrect((Rectangle){ x, y, w, 22 }, 11, fade(fg, 0.16f));
    text(F.s13, s, x + 9, y + 4, fg);
}

void ring(float cx, float cy, float r, float th, float frac, Color c, Color bg) {
    DrawRing((Vector2){ cx, cy }, r - th, r, 0, 360, 72, bg);
    if (frac > 0) DrawRing((Vector2){ cx, cy }, r - th, r, -90, -90 + 360 * fminf(frac, 1), 72, c);
}

void donut(float cx, float cy, float r, float th, const float *fr, const Color *col, int n, float anim) {
    float a = -90, total = 360 * (anim > 1 ? 1 : anim);
    for (int i = 0; i < n; i++) {
        float sweep = 360 * fr[i];
        if (a + 90 >= total) break;
        float end = fminf(a + sweep, -90 + total);
        if (end - a > 1.2f) DrawRing((Vector2){ cx, cy }, r - th, r, a + 0.6f, end - 0.6f, 48, col[i]);
        a += sweep;
    }
}

void gauge(float cx, float cy, float r, float v, float anim) {
    const Color stops[4] = { C_GAIN, C_CYAN, C_WARN, C_LOSS };
    for (int i = 0; i < 60; i++) {
        float t = i / 60.0f, a0 = 180 + 180 * t, a1 = 180 + 180 * (i + 1) / 60.0f;
        int k = (int)(t * 3); float lt = t * 3 - k;
        Color c = mix(stops[k], stops[k + 1 > 3 ? 3 : k + 1], lt);
        DrawRing((Vector2){ cx, cy }, r - 18, r, a0, a1 + 0.5f, 4, fade(c, 0.9f));
    }
    float vv = v * (anim > 1 ? 1 : anim);
    float ang = (180 + 180 * vv) * DEG2RAD;
    Vector2 tip = { cx + cosf(ang) * (r - 26), cy + sinf(ang) * (r - 26) };
    DrawLineEx((Vector2){ cx, cy }, tip, 4, C_TEXT);
    DrawCircle((int)cx, (int)cy, 10, C_TEXT);
    DrawCircle((int)cx, (int)cy, 5, C_BG);
}

void progress(Rectangle r, float f, Color c) {
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    rrect(r, r.height / 2, C_CARD2);
    if (f > 0) rrect((Rectangle){ r.x, r.y, fmaxf(r.width * f, r.height), r.height }, r.height / 2, c);
}

void sparkline(Rectangle r, const float *v, int n, Color c) {
    if (n < 2) return;
    float lo = 1e30f, hi = -1e30f;
    for (int i = 0; i < n; i++) { lo = fminf(lo, v[i]); hi = fmaxf(hi, v[i]); }
    if (hi - lo < 1e-6f) hi = lo + 1;
    Vector2 prev = { 0 };
    for (int i = 0; i < n; i++) {
        Vector2 p = { r.x + r.width * i / (n - 1), r.y + r.height - (v[i] - lo) / (hi - lo) * r.height };
        if (i) DrawLineEx(prev, p, 1.5f, c);
        prev = p;
    }
}

/* ------------------------- the revolving rupee coin -------------------------
 * Faces are drawn once into textures, then squeezed horizontally by |cos(angle)|
 * every frame, with a darker rim for thickness and a moving glint. */
static void coin_bake(void) {
    const int S = 192; const float c = S / 2.0f;
    for (int side = 0; side < 2; side++) {
        coin_face[side] = LoadRenderTexture(S, S);
        SetTextureFilter(coin_face[side].texture, TEXTURE_FILTER_BILINEAR);
        BeginTextureMode(coin_face[side]);
        ClearBackground(BLANK);
        DrawCircleGradient((int)c, (int)c, c, (Color){ 255, 222, 128, 255 }, (Color){ 196, 140, 32, 255 });
        DrawRing((Vector2){ c, c }, c - 14, c - 9, 0, 360, 64, (Color){ 168, 112, 20, 255 });
        for (int k = 0; k < 48; k++) { float an = k * 7.5f * DEG2RAD; DrawCircle((int)(c + cosf(an) * (c - 4.5f)), (int)(c + sinf(an) * (c - 4.5f)), 2.2f, (Color){ 214, 160, 50, 255 }); }
        if (side == 0) {
            Font f = F.b64; const char *g = "\xe2\x82\xb9";
            Vector2 sz = MeasureTextEx(f, g, 96, 0);
            DrawTextEx(f, g, (Vector2){ c - sz.x / 2 + 3, c - sz.y / 2 + 4 }, 96, 0, (Color){ 150, 98, 14, 200 });
            DrawTextEx(f, g, (Vector2){ c - sz.x / 2, c - sz.y / 2 }, 96, 0, (Color){ 255, 246, 210, 255 });
        } else {
            DrawRing((Vector2){ c, c - 6 }, 26, 34, 0, 360, 48, (Color){ 255, 246, 210, 255 });
            DrawLineEx((Vector2){ c + 20, c + 14 }, (Vector2){ c + 40, c + 34 }, 9, (Color){ 255, 246, 210, 255 });
        }
        EndTextureMode();
    }
    coin_ok = true;
}
void coin(float cx, float cy, float r, float t) {
    if (!coin_ok) coin_bake();
    float an = t * 3.2f, cs = cosf(an), w = fabsf(cs);
    float thick = fmaxf(2, r * 0.16f) * sinf(an);
    DrawEllipse((int)cx, (int)(cy + r * 1.25f), r * 0.8f * (0.6f + 0.4f * w), r * 0.14f, fade(BLACK, 0.25f));
    /* rim */
    for (int k = 0; k < 6; k++) {
        float o = thick * k / 6.0f;
        DrawEllipse((int)(cx + o), (int)cy, fmaxf(1.5f, r * w), r, (Color){ 150, 100, 18, 255 });
    }
    RenderTexture2D *tx = &coin_face[cs >= 0 ? 0 : 1];
    Rectangle src = { 0, 0, 192, -192 };
    if (cs < 0) src.width = -192;                       /* back face reads correctly */
    DrawTexturePro(tx->texture, src, (Rectangle){ cx + thick, cy, 2 * r * w, 2 * r }, (Vector2){ r * w, r }, 0, WHITE);
    /* glint sweeping across as it turns */
    float gx = cx + thick + sinf(an * 2) * r * w * 0.5f;
    if (w > 0.25f) DrawEllipse((int)gx, (int)(cy - r * 0.35f), r * w * 0.12f, r * 0.42f, fade(WHITE, 0.22f * w));
}
void spinner(float cx, float cy, float r, Color c) { (void)c; coin(cx, cy, r * 1.1f, now); }

/* --------------------------------- toasts --------------------------------- */
typedef struct { char s[200]; Color c; float born; } Toast;
static Toast T[5]; static int NT;
void toast(Color c, const char *fmt, ...) {
    if (NT == 5) { memmove(T, T + 1, 4 * sizeof(Toast)); NT = 4; }
    va_list ap; va_start(ap, fmt); vsnprintf(T[NT].s, sizeof T[NT].s, fmt, ap); va_end(ap);
    T[NT].c = c; T[NT].born = now; NT++;
}
void toasts_draw(void) {
    float y = ui_h() - 24;
    for (int i = NT - 1; i >= 0; i--) {
        float age = now - T[i].born; if (age > 4.5f) continue;
        float a = age < 0.15f ? age / 0.15f : age > 3.8f ? (4.5f - age) / 0.7f : 1;
        float w = text_w(F.m14, T[i].s) + 52;
        Rectangle r = { 248 + (ui_w() - 248 - w) / 2, y - 46, w, 44 };
        rrect(r, 12, fade(C_CARD2, a)); rline(r, 12, fade(T[i].c, 0.6f * a));
        DrawCircle((int)r.x + 22, (int)r.y + 22, 5, fade(T[i].c, a));
        text(F.m14, T[i].s, r.x + 36, r.y + 14, fade(C_TEXT, a));
        y -= 52;
    }
    int k = 0; for (int i = 0; i < NT; i++) if (now - T[i].born <= 4.5f) T[k++] = T[i];
    NT = k;
}

/* ------------------------------- formatting ------------------------------- */
static char *ringbuf(void) { static char b[24][48]; static int i; return b[i++ % 24]; }
static void group(char *out, long long v) {
    char d[32]; snprintf(d, sizeof d, "%lld", v);
    int n = (int)strlen(d), k = 0;
    for (int i = 0; i < n; i++) { out[k++] = d[i]; int rem = n - 1 - i; if (rem > 0 && (rem == 3 || (rem > 3 && (rem - 3) % 2 == 0))) out[k++] = ','; }
    out[k] = 0;
}
const char *inr(double v) { char *b = ringbuf(), g[40]; group(g, (long long)llround(fabs(v))); snprintf(b, 48, "%s" RS "%s", v < -0.5 ? "-" : "", g); return b; }
const char *inr_signed(double v) { char *b = ringbuf(), g[40]; group(g, (long long)llround(fabs(v))); snprintf(b, 48, "%s" RS "%s", v < 0 ? "\xe2\x88\x92" : "+", g); return b; }
const char *inr_short(double v) {
    char *b = ringbuf(); double a = fabs(v);
    const char *s = v < 0 ? "-" : "";
    if (a >= 1e7) snprintf(b, 48, "%s" RS "%.2fCr", s, a / 1e7);
    else if (a >= 1e5) snprintf(b, 48, "%s" RS "%.1fL", s, a / 1e5);
    else if (a >= 1e3) snprintf(b, 48, "%s" RS "%.1fK", s, a / 1e3);
    else snprintf(b, 48, "%s" RS "%.0f", s, a);
    return b;
}
const char *pct(double v) { char *b = ringbuf(); snprintf(b, 48, "%+.1f%%", v * 100); return b; }
Color risk_color(float vol) { return vol < 0.20f ? C_GAIN : vol < 0.32f ? C_WARN : C_LOSS; }
const char *risk_word(float vol) { return vol < 0.20f ? "Low risk" : vol < 0.32f ? "Medium risk" : "High risk"; }
