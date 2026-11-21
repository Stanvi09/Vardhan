/* scr_plan.c - "My Plan": allocation, 12-month outlook, downside, goals, AI briefing. */
#include "app.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

static float ease(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return 1 - powf(1 - t, 3); }

static void fan_chart(App *a, Rectangle r, float anim) {
    Plan *p = a->plan;
    long long base = a->prof.lump > 0 ? a->prof.lump : a->prof.sip * 12;
    Rectangle pl = { r.x + 74, r.y + 70, r.width - 74 - 170, r.height - 110 };
    float lo = 1e30f, hi = -1e30f;
    for (int t = 0; t < FAN_STEPS; t++) { lo = fminf(lo, p->fan[0][t]); hi = fmaxf(hi, p->fan[4][t]); }
    lo = fminf(lo, (float)base); float pad = (hi - lo) * 0.08f; lo -= pad; hi += pad;
    #define X(t) (pl.x + pl.width * (t) / (FAN_STEPS - 1))
    #define Y(v) (pl.y + pl.height - ((v) - lo) / (hi - lo) * pl.height)
    for (int g = 0; g <= 4; g++) {
        float v = lo + (hi - lo) * g / 4, y = Y(v);
        DrawLineEx((Vector2){ pl.x, y }, (Vector2){ pl.x + pl.width, y }, 1, fade(C_BORDER, 0.7f));
        text_r(F.n12, inr_short(v), pl.x - 10, y - 7, C_FAINT);
    }
    const char *ml[] = { "Now", "3 months", "6 months", "9 months", "1 year" };
    for (int m = 0; m <= 4; m++) text_c(F.r12, ml[m], pl.x + pl.width * m / 4, pl.y + pl.height + 10, C_FAINT);
    int upto = (int)(anim * (FAN_STEPS - 1));
    for (int band = 0; band < 2; band++) {
        int lo_i = band ? 1 : 0, hi_i = band ? 3 : 4;
        Color c = fade(C_VIOLET, band ? 0.30f : 0.13f);
        for (int t = 0; t < upto; t++) {
            Vector2 a1 = { X(t), Y(p->fan[hi_i][t]) }, b1 = { X(t + 1), Y(p->fan[hi_i][t + 1]) };
            Vector2 a2 = { X(t), Y(p->fan[lo_i][t]) }, b2 = { X(t + 1), Y(p->fan[lo_i][t + 1]) };
            DrawTriangle(a1, a2, b1, c); DrawTriangle(b1, a2, b2, c);
        }
    }
    float by = Y((float)base);
    for (float x = pl.x; x < pl.x + pl.width; x += 10) DrawLineEx((Vector2){ x, by }, (Vector2){ x + 5, by }, 1, C_DIM);
    text(F.r12, "what you put in", pl.x + 6, by - 17, C_DIM);
    for (int t = 0; t < upto; t++) DrawLineEx((Vector2){ X(t), Y(p->fan[2][t]) }, (Vector2){ X(t + 1), Y(p->fan[2][t + 1]) }, 3, C_CYAN);
    if (anim >= 1) {
        struct { int k; const char *lab; Color c; } L[3] = { { 4, "Good year (1 in 20)", C_GAIN }, { 2, "Most likely", C_CYAN }, { 0, "Bad year (1 in 20)", C_LOSS } };
        float lastY = -100;
        for (int i = 0; i < 3; i++) {
            float v = p->fan[L[i].k][FAN_STEPS - 1], y = Y(v) - 20;
            if (y < lastY + 46) y = lastY + 46;
            lastY = y;
            DrawCircle((int)X(FAN_STEPS - 1), (int)Y(v), 5, L[i].c);
            text(F.r12, L[i].lab, pl.x + pl.width + 18, y, C_DIM);
            text(F.n18, inr_signed(v - base), pl.x + pl.width + 18, y + 16, L[i].c);
        }
    }
    /* hover read-out */
    Vector2 m = GetMousePosition();
    if (hover(pl)) {
        int t = (int)roundf((m.x - pl.x) / pl.width * (FAN_STEPS - 1));
        if (t >= 0 && t < FAN_STEPS) {
            DrawLineEx((Vector2){ X(t), pl.y }, (Vector2){ X(t), pl.y + pl.height }, 1, fade(C_TEXT, 0.4f));
            Rectangle tip = { X(t) + 12 > pl.x + pl.width - 190 ? X(t) - 200 : X(t) + 12, pl.y + 10, 188, 84 };
            rrect(tip, 10, C_CARD2); rline(tip, 10, C_BORDER);
            char l[64]; snprintf(l, sizeof l, "After %d week%s", t, t == 1 ? "" : "s");
            text(F.s13, l, tip.x + 12, tip.y + 10, C_TEXT);
            snprintf(l, sizeof l, "Good   %s", inr(p->fan[4][t])); text(F.n12, l, tip.x + 12, tip.y + 30, C_GAIN);
            snprintf(l, sizeof l, "Likely %s", inr(p->fan[2][t])); text(F.n12, l, tip.x + 12, tip.y + 46, C_CYAN);
            snprintf(l, sizeof l, "Bad    %s", inr(p->fan[0][t])); text(F.n12, l, tip.x + 12, tip.y + 62, C_LOSS);
        }
    }
    #undef X
    #undef Y
}

