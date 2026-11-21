/*
 * paper.c - "The Vardhan Times".
 *
 * Ask "why?" anywhere a gain or loss depends on the news and the AI's reading
 * opens as a one-page newspaper: a headline, what happened, what it means for
 * YOUR money (bad year before vs after the news), the price chart and the
 * actual headlines the AI read. Other holdings appear as "Also in this edition".
 */
#include "app.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <stdlib.h>

#define PAPER  (Color){ 244, 239, 228, 255 }
#define PAPER2 (Color){ 234, 227, 212, 255 }
#define INK    (Color){  26,  24,  22, 255 }
#define INK2   (Color){  92,  86,  78, 255 }
#define INK3   (Color){ 150, 142, 130, 255 }
#define IRED    (Color){ 168,  38,  30, 255 }
#define IGREEN  (Color){  30, 104,  58, 255 }

static RenderTexture2D rt; static bool rt_ok;
static Outcome o_now, o_before;
static float sw_t = -10;                                  /* story switch time */
static int others[4], nothers;
static int order[MAX_STOCKS], norder_;            /* every story in this edition: your holdings, then red flags */

static float ease3(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return 1 - powf(1 - t, 3); }
static Rectangle paper_rect(void) {
    float W = (float)ui_w(), H = (float)ui_h();
    float w = fminf(1280, W - 100), h = fminf(940, H - 30);
    return (Rectangle){ (W - w) / 2, (H - h) / 2, w, h };
}

/* headline written from the AI's factor categories */
static void headline(const Stock *s, char *out, int n) {
    static const char *cat[] = { "Leadership", "Earnings", "Geopolitics", "Regulation", "Macro", "Sector", "Operations" };
    static const char *neg[] = { "leadership turmoil", "weak earnings", "geopolitical tension", "regulatory pressure", "a tough economy", "sector worries", "operational setbacks" };
    static const char *pos[] = { "a leadership boost", "strong earnings", "easing tensions", "friendly regulation", "a supportive economy", "sector tailwinds", "operational wins" };
    static const char *noun[] = { "leadership", "earnings", "geopolitics", "regulation", "the economy", "the sector", "operations" };
    int bn = -1, bn2 = -1, bp = -1, vn = 0, vn2 = 0, vp = 0;
    for (int f = 0; f < s->ai.nf; f++) {
        int c = 6; for (int k = 0; k < 7; k++) if (!strcmp(s->ai.f[f].category, cat[k])) c = k;
        int im = s->ai.f[f].impact;
        if (im < 0 && -im > vn) { if (bn >= 0 && bn != c) { bn2 = bn; vn2 = vn; } bn = c; vn = -im; }
        else if (im < 0 && -im > vn2 && c != bn) { bn2 = c; vn2 = -im; }
        if (im > 0 && im > vp) { bp = c; vp = im; }
    }
    char poss[64]; size_t L = strlen(s->name);
    snprintf(poss, sizeof poss, "%s%s", s->name, L && s->name[L - 1] == 's' ? "'" : "'s");
    char A[48], B[48];
    if (bn >= 0 && bn2 >= 0 && vn2 >= vp) {
        snprintf(A, sizeof A, "%s", neg[bn]); A[0] = (char)toupper((unsigned char)A[0]);
        snprintf(out, n, "%s and %s cloud %s year", A, neg[bn2], poss);
    } else if (bn >= 0 && bp >= 0) {
        if (bn == bp) snprintf(out, n, "%s: mixed signals on %s", s->name, noun[bn]);
        else { snprintf(A, sizeof A, "%s", pos[bp]); snprintf(B, sizeof B, "%s", neg[bn]); snprintf(out, n, "%s: %s, but %s", s->name, A, B); }
    } else if (bn >= 0) snprintf(out, n, "%s under pressure from %s", s->name, neg[bn]);
    else if (bp >= 0) snprintf(out, n, "%s outlook brightens on %s", poss, pos[bp]);
    else snprintf(out, n, "A quiet fortnight for %s", s->name);
}

static void compute(App *a) {
    Universe *u = a->u; int i = a->paper_stock;
    risk_stock_outcome(u, i, (float)a->paper_amt, 252, &o_now, 7);
    bool v = u->s[i].ai.valid; u->s[i].ai.valid = false;
    risk_stock_outcome(u, i, (float)a->paper_amt, 252, &o_before, 7);
    u->s[i].ai.valid = v;
    /* "Red flags": the stocks with the worst news right now (the ones Vardhan steers you away from) */
    nothers = 0;
    int idx[MAX_STOCKS], m = 0;
    for (int s = 0; s < u->n; s++) if (s != i && !u->s[s].hidden && u->s[s].ai.valid && u->s[s].ai.nf) idx[m++] = s;
    for (int x = 1; x < m; x++) for (int y = x; y > 0 && u->s[idx[y]].ai.sentiment < u->s[idx[y - 1]].ai.sentiment; y--) { int t = idx[y]; idx[y] = idx[y - 1]; idx[y - 1] = t; }
    for (int k = 0; k < m && nothers < 4; k++) others[nothers++] = idx[k];
}

