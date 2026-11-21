/* scr_proof.c - "Proof it works": back-test + calibration. */
#include "app.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static Backtest bt;
static float ease(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return 1 - powf(1 - t, 3); }
static const char *ymd(int d) {
    static char b[4][24]; static int k; char *o = b[k++ & 3];
    static const char *mo[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    int m = d / 100 % 100; snprintf(o, 24, "%d %s %d", d % 100, mo[(m + 11) % 12], d / 10000);
    return o;
}

static void chart(App *a, Rectangle r, float an) {
    Plan *p = &bt.plan;
    Rectangle pl = { r.x + 78, r.y + 64, r.width - 78 - 24, r.height - 120 };
    float lo = 1e30f, hi = -1e30f;
    for (int w = 0; w < FAN_STEPS; w++) {
        lo = fminf(lo, fminf(p->fan[0][w], fminf(bt.actual[w], bt.index[w])));
        hi = fmaxf(hi, fmaxf(p->fan[4][w], fmaxf(bt.actual[w], bt.index[w])));
    }
    float pad = (hi - lo) * 0.08f; lo -= pad; hi += pad;
    #define X(w) (pl.x + (w) / (float)(FAN_STEPS - 1) * pl.width)
    #define Y(v) (pl.y + pl.height - ((v) - lo) / (hi - lo) * pl.height)
    for (int k = 0; k <= 4; k++) {
        float v = lo + (hi - lo) * k / 4, yy = Y(v);
        DrawLine((int)pl.x, (int)yy, (int)(pl.x + pl.width), (int)yy, fade(C_BORDER, 0.6f));
        text_r(F.n12, inr_short(v), pl.x - 10, yy - 7, C_FAINT);
    }
    float A = (float)bt.amount, ya = Y(A);
    for (float xx = pl.x; xx < pl.x + pl.width; xx += 10) DrawLine((int)xx, (int)ya, (int)(xx + 5), (int)ya, C_FAINT);
    /* predicted bands (made a year ago) */
    for (int band = 0; band < 2; band++) {
        int lo_i = band ? 1 : 0, hi_i = band ? 3 : 4;
        Color c = fade(C_VIOLET, band ? 0.30f : 0.14f);
        for (int w = 0; w + 1 < FAN_STEPS; w++) {
            Vector2 a1 = { X(w), Y(p->fan[hi_i][w]) }, b1 = { X(w + 1), Y(p->fan[hi_i][w + 1]) };
            Vector2 a2 = { X(w), Y(p->fan[lo_i][w]) }, b2 = { X(w + 1), Y(p->fan[lo_i][w + 1]) };
            DrawTriangle(a1, a2, b2, c); DrawTriangle(a1, b2, b1, c);
            DrawTriangle(a1, b2, a2, c); DrawTriangle(a1, b1, b2, c);
        }
    }
    for (int w = 0; w + 1 < FAN_STEPS; w++) DrawLineEx((Vector2){ X(w), Y(p->fan[2][w]) }, (Vector2){ X(w + 1), Y(p->fan[2][w + 1]) }, 1.5f, fade(C_VIOLET, 0.8f));
    /* what really happened, drawn over time */
    int upto = (int)(an * (FAN_STEPS - 1));
    for (int w = 0; w < upto; w++) {
        DrawLineEx((Vector2){ X(w), Y(bt.index[w]) }, (Vector2){ X(w + 1), Y(bt.index[w + 1]) }, 1.5f, fade(C_DIM, 0.7f));
        DrawLineEx((Vector2){ X(w), Y(bt.actual[w]) }, (Vector2){ X(w + 1), Y(bt.actual[w + 1]) }, 3, C_CYAN);
    }
    glow(X(upto), Y(bt.actual[upto]), 26, C_CYAN, 0.5f);
    DrawCircle((int)X(upto), (int)Y(bt.actual[upto]), 5, WHITE);
    text(F.r12, ymd(bt.from), pl.x, pl.y + pl.height + 10, C_FAINT);
    text_r(F.r12, ymd(bt.to), pl.x + pl.width, pl.y + pl.height + 10, C_FAINT);
    text_c(F.r12, "6 months", pl.x + pl.width / 2, pl.y + pl.height + 10, C_FAINT);
    /* legend */
    float lx = r.x + 24, ly = r.y + r.height - 30;
    rrect((Rectangle){ lx, ly + 2, 18, 10 }, 3, fade(C_VIOLET, 0.4f)); text(F.r13, "Range Vardhan predicted a year ago", lx + 26, ly - 1, C_DIM); lx += 270;
    DrawLineEx((Vector2){ lx, ly + 7 }, (Vector2){ lx + 18, ly + 7 }, 3, C_CYAN); text(F.r13, "What really happened", lx + 26, ly - 1, C_DIM); lx += 186;
    DrawLineEx((Vector2){ lx, ly + 7 }, (Vector2){ lx + 18, ly + 7 }, 1.5f, C_DIM); text(F.r13, "Nifty 50, for comparison", lx + 26, ly - 1, C_DIM);
    /* hover */
    if (hover(pl)) {
        int w = (int)roundf((GetMousePosition().x - pl.x) / pl.width * (FAN_STEPS - 1));
        if (w >= 0 && w < FAN_STEPS && w <= upto) {
            DrawLine((int)X(w), (int)pl.y, (int)X(w), (int)(pl.y + pl.height), fade(C_TEXT, 0.25f));
            Rectangle tip = { X(w) + 12 > pl.x + pl.width - 230 ? X(w) - 232 : X(w) + 12, pl.y + 8, 220, 86 };
            rrect(tip, 10, C_CARD2); rline(tip, 10, C_BORDER);
            char l[96]; snprintf(l, sizeof l, "Week %d", w); text(F.s13, l, tip.x + 12, tip.y + 10, C_TEXT);
            snprintf(l, sizeof l, "Predicted %s - %s", inr_short(p->fan[0][w]), inr_short(p->fan[4][w])); text(F.r12, l, tip.x + 12, tip.y + 32, C_DIM);
            snprintf(l, sizeof l, "Actual    %s", inr(bt.actual[w])); text(F.r12, l, tip.x + 12, tip.y + 50, C_CYAN);
            snprintf(l, sizeof l, "Nifty 50  %s", inr(bt.index[w])); text(F.r12, l, tip.x + 12, tip.y + 66, C_DIM);
        }
    }
    #undef X
    #undef Y
    (void)a;
}

void draw_proof(App *a, Rectangle r) {
    header("Proof it works", "We hid the last 12 months of prices, built your plan as if it were a year ago, then checked what really happened.", r);
    if (!a->bt_ready) {
        bt_run(a->u, &a->prof, &bt);
        a->bt_ready = true; a->scr_t = ui_time();
    }
    if (!bt.ok) { text(F.r16, bt.msg[0] ? bt.msg : "Back-test unavailable", r.x, r.y + 100, C_WARN); return; }
    float t = ui_time() - a->scr_t, an = ease(t / 2.2f), G = 18, y = r.y + 88;

    int live = 0; for (int i = 0; i < a->u->n; i++) live += a->u->s[i].live && !a->u->s[i].hidden;
    if (!live) {
        Rectangle b = { r.x + r.width - 430, r.y + 6, 430, 40 };
        rrect(b, 10, fade(C_WARN, 0.12f)); rline(b, 10, fade(C_WARN, 0.5f));
        text(F.m13, "Sample prices: load real prices for a real test.", b.x + 14, b.y + 12, C_WARN);
    }

    float cw = r.width * 0.64f, h1 = (r.height - 88) * 0.60f;
    Rectangle C = { r.x, y, cw, h1 };
    card(C);
    text(F.s18, "Your plan, one year ago vs. what really happened", C.x + 24, C.y + 20, C_TEXT);
    char l[200];
    snprintf(l, sizeof l, "%s invested on %s", inr((double)bt.amount), ymd(bt.from));
    text_r(F.r13, l, C.x + C.width - 24, C.y + 24, C_DIM);
    chart(a, C, an);

    /* verdict */
    Rectangle V = { r.x + cw + G, y, r.width - cw - G, h1 };
    card(V);
    float vx = V.x + 24, vy = V.y + 22, vw = V.width - 48;
    Color vc = bt.inside ? C_GAIN : C_WARN;
    float fin = bt.actual[FAN_STEPS - 1];
    if (an > 0.95f) {
        float pop = ease((t - 2.1f) / 0.4f);
        glow(vx + 22, vy + 22, 40 * pop, vc, 0.4f);
        DrawCircle((int)vx + 22, (int)vy + 22, 20 * pop, vc);
        if (bt.inside) { DrawLineEx((Vector2){ vx + 13, vy + 22 }, (Vector2){ vx + 20, vy + 29 }, 3.5f, C_BG); DrawLineEx((Vector2){ vx + 20, vy + 29 }, (Vector2){ vx + 32, vy + 15 }, 3.5f, C_BG); }
        else text_c(F.s20, "!", vx + 22, vy + 11, C_BG);
        text(F.s20, bt.inside ? "Landed inside the range" : "Landed outside the range", vx + 56, vy + 2, C_TEXT);
        text(F.r13, bt.in_middle ? "and inside the likely middle band" : bt.inside ? "a rare year, but still inside" : "this is the 1-in-10 case we warn about", vx + 56, vy + 28, C_DIM);
    } else { spinner(vx + 22, vy + 22, 14, C_VIOLET); text(F.s18, "Replaying the year...", vx + 56, vy + 10, C_DIM); }
    vy += 70;
    struct { const char *k; char v[64]; Color c; } rows[4];
    rows[0].k = "Predicted range"; snprintf(rows[0].v, 64, "%s - %s", inr_short(bt.plan.fan[0][FAN_STEPS - 1]), inr_short(bt.plan.fan[4][FAN_STEPS - 1])); rows[0].c = C_VIOLET;
    rows[1].k = "Actually ended at"; snprintf(rows[1].v, 64, "%s", inr(fin)); rows[1].c = fin >= bt.amount ? C_GAIN : C_LOSS;
    rows[2].k = "Weeks inside range"; snprintf(rows[2].v, 64, "%d of %d", bt.weeks_inside, FAN_STEPS); rows[2].c = C_TEXT;
    rows[3].k = "Deepest dip on the way"; snprintf(rows[3].v, 64, "%s", inr_signed(bt.worst_dip)); rows[3].c = bt.worst_dip < 0 ? C_LOSS : C_GAIN;
    for (int i = 0; i < 4; i++) {
        text(F.r14, rows[i].k, vx, vy, C_DIM);
        text_r(F.n18, rows[i].v, vx + vw, vy - 2, rows[i].c);
        vy += 34;
    }
    vy += 8;
    if (bt.hot >= 0) {
        float fh = 150; if (vy + fh > V.y + V.height - 20) fh = V.y + V.height - 20 - vy;
        Rectangle f = { vx, vy, vw, fh };
        rrect(f, 12, C_BG2); rline(f, 12, fade(C_PINK, 0.35f));
        text(F.s14, "Meanwhile, the friend's tip", f.x + 16, f.y + 14, C_PINK);
        Stock *hs = &a->u->s[bt.hot];
        snprintf(l, sizeof l, "%s had jumped %.0f%% the year before, the stock everyone was talking about.", hs->name, bt.hot_past_ret * 100);
        text_wrap(F.r13, l, f.x + 16, f.y + 38, f.width - 32, C_DIM, 18);
        snprintf(l, sizeof l, "All-in: ended %s, dipped %s", inr_short(bt.hot_final), inr_signed(bt.hot_dip));
        text(F.m13, l, f.x + 16, f.y + f.height - 52, C_TEXT);
        snprintf(l, sizeof l, "Your plan: ended %s, dipped %s", inr_short(fin), inr_signed(bt.worst_dip));
        text(F.m13, l, f.x + 16, f.y + f.height - 30, C_CYAN);
        float fy = f.y + f.height + 16;
        if (fy + 40 < V.y + V.height) {
            const char *msg = bt.hot_dip < bt.worst_dip ? "Your plan fell less on the way down. That is the point: smaller dips mean you don't panic-sell."
                                                        : "This year the hot tip did well. Vardhan can't promise the best return, only an honest range.";
            text_wrap(F.r13, msg, vx, fy, vw, C_DIM, 18);
        }
    }

    /* calibration */
    y += h1 + G;
    Rectangle K = { r.x, y, r.width, r.y + r.height - y };
    card(K);
    text(F.s18, "Are the \"bad month\" warnings honest?", K.x + 24, K.y + 20, C_TEXT);
    float rate = bt.tests ? (float)bt.breaks / bt.tests : 0;
    snprintf(l, sizeof l, "We say a month this bad happens 1 time in 20 (5%%). Tested %d times across %d stocks: it happened %.1f%% of the time.", bt.tests, bt.ncal, rate * 100);
    text(F.r13, l, K.x + 24, K.y + 48, C_DIM);
    Rectangle bigr = { K.x + K.width - 210, K.y + 16, 186, 60 };
    rrect(bigr, 12, C_BG2);
    snprintf(l, sizeof l, "%.1f%%", rate * 100);
    text(F.n32, l, bigr.x + 16, bigr.y + 10, fabsf(rate - 0.05f) < 0.03f ? C_GAIN : C_WARN);
    text(F.r12, "vs 5% promised", bigr.x + 112, bigr.y + 24, C_FAINT);

    Rectangle pl = { K.x + 60, K.y + 92, K.width - 84, K.height - 160 };
    float top = 0.20f;
    for (int k = 0; k <= 4; k++) { float v = top * k / 4, yy = pl.y + pl.height - v / top * pl.height; DrawLine((int)pl.x, (int)yy, (int)(pl.x + pl.width), (int)yy, fade(C_BORDER, 0.5f)); snprintf(l, sizeof l, "%.0f%%", v * 100); text_r(F.n12, l, pl.x - 8, yy - 7, C_FAINT); }
    int n = bt.ncal; float bw = pl.width / (n ? n : 1);
    for (int i = 0; i < n; i++) {
        float v = (float)bt.cal[i].breaks / bt.cal[i].tests; if (v > top) v = top;
        float h = v / top * pl.height * an;
        Color c = fabsf(v - 0.05f) <= 0.04f ? C_GAIN : C_WARN;
        Rectangle b = { pl.x + i * bw + bw * 0.18f, pl.y + pl.height - h, bw * 0.64f, h };
        rrect(b, 3, hover((Rectangle){ pl.x + i * bw, pl.y, bw, pl.height }) ? c : fade(c, 0.75f));
        const char *sym = a->u->s[bt.cal[i].stock].sym;
        bool crowded = text_w(F.r12, "HINDUNILVR") > bw - 6;
        text_c(F.r12, sym, pl.x + i * bw + bw / 2, pl.y + pl.height + 8 + (crowded && (i & 1) ? 15 : 0), C_FAINT);
        if (hover((Rectangle){ pl.x + i * bw, pl.y, bw, pl.height })) {
            snprintf(l, sizeof l, "%s: worse than predicted %d of %d months", a->u->s[bt.cal[i].stock].name, bt.cal[i].breaks, bt.cal[i].tests);
            float tw = text_w(F.r13, l) + 24, tx = fminf(pl.x + i * bw, pl.x + pl.width - tw);
            rrect((Rectangle){ tx, pl.y - 6, tw, 30 }, 8, C_CARD2); text(F.r13, l, tx + 12, pl.y + 1, C_TEXT);
        }
    }
    float y5 = pl.y + pl.height - 0.05f / top * pl.height;
    for (float xx = pl.x; xx < pl.x + pl.width; xx += 12) DrawLineEx((Vector2){ xx, y5 }, (Vector2){ xx + 6, y5 }, 2, C_VIOLET);
    rrect((Rectangle){ pl.x + 4, y5 - 22, 96, 18 }, 4, C_CARD);
    text(F.s13, "5% promised", pl.x + 8, y5 - 21, C_VIOLET);
    if (rate > 0.08f) text_r(F.r12, "Real crashes are more common than one year of history suggests, so Vardhan also adds news risk and Covid/2008 crash tests.", K.x + K.width - 24, K.y + K.height - 22, C_FAINT);
    else text_r(F.r12, "Close to the promise: the warnings are neither too scary nor too soft.", K.x + K.width - 24, K.y + K.height - 22, C_FAINT);
}