/* ------------------------------------------------------------------
 * What-if drawer: drag the monthly amount, the lump sum or a goal's
 * deadline and the goal odds re-simulate live (2000 Monte Carlo paths
 * per goal, same plan, same news adjustments).
 * ------------------------------------------------------------------ */
static void draw_whatif(App *a, Rectangle D) {
    Profile *pr = &a->prof; Plan *p = a->plan;
    float t = ui_time() - a->wi_t, sl = 1 - powf(1 - fminf(t / 0.25f, 1), 3);
    DrawRectangle(0, 0, ui_w(), ui_h(), fade(C_BG, 0.55f * sl));
    D.x += (1 - sl) * D.width;
    rrect(D, 0, C_CARD); DrawLine((int)D.x, (int)D.y, (int)D.x, (int)(D.y + D.height), C_BORDER);
    ui_modal(true, D);
    float x = D.x + 28, w = D.width - 56, y = D.y + 52;
    text(F.s24, "What if...?", x, y, C_TEXT);
    if (button((Rectangle){ D.x + D.width - 64, y - 2, 36, 32 }, "x", B_GHOST)) a->wi_open = false;
    y += 36;
    text_wrap(F.r13, "Move the sliders. Your goal odds are re-simulated instantly with the same plan and the same news.", x, y, w, C_DIM, 19);
    y += 56;

    bool ch = false;
    int sip_hi = a->prof.sip / 1000 * 3; if (sip_hi < 50) sip_hi = 50;
    int lump_hi = (int)(a->prof.lump / 1000 * 3); if (lump_hi < 500) lump_hi = 500;
    char l[96];
    text(F.m14, "Invest every month", x, y, C_DIM);
    snprintf(l, sizeof l, "%s", inr(a->wi_sip_k * 1000.0)); text_r(F.n18, l, x + w, y - 3, C_TEXT);
    ch |= slider_i((Rectangle){ x, y + 24, w, 28 }, &a->wi_sip_k, 0, sip_hi); y += 70;
    text(F.m14, "Invest now (lump sum)", x, y, C_DIM);
    snprintf(l, sizeof l, "%s", inr(a->wi_lump_k * 1000.0)); text_r(F.n18, l, x + w, y - 3, C_TEXT);
    ch |= slider_i((Rectangle){ x, y + 24, w, 28 }, &a->wi_lump_k, 0, lump_hi); y += 70;
    for (int g = 0; g < pr->ngoals && g < 4; g++) {
        snprintf(l, sizeof l, "%s deadline", pr->goals[g].name); text(F.m14, l, x, y, C_DIM);
        snprintf(l, sizeof l, "%d yr", a->wi_years[g]); text_r(F.n18, l, x + w, y - 3, C_TEXT);
        ch |= slider_i((Rectangle){ x, y + 24, w, 28 }, &a->wi_years[g], 1, 30); y += 70;
    }
    if (ch) { a->wi_dirty = true; }
    /* re-simulate a moment after the user stops dragging (keeps it smooth) */
    static float last_change;
    if (ch) last_change = ui_time();
    if (a->wi_dirty && ui_time() - last_change > 0.12f) {
        long long lump = a->wi_lump_k * 1000LL, sip = a->wi_sip_k * 1000LL; int n = pr->ngoals ? pr->ngoals : 1;
        for (int g = 0; g < pr->ngoals; g++)
            a->wi_prob[g] = risk_goal_probability(a->u, p, lump / n, sip / n, a->wi_years[g], pr->goals[g].target, &a->wi_p50[g]);
        a->wi_dirty = false;
    }

    y += 6;
    DrawLine((int)x, (int)y, (int)(x + w), (int)y, C_BORDER); y += 18;
    text(F.s16, "Chance of reaching each goal", x, y, C_TEXT); y += 34;
    for (int g = 0; g < pr->ngoals && g < 4; g++) {
        float was = p->goal_prob[g], now = a->wi_prob[g];
        Color rc = now >= 0.7f ? C_GAIN : now >= 0.4f ? C_WARN : C_LOSS;
        Rectangle row = { x, y, w, 64 };
        rrect(row, 12, C_BG2);
        ring(row.x + 34, row.y + 32, 22, 6, now, rc, C_CARD2);
        snprintf(l, sizeof l, "%.0f%%", now * 100); text_c(F.s13, l, row.x + 34, row.y + 25, C_TEXT);
        text(F.s14, pr->goals[g].name, row.x + 70, row.y + 12, C_TEXT);
        snprintf(l, sizeof l, "Likely %s of %s", inr_short((double)a->wi_p50[g]), inr_short((double)pr->goals[g].target));
        text(F.r12, l, row.x + 70, row.y + 36, C_DIM);
        float d = (now - was) * 100;
        if (fabsf(d) >= 0.5f) { snprintf(l, sizeof l, "%+.0f pts", d); text_r(F.s14, l, row.x + row.width - 14, row.y + 22, d > 0 ? C_GAIN : C_LOSS); }
        else text_r(F.r13, "same", row.x + row.width - 14, row.y + 23, C_FAINT);
        y += 74;
    }
    if (a->wi_dirty) spinner(D.x + D.width - 44, D.y + 140, 8, C_VIOLET);

    float by = D.y + D.height - 92;
    if (button((Rectangle){ x, by, w * 0.5f - 6, 46 }, "Reset", B_SECONDARY)) {
        a->wi_sip_k = (int)(pr->sip / 1000); a->wi_lump_k = (int)(pr->lump / 1000);
        for (int g = 0; g < pr->ngoals; g++) a->wi_years[g] = pr->goals[g].years;
        a->wi_dirty = true;
    }
    if (button((Rectangle){ x + w * 0.5f + 6, by, w * 0.5f - 6, 46 }, "Use these numbers", B_PRIMARY)) {
        pr->sip = a->wi_sip_k * 1000LL; pr->lump = a->wi_lump_k * 1000LL;
        for (int g = 0; g < pr->ngoals; g++) pr->goals[g].years = a->wi_years[g];
        profile_compute_risk(pr); profile_save(pr, a->u->dir);
        plan_rebuild(a); a->wi_open = false; a->scr_t = ui_time();
        toast(C_GAIN, "Plan updated with your new numbers");
    }
    ui_modal(false, D);
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && GetMousePosition().x < D.x && t > 0.3f) a->wi_open = false;
}