static bool in_plan(App *a, int s) { for (int k = 0; k < a->plan->npicks; k++) if (a->plan->picks[k].stock == s) return true; return false; }
static void build_order(App *a) {
    Universe *u = a->u; norder_ = 0;
    for (int k = 0; k < a->plan->npicks; k++) { int s = a->plan->picks[k].stock; if (s >= 0) order[norder_++] = s; }
    int idx[MAX_STOCKS], m = 0;
    for (int s = 0; s < u->n; s++) if (!u->s[s].hidden && !in_plan(a, s)) idx[m++] = s;
    for (int x = 1; x < m; x++) for (int y = x; y > 0 && (u->s[idx[y]].ai.valid ? u->s[idx[y]].ai.sentiment : 0) < (u->s[idx[y - 1]].ai.valid ? u->s[idx[y - 1]].ai.sentiment : 0); y--) { int t = idx[y]; idx[y] = idx[y - 1]; idx[y - 1] = t; }
    for (int k = 0; k < m; k++) order[norder_++] = idx[k];
}
static void go_story(App *a, int s, long long amt);

void paper_open(App *a, int stock, long long amount) {
    if (stock < 0 || stock >= a->u->n) return;
    if (amount <= 0) {
        amount = 50000;
        for (int k = 0; k < a->plan->npicks; k++) if (a->plan->picks[k].stock == stock && a->plan->picks[k].amount > 0) amount = a->plan->picks[k].amount;
    }
    a->paper_stock = stock; a->paper_amt = amount; a->paper_on = true; a->paper_t = ui_time();
    build_order(a);
    compute(a);
}

static long long amount_for(App *a, int s) {
    for (int k = 0; k < a->plan->npicks; k++) if (a->plan->picks[k].stock == s && a->plan->picks[k].amount > 0) return a->plan->picks[k].amount;
    return 50000;
}

static void go_story(App *a, int s, long long amt) {
    if (s == a->paper_stock) return;
    a->paper_stock = s; a->paper_amt = amt > 0 ? amt : amount_for(a, s); compute(a); sw_t = ui_time();
}

static float wrap(Font f, const char *s, float x, float y, float w, Color c, float lh) { return text_wrap(f, s, x, y, w, c, lh); }
static void rule(float x, float y, float w, float th) { DrawRectangle((int)x, (int)y, (int)w, (int)fmaxf(1, th), INK); }

/* ------------------------------------------------------------------------ *
 *  Typesetting: justified text flowing down narrow columns, with a drop cap.
 * ------------------------------------------------------------------------ */
typedef struct { float x, y, w, bottom; } Col;

/* Lays out `src` (paragraphs separated by '\n') across columns. Returns the y
 * where text ended in the last column used (and that column's index in *last). */
static float flow(const char *src, Col *cols, int ncols, Font f, float lh, Color ink, bool dropcap, int *last) {
    bool draw = ink.a > 0;
    static char buf[4000]; snprintf(buf, sizeof buf, "%s", src);
    float sp = text_w(f, " ");
    int ci = 0; float y = cols[0].y;
    float dcw = 0; int dclines = 0;
    char *para = buf; bool first = true;
    while (para && *para && ci < ncols) {
        char *nl = strchr(para, '\n'); if (nl) *nl = 0;
        /* split paragraph into words */
        char *words[400]; int nw = 0;
        for (char *t = strtok(para, " "); t && nw < 400; t = strtok(NULL, " ")) words[nw++] = t;
        if (first && dropcap && nw) {                      /* big first letter, three lines deep */
            char dc[2] = { words[0][0], 0 }; words[0]++;
            dcw = text_w(F.hd64, dc) + 6; dclines = 3;
            if (draw) text(F.hd64, dc, cols[0].x, y - 6, ink);
        }
        int wi = 0; bool pstart = true;
        while (wi < nw && ci < ncols) {
            Col *C = &cols[ci];
            if (y + lh > C->bottom) { ci++; if (ci >= ncols) break; y = cols[ci].y; continue; }
            float ind = (dclines > 0 && ci == 0) ? dcw : (pstart && !first ? 14 : 0);
            float avail = C->w - ind, used = 0; int a0 = wi, cnt = 0;
            while (wi < nw) {
                float ww = text_w(f, words[wi]);
                if (cnt && used + sp + ww > avail) break;
                used += (cnt ? sp : 0) + ww; wi++; cnt++;
            }
            bool lastline = wi >= nw;
            float gap = (!lastline && cnt > 1) ? sp + (avail - used) / (cnt - 1) : sp;
            if (gap > sp * 3.2f) gap = sp;                   /* don't stretch a loose line too far */
            float x = C->x + ind;
            if (draw) for (int k = a0; k < wi; k++) { text(f, words[k], x, y, ink); x += text_w(f, words[k]) + gap; }
            y += lh; pstart = false;
            if (dclines > 0 && ci == 0) dclines--;
        }
        first = false;
        para = nl ? nl + 1 : NULL;
    }
    if (last) *last = ci < ncols ? ci : ncols - 1;
    return y;
}

/* a printed photo: the price chart with halftone shading */
static void halftone_chart(const Stock *s, Rectangle r) {
    DrawRectangleRec(r, (Color){ 226, 219, 204, 255 });
    int n0 = s->n > 126 ? s->n - 126 : 0;
    float lo = 1e30f, hi = -1e30f;
    for (int k = n0; k < s->n; k++) { lo = fminf(lo, s->close[k]); hi = fmaxf(hi, s->close[k]); }
    if (hi <= lo) return;
    float pad = (hi - lo) * 0.12f; lo -= pad; hi += pad;
    #define PX(k) (r.x + 6 + ((k) - n0) / (float)(s->n - 1 - n0) * (r.width - 12))
    #define PY(v) (r.y + 6 + (1 - ((v) - lo) / (hi - lo)) * (r.height - 12))
    /* dots: bigger lower down, only under the price line */
    for (float x = r.x + 4; x < r.x + r.width - 4; x += 5) {
        int k = n0 + (int)((x - r.x - 6) / (r.width - 12) * (s->n - 1 - n0)); if (k < n0) k = n0; if (k >= s->n) k = s->n - 1;
        float top = PY(s->close[k]);
        for (float y = r.y + r.height - 4; y > top; y -= 5) {
            float d = (y - top) / (r.y + r.height - top + 1);
            DrawCircleV((Vector2){ x, y }, 0.6f + 1.5f * d, (Color){ 70, 64, 56, 255 });
        }
    }
    Vector2 prev = { 0 };
    for (int k = n0; k < s->n; k++) { Vector2 p = { PX(k), PY(s->close[k]) }; if (k > n0) DrawLineEx(prev, p, 2.2f, INK); prev = p; }
    DrawRectangleLinesEx(r, 1, INK);
    #undef PX
    #undef PY
}

static void grain(float ox, float oy, float pw, float ph) {
    static Texture2D t; static bool ok;
    if (!ok) {
        Image im = GenImagePerlinNoise(256, 256, 0, 0, 18.0f);
        Image w = GenImageWhiteNoise(256, 256, 0.5f);
        ImageColorBrightness(&w, -40);
        ImageDraw(&im, w, (Rectangle){ 0, 0, 256, 256 }, (Rectangle){ 0, 0, 256, 256 }, (Color){ 255, 255, 255, 60 });
        t = LoadTextureFromImage(im); UnloadImage(im); UnloadImage(w); ok = true;
        SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    }
    DrawTexturePro(t, (Rectangle){ 0, 0, pw, ph }, (Rectangle){ ox, oy, pw, ph }, (Vector2){ 0 }, 0, (Color){ 120, 100, 60, 22 });
    /* edges darken a little, and the horizontal fold */
    for (int i = 0; i < 14; i++) { Color e = (Color){ 120, 100, 70, (unsigned char)(14 - i) }; DrawRectangleLinesEx((Rectangle){ ox + i, oy + i, pw - 2 * i, ph - 2 * i }, 1, e); }
    DrawRectangleGradientV((int)ox, (int)(oy + ph * 0.5f - 10), (int)pw, 10, (Color){ 0, 0, 0, 0 }, (Color){ 90, 70, 40, 18 });
    DrawRectangleGradientV((int)ox, (int)(oy + ph * 0.5f), (int)pw, 12, (Color){ 255, 255, 255, 30 }, (Color){ 255, 255, 255, 0 });
}