void draw_plan(App *a, Rectangle r) {
    Plan *p = a->plan; Profile *pr = &a->prof; Universe *u = a->u;
    float t = ui_time() - a->scr_t, an = ease(t / 1.1f);
    char title[96]; snprintf(title, sizeof title, "Hi %s, here's your plan", pr->name);
    char sub[200]; snprintf(sub, sizeof sub, "%s now + %s a month  \xc2\xb7  %s investor  \xc2\xb7  %d goal%s", inr((double)pr->lump), inr((double)pr->sip), risk_label(pr->risk), pr->ngoals, pr->ngoals == 1 ? "" : "s");
    header(title, sub, r);
    int live = 0; for (int i = 0; i < u->n; i++) live += u->s[i].live && !u->s[i].hidden;
    if (!live) {
        Rectangle b = { r.x + r.width - 360, r.y + 6, 360, 40 };
        rrect(b, 10, fade(C_WARN, 0.12f)); rline(b, 10, fade(C_WARN, 0.5f));
        text(F.m13, "Using sample prices.", b.x + 14, b.y + 12, C_WARN);
        if (button((Rectangle){ b.x + 168, b.y + 6, 180, 28 }, "Load real prices", B_LINK)) go(a, S_SETTINGS);
    }
    if (!p->ready) return;
    float G = 18, y = r.y + 88;
    Rectangle WD = { ui_w() - 440.0f, 0, 440, (float)ui_h() };
    if (a->wi_open) ui_modal(true, WD);

    /* ---------------- allocation ---------------- */
    float lw = r.width * 0.40f;
    float topH = r.height >= 860 ? 352 : 316;
    Rectangle A = { r.x, y, lw, topH };
    card(A);
    text(F.s18, "Where your money goes", A.x + 22, A.y + 20, C_TEXT);
    float fr[MAX_PICKS]; Color col[MAX_PICKS];
    for (int k = 0; k < p->npicks; k++) { fr[k] = p->picks[k].weight; col[k] = p->picks[k].stock < 0 ? (Color){ 70, 82, 110, 255 } : pick_color(k); }
    float dcx = A.x + 118, dcy = A.y + 196;
    glow(dcx, dcy, 140, C_VIOLET, 0.10f);
    donut(dcx, dcy, 96, 26, fr, col, p->npicks, an);
    char eq[16]; snprintf(eq, sizeof eq, "%.0f%%", p->equity * 100);
    text_c(F.b36, eq, dcx, dcy - 30, C_TEXT);
    text_c(F.r13, "in stocks", dcx, dcy + 10, C_DIM);
    char sf[24]; snprintf(sf, sizeof sf, "%.0f%% kept safe", (1 - p->equity) * 100);
    text_c(F.r12, sf, dcx, dcy + 28, C_FAINT);
    float ly = A.y + 62, lx = A.x + 238;
    for (int k = 0; k < p->npicks; k++) {
        Pick *pk = &p->picks[k];
        Rectangle row = { lx - 8, ly - 4, A.x + A.width - lx - 6, 32 };
        bool h = pk->stock >= 0 && hover(row);
        if (h) { rrect(row, 8, C_CARD2); SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) paper_open(a, pk->stock, pk->amount); }
        DrawCircle((int)lx + 4, (int)ly + 12, 5, col[k]);
        const char *nm = pk->stock < 0 ? "Safe deposit" : u->s[pk->stock].sym;
        text(F.s14, nm, lx + 18, ly + 4, C_TEXT);
        char w[16]; snprintf(w, sizeof w, "%.0f%%", pk->weight * 100);
        text_r(F.n14, w, A.x + A.width - 110, ly + 5, C_DIM);
        text_r(F.n14, inr_short((double)pk->amount), A.x + A.width - 20, ly + 5, C_TEXT);
        if (pk->stock >= 0) { const Stock *s = &u->s[pk->stock]; float v = s->vol * (s->ai.valid ? s->ai.vol_mult : 1); DrawCircle((int)(lx + 18 + text_w(F.s14, nm) + 10), (int)ly + 12, 3, risk_color(v)); }
        ly += 34;
        if (ly > A.y + A.height - 30) break;
    }

    /* ---------------- outlook ---------------- */
    Rectangle O = { r.x + lw + G, y, r.width - lw - G, topH };
    card(O);
    text(F.s18, "Your next 12 months", O.x + 22, O.y + 20, C_TEXT);
    text(F.r13, "3,000 simulated years from real price history, adjusted for what the AI found in the news", O.x + 22, O.y + 44, C_DIM);
    fan_chart(a, O, ease(t / 1.6f));

    /* ---------------- downside ---------------- */
    y += topH + G;
    long long base = pr->lump > 0 ? pr->lump : pr->sip * 12;
    Rectangle D = { r.x, y, r.width, 156 };
    card(D);
    glow(D.x + 60, D.y + 40, 160, C_LOSS, 0.06f);
    text(F.s18, "What could go wrong", D.x + 22, D.y + 18, C_TEXT);
    text(F.r13, "Most apps only show gains. Here is the downside, in rupees, before you invest.", D.x + 40 + text_w(F.s18, "What could go wrong"), D.y + 22, C_DIM);
    char pl[96]; snprintf(pl, sizeof pl, "Chance of ending the year below %s: %.0f%%", inr_short((double)base), p->prob_loss * 100);
    text_r(F.s14, pl, D.x + D.width - 22, D.y + 20, p->prob_loss > 0.25f ? C_LOSS : C_WARN);
    struct { const char *t, *d; long long v; } tiles[4] = {
        { "A bad year", "happens about 1 year in 20", -p->var95 },
        { "Worst 5% on average", "the average of the 150 worst simulations", -p->cvar95 },
        { "If a Covid-style crash hit", "Feb-Mar 2020: Nifty fell 38%", p->stress_covid },
        { "If 2008 happened again", "Global financial crisis", p->stress_2008 },
    };
    float tw = (D.width - 44 - 3 * 14) / 4;
    for (int k = 0; k < 4; k++) {
        Rectangle tl = { D.x + 22 + k * (tw + 14), D.y + 52, tw, 88 };
        rrect(tl, 12, C_BG2);
        text(F.m13, tiles[k].t, tl.x + 16, tl.y + 12, C_DIM);
        long long v = tiles[k].v;
        text(F.n24, v < 0 ? inr_signed((double)v) : inr_signed((double)v), tl.x + 16, tl.y + 34, v < 0 ? C_LOSS : C_GAIN);
        text(F.r12, tiles[k].d, tl.x + 16, tl.y + 66, C_FAINT);
    }

    /* ---------------- goals + AI briefing ---------------- */
    y += 156 + G;
    float h3 = r.y + r.height - y, gw = r.width * 0.56f;
    Rectangle Gc = { r.x, y, gw, h3 }, B = { r.x + gw + G, y, r.width - gw - G, h3 };
    card(Gc);
    text(F.s18, "Your goals", Gc.x + 22, Gc.y + 18, C_TEXT);
    if (pr->ngoals && button((Rectangle){ Gc.x + Gc.width - 150, Gc.y + 12, 130, 32 }, "What if...?", B_SECONDARY)) {
        a->wi_open = true; a->wi_dirty = true; a->wi_t = ui_time();
        a->wi_sip_k = (int)(pr->sip / 1000); a->wi_lump_k = (int)(pr->lump / 1000);
        for (int g = 0; g < pr->ngoals; g++) { a->wi_years[g] = pr->goals[g].years; a->wi_prob[g] = p->goal_prob[g]; a->wi_p50[g] = p->goal_p50[g]; }
    }
    int n = pr->ngoals ? pr->ngoals : 1;
    float cw = (Gc.width - 44 - (n - 1) * 12) / n;
    for (int g = 0; g < pr->ngoals; g++) {
        Rectangle c = { Gc.x + 22 + g * (cw + 12), Gc.y + 50, cw, h3 - 66 };
        rrect(c, 12, C_BG2);
        float pr_ = p->goal_prob[g] * an;
        Color rc = p->goal_prob[g] >= 0.7f ? C_GAIN : p->goal_prob[g] >= 0.4f ? C_WARN : C_LOSS;
        /* compact: ring on the left, everything else stacked beside it */
        float rcx = c.x + 46, rcy = c.y + c.height / 2;
        ring(rcx, rcy, 33, 7, pr_, rc, C_CARD2);
        char pc[8]; snprintf(pc, sizeof pc, "%.0f%%", pr_ * 100);
        text_c(F.s16, pc, rcx, rcy - 10, C_TEXT);
        float tx = c.x + 94, ty = fmaxf(c.y + 10, rcy - 50);
        text(F.s14, pr->goals[g].name, tx, ty, C_TEXT);
        char d[64]; snprintf(d, sizeof d, "%s in %d yr", inr_short((double)pr->goals[g].target), pr->goals[g].years);
        text(F.r13, d, tx, ty + 22, C_DIM);
        char l[96]; snprintf(l, sizeof l, "Likely to have %s", inr_short((double)p->goal_p50[g]));
        text(F.r13, l, tx, ty + 44, C_TEXT);
        long long share = pr->sip / (pr->ngoals ? pr->ngoals : 1), need = p->goal_sip_needed[g] - share;
        if (p->goal_prob[g] >= 0.7f) text(F.s13, "On track", tx, ty + 68, C_GAIN);
        else if (need > 0) { snprintf(l, sizeof l, "Add ~%s/month for 70%%", inr_short((double)need)); text(F.s13, l, tx, ty + 68, C_WARN); }
        else text(F.s13, "Close: stay consistent", tx, ty + 68, C_WARN);
    }

    card(B);
    text(F.s18, "Why? The news behind it", B.x + 22, B.y + 18, C_TEXT);
    /* two views: news about your stocks, and the stocks Vardhan kept you away from */
    float tx = B.x + 22 + text_w(F.s18, "Why? The news behind it") + 16;
    if (chip((Rectangle){ tx, B.y + 16, 100, 28 }, "Your stocks", a->plan_news_tab == 0)) a->plan_news_tab = 0;
    if (chip((Rectangle){ tx + 106, B.y + 16, 84, 28 }, "Avoided", a->plan_news_tab == 1)) a->plan_news_tab = 1;
    struct { int s, f; } all[64]; int na = 0;
    if (a->plan_news_tab == 0) {
        for (int k = 0; k < p->npicks; k++) {
            int s = p->picks[k].stock; if (s < 0) continue;
            for (int f = 0; f < u->s[s].ai.nf && na < 64; f++) { all[na].s = s; all[na].f = f; na++; }
        }
    } else {
        for (int s = 0; s < u->n; s++) {
            bool mine = false; for (int k = 0; k < p->npicks; k++) mine |= p->picks[k].stock == s;
            if (mine || u->s[s].hidden || !u->s[s].ai.valid || u->s[s].ai.sentiment >= -0.1f) continue;
            int wf = -1; for (int f = 0; f < u->s[s].ai.nf; f++) if (wf < 0 || u->s[s].ai.f[f].impact < u->s[s].ai.f[wf].impact) wf = f;
            if (wf >= 0 && na < 64) { all[na].s = s; all[na].f = wf; na++; }
        }
    }
    for (int i = 1; i < na; i++) for (int j = i; j > 0; j--) {           /* most important first */
        int x = abs(u->s[all[j].s].ai.f[all[j].f].impact), z = abs(u->s[all[j - 1].s].ai.f[all[j - 1].f].impact);
        if (x > z) { __typeof__(all[0]) tmp = all[j]; all[j] = all[j - 1]; all[j - 1] = tmp; } else break;
    }
    if (na && button((Rectangle){ B.x + B.width - 170, B.y + B.height - 36, 150, 26 }, "Read the paper \xe2\x86\x92", B_LINK)) paper_open(a, all[0].s, 0);
    if (!na) text(F.r13, a->plan_news_tab ? "No stocks have bad news right now." : "No news reading yet for your stocks.", B.x + 22, B.y + 62, C_DIM);
    float by = B.y + 54;
    for (int i = 0; i < na && by + 44 < B.y + B.height - 26; i++) {
        Factor *f = &u->s[all[i].s].ai.f[all[i].f];
        Rectangle fr = { B.x + 14, by - 4, B.width - 28, 48 };
        if (hover(fr)) { rrect(fr, 8, C_CARD2); SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) paper_open(a, all[i].s, 0); }
        Color c = f->impact > 0 ? C_GAIN : f->impact < 0 ? C_LOSS : C_DIM;
        rrect((Rectangle){ B.x + 22, by + 2, 4, 30 }, 2, c);
        text(F.s13, u->s[all[i].s].sym, B.x + 36, by, c);
        ui_scissor((float)B.x + 36, (int)by + 14, (int)B.width - 60, 36);
        text_wrap(F.r13, f->text, B.x + 36, by + 17, B.width - 60, C_DIM, 17);
        EndScissorMode();
        by += 52;
    }
    if (!na) text(F.r14, "No AI analysis yet. Connect an AI key in Settings.", B.x + 22, by, C_FAINT);
    else {
        const Analysis *x = &u->s[all[0].s].ai;
        char f[96]; snprintf(f, sizeof f, "by %s  \xc2\xb7  %s", x->by, x->when);
        text(F.r12, f, B.x + 22, B.y + B.height - 24, C_FAINT);
    }
    if (a->wi_open) { ui_modal(false, WD); draw_whatif(a, WD); }
}