/* the story's text, written like an article from the AI's reading and our numbers */
static void article(App *a, const Stock *s, char *out, int n) {
    int k = 0;
    #define W(...) do { if (k < n) k += snprintf(out + k, (size_t)(n - k), __VA_ARGS__); } while (0)
    int ord[MAX_FACTORS], nf = s->ai.nf;
    for (int i = 0; i < nf; i++) ord[i] = i;
    for (int i = 1; i < nf; i++) for (int j = i; j > 0 && abs(s->ai.f[ord[j]].impact) > abs(s->ai.f[ord[j - 1]].impact); j--) { int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t; }
    static const char *negl[] = { "", "Adding to the pressure, ", "There is more: ", "Investors also face this: " };
    static const char *posl[] = { "On the brighter side, ", "There is good news too. ", "In its favour, ", "Helping a little, " };
    int ni = 0, pi = 0;
    for (int i = 0; i < nf; i++) {
        const Factor *f = &s->ai.f[ord[i]];
        const char *lead = i == 0 ? "" : f->impact >= 0 ? posl[pi++ & 3] : negl[(++ni) & 3];
        if (i == 0) W("%s \xe2\x80\x94 ", s->mkt == MKT_IN ? "MUMBAI" : "NEW YORK");
        char t[260]; snprintf(t, sizeof t, "%s", f->text);
        if (lead[0] && lead[strlen(lead) - 1] == ' ' && lead[strlen(lead) - 2] == ',' && islower((unsigned char)t[1])) t[0] = (char)tolower((unsigned char)t[0]);
        if (t[0] && strncmp(t, "Q", 1) && strncmp(t, "CEO", 3) && strncmp(t, "US", 2) && isupper((unsigned char)t[1])) t[0] = (char)toupper((unsigned char)t[0]);
        W("%s%s%s", i ? "\n" : "", lead, t);
    }
    W("\nWhat does this mean for an ordinary investor? Putting %s into %s for a year, Vardhan's 3,000 simulations put a bad year (1 in 20) at %s, compared with %s before this news. ",
      inr((double)a->paper_amt), s->name, inr_signed(o_now.p[0] - a->paper_amt), inr_signed(o_before.p[0] - a->paper_amt));
    W("The most likely result is %s, and there is a %.0f%% chance of ending the year with less than you started.", inr_signed(o_now.p[2] - a->paper_amt), o_now.prob_loss * 100);
    /* the numbers behind it */
    W("\nThe record shows why. Over the past year %s has %s %.0f%%. A typical year sees it swing by about %.0f%%, and its worst fall in the last two years was %.0f%% from peak to bottom",
      s->name, s->ret1y >= 0 ? "gained" : "lost", fabsf(s->ret1y * 100), s->vol * 100, fabsf(s->maxdd * 100));
    if (!s->fund) W(", %s the wider market.", s->beta > 1.15f ? "moving more sharply than" : s->beta < 0.85f ? "moving more gently than" : "moving roughly in step with"); else W(".");
    /* Vardhan's view for this reader */
    char why[200] = ""; float fit = advisor_fit(a->u, &a->prof, a->paper_stock, why, sizeof why);
    bool mine = false; float wt = 0; for (int k = 0; k < a->plan->npicks; k++) if (a->plan->picks[k].stock == a->paper_stock) { mine = true; wt = a->plan->picks[k].weight; }
    if (k < n && strstr(out, why)) why[0] = 0;                 /* don't repeat a sentence already in the story */
    char score[96];
    if (fit < 10) snprintf(score, sizeof score, "it fails Vardhan's checks for a %s investor like you", risk_label(a->prof.risk));
    else snprintf(score, sizeof score, "it scores %.0f out of 100 for a %s investor like you", fit, risk_label(a->prof.risk));
    if (mine) W("\nVardhan's view: the stock is in your plan, at %.0f%% of your money, because %s. %s", wt * 100, score, why);
    else W("\nVardhan's view: %s, which is why it is not in your plan. %s", score, why);
    /* what to watch */
    const char *watch[8]; int nwch = 0;
    for (int i = 0; i < nf && nwch < 3; i++) if (s->ai.f[i].impact < 0) {
        const char *cat = s->ai.f[i].category; bool dup = false;
        for (int j = 0; j < nwch; j++) if (!strcmp(watch[j], cat)) dup = true;
        if (!dup) watch[nwch++] = cat;
    }
    if (nwch) {
        W("\nWhat to watch: further news on ");
        for (int j = 0; j < nwch; j++) { char lc[32]; snprintf(lc, sizeof lc, "%s", watch[j]); lc[0] = (char)tolower((unsigned char)lc[0]);
            W("%s%s", lc, j + 2 < nwch ? ", " : j + 1 < nwch ? " and " : ""); }
        W(". Vardhan re-reads the headlines whenever you refresh, and the numbers on this page update with them.");
    } else W("\nWhat to watch: nothing alarming in this week's headlines. Vardhan re-reads the news whenever you refresh.");
    #undef W
}

/* Draws the whole page at origin (ox, oy). Interactive only once it has landed. */
static void body(App *a, float ox, float oy, float pw, float ph, bool live) {
    Universe *u = a->u; Stock *s = &u->s[a->paper_stock];
    DrawRectangle((int)ox, (int)oy, (int)pw, (int)ph, PAPER);
    grain(ox, oy, pw, ph);
    float L = ox + 34, R = ox + pw - 34, W = R - L;
    char b[600];

    /* ---------------- nameplate with ears ---------------- */
    time_t tt = time(NULL); struct tm *tm = localtime(&tt);
    float ey = oy + 20;
    Rectangle earL = { L, ey, 178, 64 }, earR = { R - 178, ey, 178, 64 };
    DrawRectangleLinesEx(earL, 1, INK); DrawRectangleLinesEx(earR, 1, INK);
    const Stock *nx = &u->s[u->idx_in];
    text_c(F.s13, "MARKETS", earL.x + earL.width / 2, earL.y + 8, INK2);
    snprintf(b, sizeof b, "Nifty 50  %.0f", nx->price);
    text_c(F.hd17, b, earL.x + earL.width / 2, earL.y + 26, INK);
    snprintf(b, sizeof b, "%s %.2f%% today", nx->chg1d >= 0 ? "up" : "down", fabsf(nx->chg1d * 100));
    text_c(F.i15, b, earL.x + earL.width / 2, earL.y + 44, nx->chg1d >= 0 ? IGREEN : IRED);
    text_c(F.s13, "TODAY'S RISK", earR.x + earR.width / 2, earR.y + 8, INK2);
    float sent = s->ai.valid ? s->ai.sentiment : 0;
    const char *wx = sent < -0.35f ? "Stormy" : sent < -0.1f ? "Clouds gathering" : sent < 0.15f ? "Mixed skies" : "Fair weather";
    text_c(F.hd17, wx, earR.x + earR.width / 2, earR.y + 26, INK);
    snprintf(b, sizeof b, "for %s", s->sym); text_c(F.i15, b, earR.x + earR.width / 2, earR.y + 44, INK2);
    text_c(F.hd64, "The Vardhan Times", ox + pw / 2, oy + 12, INK);
    /* dateline */
    float dy = oy + 92;
    rule(L, dy, W, 1);
    char date[64]; strftime(date, sizeof date, "%A, %B %d, %Y", tm); for (char *c = date; *c; c++) *c = (char)toupper((unsigned char)*c);
    snprintf(b, sizeof b, "VOL. I  \xc2\xb7  No. %d", tm->tm_yday + 1); text(F.s13, b, L, dy + 6, INK);
    text_c(F.s13, date, ox + pw / 2, dy + 6, INK);
    text_r(F.s13, "MARKETS EDITION  \xc2\xb7  FREE", R, dy + 6, INK);
    rule(L, dy + 25, W, 3); rule(L, dy + 30, W, 1);

    /* ---------------- index strip ---------------- */
    {
        float sx = L, sy = dy + 38;
        text(F.hd17, "Inside:", sx, sy, INK); sx += text_w(F.hd17, "Inside:") + 10;
        for (int k = 0; k < norder_; k++) {
            const Stock *o = &u->s[order[k]];
            bool cur = order[k] == a->paper_stock, mine = in_plan(a, order[k]);
            bool red = o->ai.valid && o->ai.sentiment < -0.15f;
            float tw = text_w(F.s13, o->sym) + 14;
            if (sx + tw > R - 190) { text(F.s13, "...", sx, sy + 3, INK3); break; }
            Rectangle t = { sx, sy, tw, 22 };
            bool h = live && hover(t);
            if (cur) DrawRectangleRec(t, INK); else if (h) DrawRectangleRec(t, PAPER2);
            if (k > 0 && mine != in_plan(a, order[k - 1])) { DrawRectangle((int)sx - 6, (int)sy + 2, 1, 18, INK); }
            text(F.s13, o->sym, sx + 7, sy + 3, cur ? PAPER : red ? IRED : INK);
            if (h) { SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) go_story(a, order[k], 0); }
            sx += tw + (k + 1 < norder_ && mine != in_plan(a, order[k + 1]) ? 12 : 1);
        }
        text_r(F.i15, live ? "use \xe2\x86\x90 \xe2\x86\x92 to turn pages" : "", R, sy + 2, INK2);
        rule(L, sy + 28, W, 1);
    }

    float top = dy + 78, bottom = oy + ph - 190;
    float gut = 22, cw = (W - 4 * gut) / 5;
    #define CX(i) (L + (i) * (cw + gut))
    float ct = ui_time() - sw_t, fadein = ct < 0.35f ? ct / 0.35f : 1;
    Color ink = fade(INK, fadein), ink2 = fade(INK2, fadein);

    if (!s->ai.valid) {
        snprintf(b, sizeof b, "No news reading yet for %s", s->name);
        wrap(F.hd44, b, L, top + 10, W * 0.7f, ink, 48);
        wrap(F.hi18, "Turn on the AI in Settings and press 'Re-read the news' on this stock. Vardhan will fetch the latest headlines and explain what they mean for your money.", L, top + 120, W * 0.6f, ink2, 26);
    } else {
        /* kicker + headline across four columns */
        float y = top;
        snprintf(b, sizeof b, "%s  \xc2\xb7  %s", s->sector, in_plan(a, a->paper_stock) ? "In your plan" : s->ai.sentiment < -0.15f ? "Not in your plan: too risky right now" : "Not in your plan");
        for (char *c = b; *c; c++) *c = (char)toupper((unsigned char)*c);
        text(F.s13, b, L, y, in_plan(a, a->paper_stock) ? IGREEN : IRED); y += 22;
        char hl[200]; if (s->ai.headline[0]) snprintf(hl, sizeof hl, "%s", s->ai.headline); else headline(s, hl, sizeof hl);
        y += wrap(F.hd44, hl, L, y, CX(4) - gut - L, ink, 46) + 4;
        y += wrap(F.hi18, s->ai.summary, L, y, CX(4) - gut - L, ink2, 24) + 6;
        snprintf(b, sizeof b, "By the VARDHAN AI DESK  \xc2\xb7  %s  \xc2\xb7  %s", s->ai.by, s->ai.when);
        text(F.s13, b, L, y, ink2); y += 22;
        rule(L, y, CX(4) - gut - L, 1); y += 12;

        /* photo (halftone chart) in column 4, article flows down columns 1-3 */
        Rectangle ph_r = { CX(3), y, cw, 128 };
        halftone_chart(s, ph_r);
        float ch6 = s->n > 126 ? s->close[s->n - 1] / s->close[s->n - 127] - 1 : 0;
        snprintf(b, sizeof b, "%s shares over six months: %+.0f%%.", s->name, ch6 * 100);
        float cy = y + 134 + wrap(F.i15, b, CX(3), y + 134, cw, ink2, 18) + 10;
        /* market data, set small like a newspaper's stock table */
        {
            text(F.s13, "MARKET DATA", CX(3), cy, ink); cy += 18;
            rule(CX(3), cy, cw, 1); cy += 5;
            float ch1 = s->n > 21 ? s->close[s->n - 1] / s->close[s->n - 22] - 1 : 0;
            struct { const char *k; char v[32]; Color c; } md[7];
            int nm = 0;
            md[nm].k = "Last price"; snprintf(md[nm].v, 32, "%s%.2f", s->mkt == MKT_US ? "$" : "\xe2\x82\xb9", s->price); md[nm++].c = ink;
            md[nm].k = "Today"; snprintf(md[nm].v, 32, "%+.2f%%", s->chg1d * 100); md[nm].c = s->chg1d >= 0 ? IGREEN : IRED; nm++;
            md[nm].k = "1 month"; snprintf(md[nm].v, 32, "%+.1f%%", ch1 * 100); md[nm].c = ch1 >= 0 ? IGREEN : IRED; nm++;
            md[nm].k = "1 year"; snprintf(md[nm].v, 32, "%+.1f%%", s->ret1y * 100); md[nm].c = s->ret1y >= 0 ? IGREEN : IRED; nm++;
            md[nm].k = "Yearly swing"; snprintf(md[nm].v, 32, "%.0f%%", s->vol * 100); md[nm++].c = ink;
            md[nm].k = "Worst fall (2y)"; snprintf(md[nm].v, 32, "%.0f%%", s->maxdd * 100); md[nm++].c = IRED;
            md[nm].k = "Beta vs index"; snprintf(md[nm].v, 32, "%.2f", s->beta); md[nm++].c = ink;
            for (int k = 0; k < nm && cy + 19 < bottom; k++) {
                text(F.p14, md[k].k, CX(3), cy, ink2); text_r(F.p14, md[k].v, CX(3) + cw, cy, md[k].c);
                cy += 18;
                for (float xx = CX(3); xx < CX(3) + cw; xx += 3) DrawPixel((int)xx, (int)cy - 1, INK3);
            }
            cy += 10;
        }
        /* pull quote, if there's room */
        if (cy + 70 < bottom) {
            rule(CX(3), cy, cw, 2); cy += 8;
            snprintf(b, sizeof b, "\xe2\x80\x9c" "A bad year could cost you %s.\xe2\x80\x9d", inr((double)-(o_now.p[0] - a->paper_amt)));
            float qh = wrap(F.hd22, b, CX(3), -2000, cw, BLANK, 25);
            if (cy + qh + 10 < bottom) { cy += wrap(F.hd22, b, CX(3), cy, cw, IRED, 25) + 4; rule(CX(3), cy, cw, 2); }
        }
        static char art[4000]; article(a, s, art, sizeof art);
        /* balance the three columns: measure the story in one tall column, then split it evenly */
        float lh = 19.5f;
        Col one = { CX(0), 0, cw, 1e9f }; int dummy;
        float need = flow(art, &one, 1, F.p14, lh, BLANK, true, &dummy);
        float lines = need / lh, avail = bottom - y;
        if (lines * lh < 3 * avail - 3 * lh) { lh = fminf(23.0f, 3 * avail / (lines + 4)); need = flow(art, &one, 1, F.p14, lh, BLANK, true, &dummy); }
        float colh = ceilf(need / lh / 3.0f + 1) * lh;
        float cb = fminf(bottom, y + colh);
        Col cols[3] = { { CX(0), y, cw, cb }, { CX(1), y, cw, cb }, { CX(2), y, cw, bottom } };
        int last = 0;
        float yend = flow(art, cols, 3, F.p14, lh, ink, true, &last);
        /* a "words to know" explainer fills any space left at the bottom of the last column */
        float fx = CX(last), fy = yend + 14, fw = cw;
        if (bottom - fy > 96) {
            static const char *jw[][2] = {
                { "Bad year (1 in 20)", "How much you could lose in a year that only comes around once every twenty years." },
                { "Yearly swing", "How far the price usually moves up or down in a year. Bigger means bumpier." },
                { "Beta", "How strongly the stock follows the market. Above 1 means bigger moves than the index." },
                { "XIRR", "Your yearly return, taking into account when each rupee went in." } };
            DrawRectangleLinesEx((Rectangle){ fx, fy, fw, bottom - fy - 4 }, 1, INK);
            float jy = fy + 10;
            text_c(F.hd17, "Words to know", fx + fw / 2, jy, ink); jy += 26;
            for (int k = 0; k < 4; k++) {
                float h = 20 + wrap(F.p14, jw[k][1], 0, -3000, fw - 20, BLANK, 17);
                if (jy + h > bottom - 10) break;
                text(F.s13, jw[k][0], fx + 10, jy, ink); jy += 18;
                jy += wrap(F.p14, jw[k][1], fx + 10, jy, fw - 20, ink2, 17) + 8;
            }
        }
        for (int i = 1; i < 4; i++) DrawRectangle((int)(CX(i) - gut / 2), (int)y, 1, (int)(bottom - y), INK3);
    }

    /* ---------------- right column: by the numbers + wires ---------------- */
    {
        float x = CX(4), y = top;
        DrawRectangle((int)(x - gut / 2), (int)top, 1, (int)(bottom - top), INK);
        text_c(F.hd17, "By the numbers", x + cw / 2, y, ink); y += 24;
        rule(x, y, cw, 2); y += 10;
        snprintf(b, sizeof b, "If you put %s in %s for a year", inr((double)a->paper_amt), s->sym);
        y += wrap(F.i15, b, x, y, cw, ink2, 18) + 8;
        text(F.s13, "A BAD YEAR", x, y, ink2); y += 18;
        text(F.hd22, inr_signed(o_now.p[0] - a->paper_amt), x, y, IRED); y += 26;
        snprintf(b, sizeof b, "was %s before this news", inr_signed(o_before.p[0] - a->paper_amt));
        text(F.i15, b, x, y, ink2); y += 26;
        text(F.s13, "MOST LIKELY", x, y, ink2); y += 18;
        text(F.hd22, inr_signed(o_now.p[2] - a->paper_amt), x, y, o_now.p[2] >= a->paper_amt ? IGREEN : IRED); y += 32;
        text(F.s13, "CHANCE OF A LOSS", x, y, ink2); y += 18;
        snprintf(b, sizeof b, "%.0f%%", o_now.prob_loss * 100); text(F.hd22, b, x, y, ink); y += 30;
        if (s->ai.valid) {
            snprintf(b, sizeof b, "The news makes swings %.1f\xc3\x97 normal, with a %.0f%% chance of a sudden %.0f%% drop.", s->ai.vol_mult, s->ai.event_prob * 100, fabsf(s->ai.event_size) * 100);
            y += wrap(F.i15, b, x, y, cw, ink2, 18) + 10;
        }
        rule(x, y, cw, 2); y += 10;
        text_c(F.hd17, "From the wires", x + cw / 2, y, ink); y += 26;
        for (NewsItem *it = s->news; it; it = it->next) {
            float h = wrap(F.p14, it->title, 0, -1000, cw, BLANK, 18);
            if (y + h + 22 > bottom) break;
            y += wrap(F.p14, it->title, x, y, cw, ink, 18);
            snprintf(b, sizeof b, "%s, %s", it->source, it->when); text(F.i15, b, x, y + 1, ink2); y += 26;
        }
    }

    /* ---------------- red flags: briefs ---------------- */
    float sy = bottom + 8;
    rule(L, sy, W, 3); rule(L, sy + 5, W, 1);
    bool flags = nothers && u->s[others[0]].ai.sentiment < -0.1f;
    text(F.hd22, flags ? "Red Flags" : "In Brief", L, sy + 12, flags ? IRED : INK);
    text(F.i15, flags ? "the stocks with the worst news this week, and why Vardhan steers you away" : "more stories in this edition", L + text_w(F.hd22, flags ? "Red Flags" : "In Brief") + 12, sy + 17, INK2);
    float bw = (W - 3 * gut) / 4;
    for (int k = 0; k < nothers && k < 4; k++) {
        const Stock *o = &u->s[others[k]];
        Rectangle c = { L + k * (bw + gut), sy + 44, bw, 104 };
        bool h = live && hover(c);
        if (k) DrawRectangle((int)(c.x - gut / 2), (int)c.y, 1, (int)c.height, INK3);
        char hl[200]; if (o->ai.headline[0]) snprintf(hl, sizeof hl, "%s", o->ai.headline); else headline(o, hl, sizeof hl);
        float hy = c.y + wrap(F.hd17, hl, c.x, c.y, c.width, h ? IRED : INK, 20) + 4;
        if (o->ai.nf) { int wf = 0; for (int f = 1; f < o->ai.nf; f++) if (o->ai.f[f].impact < o->ai.f[wf].impact) wf = f;
            char t[300]; snprintf(t, sizeof t, "%s", o->ai.f[wf].text);
            float avail = c.y + c.height - 22 - hy; if (avail > 18) { ui_scissor(c.x, hy, c.width, avail); wrap(F.p14, t, c.x, hy, c.width, INK2, 17); EndScissorMode(); } }
        snprintf(b, sizeof b, "%s  \xc2\xb7  page %d \xe2\x86\x92", o->sym, k + 2);
        text(F.s13, b, c.x, c.y + c.height - 16, h ? IRED : INK);
        if (h) { SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) go_story(a, others[k], 0); }
    }
    text_c(F.i15, "Vardhan reads the news so you don't invest blind. Education only, not investment advice.", ox + pw / 2, oy + ph - 22, INK2);
    if (live) {
        Rectangle cl = { R - 60, oy + ph - 26, 60, 20 };
        bool h = hover(cl);
        text_r(F.s13, "CLOSE \xc3\x97", R, oy + ph - 22, h ? IRED : INK2);
        if (h) { SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) a->paper_on = false; }
    }
    #undef CX
}

void draw_paper(App *a) {
    if (!a->paper_on) return;
    Rectangle P = paper_rect();
    float t = ui_time() - a->paper_t, k = t / 0.75f, e = ease3(k);
    int W = ui_w(), H = ui_h();
    DrawRectangle(0, 0, W, H, fade((Color){ 3, 5, 10, 255 }, 0.78f * fminf(1, t / 0.3f)));
    ui_modal(true, (Rectangle){ 0, 0, (float)W, (float)H });
    if (k < 1) {
        /* the classic spinning front page, settling onto the desk */
        float sc0 = ui_scale(); int tw = (int)(P.width * sc0), th = (int)(P.height * sc0);
        if (!rt_ok || rt.texture.width != tw || rt.texture.height != th) {
            if (rt_ok) UnloadRenderTexture(rt);
            rt = LoadRenderTexture(tw, th); rt_ok = true;
            SetTextureFilter(rt.texture, TEXTURE_FILTER_BILINEAR);
        }
        ui_cam_end();                                     /* render the page at full resolution */
        BeginTextureMode(rt); ClearBackground(PAPER); ui_cam_begin(); body(a, 0, 0, P.width, P.height, false); ui_cam_end(); EndTextureMode();
        ui_cam_begin();
        float sc = 0.06f + 0.94f * e, rot = (1 - e) * 540;
        Rectangle src = { 0, 0, (float)tw, -(float)th };
        Rectangle dst = { P.x + P.width / 2, P.y + P.height / 2, P.width * sc, P.height * sc };
        DrawRectanglePro((Rectangle){ dst.x + 10 * sc, dst.y + 14 * sc, dst.width, dst.height }, (Vector2){ dst.width / 2, dst.height / 2 }, rot, fade(BLACK, 0.35f));
        DrawTexturePro(rt.texture, src, dst, (Vector2){ dst.width / 2, dst.height / 2 }, rot, WHITE);
    } else {
        for (int i = 1; i <= 3; i++) DrawRectangle((int)P.x + 4 * i, (int)P.y + 6 * i, (int)P.width, (int)P.height, fade(BLACK, 0.12f));
        body(a, P.x, P.y, P.width, P.height, true);
        if (IsKeyPressed(KEY_ESCAPE)) a->paper_on = false;
        int cur = 0; for (int k = 0; k < norder_; k++) if (order[k] == a->paper_stock) cur = k;
        if (IsKeyPressed(KEY_RIGHT) && norder_) go_story(a, order[(cur + 1) % norder_], 0);
        if (IsKeyPressed(KEY_LEFT) && norder_) go_story(a, order[(cur - 1 + norder_) % norder_], 0);
        /* page-turn arrows beside the paper */
        for (int d = 0; d < 2; d++) {
            Rectangle ar = { d ? P.x + P.width + 8 : P.x - 48, P.y + P.height / 2 - 30, 40, 60 };
            bool h = CheckCollisionPointRec(GetMousePosition(), ar);
            rrect(ar, 10, h ? fade(PAPER, 0.25f) : fade(PAPER, 0.1f));
            text_c(F.s24, d ? "\xe2\x86\x92" : "\xe2\x86\x90", ar.x + 20, ar.y + 16, PAPER);
            if (h) { SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && norder_) go_story(a, order[d ? (cur + 1) % norder_ : (cur - 1 + norder_) % norder_], 0); }
        }
        Vector2 mp = GetMousePosition();
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mp, (Rectangle){ P.x - 50, P.y, P.width + 100, P.height }) && t > 0.9f) a->paper_on = false;
    }
    ui_modal(false, P);
}

/* For the PDF report: one finished front page for `stock`, drawn at (ox, oy). */
void paper_print(App *a, int stock, float ox, float oy, float pw, float ph) {
    int old = a->paper_stock; long long olda = a->paper_amt;
    a->paper_stock = stock; a->paper_amt = amount_for(a, stock);
    build_order(a); compute(a); sw_t = -10;
    body(a, ox, oy, pw, ph, false);
    a->paper_stock = old; a->paper_amt = olda;
}
