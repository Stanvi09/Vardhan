/* scr_explore.c - stock list (trie search), price chart (segment tree), AI analysis, what-if. */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *money_px(const Stock *s, float v) { static char b[4][32]; static int i; char *o = b[i++ & 3];
    if (s->mkt == MKT_US) snprintf(o, 32, "$%.2f", v); else snprintf(o, 32, "%s", inr(v)); return o; }

static void stock_list(App *a, Rectangle L) {
    Universe *u = a->u;
    card(L);
    input((Rectangle){ L.x + 16, L.y + 16, L.width - 32, 44 }, a->ex_q, sizeof a->ex_q, "Search  e.g. 'tata', 'bank', 'NVDA'", I_TEXT);
    const char *m[] = { "All", "India", "USA" };
    for (int i = 0; i < 3; i++) if (chip((Rectangle){ L.x + 16 + i * 84, L.y + 70, 76, 30 }, m[i], a->ex_mkt == i)) a->ex_mkt = i;
    int ids[MAX_STOCKS], n = 0;
    if (a->ex_q[0]) n = trie_find(&u->trie, a->ex_q, ids, MAX_STOCKS);       /* TRIE prefix search */
    else for (int i = 0; i < u->n; i++) ids[n++] = i;
    if (a->ex_q[0]) { char s[64]; snprintf(s, sizeof s, "trie: %d match%s", n, n == 1 ? "" : "es"); text_r(F.r12, s, L.x + L.width - 18, L.y + 78, C_FAINT); }
    Rectangle list = { L.x + 8, L.y + 112, L.width - 16, L.height - 120 };
    if (hover(list)) a->ex_scroll -= GetMouseWheelMove() * 50;
    if (a->ex_scroll < 0) a->ex_scroll = 0;
    ui_scissor((float)list.x, (int)list.y, (int)list.width, (int)list.height);
    float y = list.y - a->ex_scroll; int shown = 0;
    for (int k = 0; k < n; k++) {
        const Stock *s = &u->s[ids[k]];
        if (s->hidden || (a->ex_mkt == 1 && s->mkt != MKT_IN) || (a->ex_mkt == 2 && s->mkt != MKT_US)) continue;
        Rectangle row = { list.x, y, list.width, 58 };
        bool sel = a->ex == ids[k], h = hover(row) && hover(list);
        if (sel) { rrect(row, 10, fade(C_VIOLET, 0.16f)); DrawRectangleRounded((Rectangle){ row.x, row.y + 14, 3, 30 }, 1, 4, C_VIOLET); }
        else if (h) rrect(row, 10, C_CARD2);
        if (h && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { a->ex = ids[k]; a->has_sel = false; }
        if (h) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        text(F.s14, s->sym, row.x + 14, row.y + 11, C_TEXT);
        char nm[40]; snprintf(nm, sizeof nm, "%.22s", s->name);
        text(F.r12, nm, row.x + 14, row.y + 32, C_FAINT);
        int start = s->n > 120 ? s->n - 120 : 0;
        sparkline((Rectangle){ row.x + 150, row.y + 14, 64, 28 }, s->close + start, s->n - start, s->ret1y >= 0 ? C_GAIN : C_LOSS);
        text_r(F.n14, money_px(s, s->price), row.x + row.width - 12, row.y + 10, C_TEXT);
        float v = s->vol * (s->ai.valid ? s->ai.vol_mult : 1);
        text_r(F.r12, risk_word(v), row.x + row.width - 12, row.y + 32, risk_color(v));
        if (s->ai.valid) DrawCircle((int)(row.x + 236), (int)row.y + 29, 4, s->ai.sentiment > 0.15f ? C_GAIN : s->ai.sentiment < -0.15f ? C_LOSS : C_DIM);
        y += 62; shown++;
    }
    EndScissorMode();
    float maxs = shown * 62 - list.height; if (a->ex_scroll > maxs && maxs > 0) a->ex_scroll = maxs; if (maxs <= 0) a->ex_scroll = 0;
    if (!shown) text_c(F.r14, "No stock starts with that", list.x + list.width / 2, list.y + 30, C_FAINT);
}

static void price_chart(App *a, const Stock *s, Rectangle r) {
    Rectangle pl = { r.x + 8, r.y + 8, r.width - 86, r.height - 30 };
    float lo, hi; seg_query(&s->seg, 0, s->n - 1, &lo, &hi);
    float pad = (hi - lo) * 0.08f; lo -= pad; hi += pad;
    #define X(k) (pl.x + pl.width * (k) / (s->n - 1))
    #define Y(v) (pl.y + pl.height - ((v) - lo) / (hi - lo) * pl.height)
    for (int g = 0; g <= 3; g++) { float v = lo + (hi - lo) * g / 3; DrawLineEx((Vector2){ pl.x, Y(v) }, (Vector2){ pl.x + pl.width, Y(v) }, 1, fade(C_BORDER, 0.6f)); text(F.n12, money_px(s, v), pl.x + pl.width + 8, Y(v) - 7, C_FAINT); }
    Color c = s->ret1y >= 0 ? C_GAIN : C_LOSS;
    for (int k = 1; k < s->n; k++) {
        Vector2 p0 = { X(k - 1), Y(s->close[k - 1]) }, p1 = { X(k), Y(s->close[k]) };
        DrawTriangle(p0, (Vector2){ p0.x, pl.y + pl.height }, p1, fade(c, 0.06f));
        DrawTriangle(p1, (Vector2){ p0.x, pl.y + pl.height }, (Vector2){ p1.x, pl.y + pl.height }, fade(c, 0.06f));
        DrawLineEx(p0, p1, 1.8f, c);
    }
    for (int k = 0; k < s->n; k += s->n / 4) { char d[16]; snprintf(d, sizeof d, "%02d/%d", s->date[k] / 100 % 100, s->date[k] / 10000); text_c(F.r12, d, X(k), pl.y + pl.height + 6, C_FAINT); }
    /* SEGMENT TREE range query by dragging */
    Vector2 m = GetMousePosition();
    int mi = (int)roundf((m.x - pl.x) / pl.width * (s->n - 1)); if (mi < 0) mi = 0; if (mi >= s->n) mi = s->n - 1;
    if (hover(pl) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { a->drag = true; a->sel_a = a->sel_b = mi; }
    if (a->drag) { a->sel_b = mi; if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { a->drag = false; a->has_sel = abs(a->sel_a - a->sel_b) > 2; } }
    if (a->drag || a->has_sel) {
        int l = a->sel_a < a->sel_b ? a->sel_a : a->sel_b, h = a->sel_a < a->sel_b ? a->sel_b : a->sel_a;
        DrawRectangleRec((Rectangle){ X(l), pl.y, X(h) - X(l), pl.height }, fade(C_VIOLET, 0.14f));
        float qlo, qhi; seg_query(&s->seg, l, h, &qlo, &qhi);
        Rectangle tip = { pl.x + 10, pl.y + 6, 340, 52 };
        rrect(tip, 10, C_CARD2); rline(tip, 10, C_VIOLET);
        char t1[96], t2[120];
        snprintf(t1, sizeof t1, "%02d/%02d/%d  \xe2\x86\x92  %02d/%02d/%d   %s", s->date[l] % 100, s->date[l] / 100 % 100, s->date[l] / 10000, s->date[h] % 100, s->date[h] / 100 % 100, s->date[h] / 10000, pct(s->close[h] / s->close[l] - 1));
        snprintf(t2, sizeof t2, "High %s   Low %s   (segment tree, O(log n))", money_px(s, qhi), money_px(s, qlo));
        text(F.m13, t1, tip.x + 12, tip.y + 9, C_TEXT);
        text(F.r12, t2, tip.x + 12, tip.y + 30, C_DIM);
    } else if (hover(pl)) {
        DrawLineEx((Vector2){ X(mi), pl.y }, (Vector2){ X(mi), pl.y + pl.height }, 1, fade(C_TEXT, 0.35f));
        DrawCircle((int)X(mi), (int)Y(s->close[mi]), 4, c);
        char t[64]; snprintf(t, sizeof t, "%02d/%02d/%d  %s", s->date[mi] % 100, s->date[mi] / 100 % 100, s->date[mi] / 10000, money_px(s, s->close[mi]));
        text(F.m13, t, pl.x + 10, pl.y + 6, C_TEXT);
        text(F.r12, "drag to measure any period", pl.x + 10, pl.y + 26, C_FAINT);
    }
    #undef X
    #undef Y
}

void draw_explore(App *a, Rectangle r) {
    Universe *u = a->u;
    header("Explore stocks", "Real companies, real price history, and what the news means for their risk.", r);
    float y = r.y + 88, lw = 360, G = 18;
    stock_list(a, (Rectangle){ r.x, y, lw, r.y + r.height - y });
    if (a->ex < 0) return;
    Stock *s = &u->s[a->ex];
    Rectangle R = { r.x + lw + G, y, r.width - lw - G, r.y + r.height - y };

    /* header + chart */
    Rectangle H = { R.x, R.y, R.width, 312 };
    card(H);
    text(F.b28, s->name, H.x + 22, H.y + 18, C_TEXT);
    float nx = H.x + 30 + text_w(F.b28, s->name);
    pill(nx, H.y + 26, s->sym, C_CYAN); nx += pill_w(s->sym) + 6;
    pill(nx, H.y + 26, s->sector, C_DIM); nx += pill_w(s->sector) + 6;
    if (!s->live) pill(nx, H.y + 26, "SAMPLE PRICES", C_WARN);
    text(F.n24, money_px(s, s->price), H.x + 22, H.y + 58, C_TEXT);
    char ch[64]; snprintf(ch, sizeof ch, "%s today   %s in 1 year   biggest fall %s", pct(s->chg1d), pct(s->ret1y), pct(s->maxdd));
    text(F.m14, ch, H.x + 30 + text_w(F.n24, money_px(s, s->price)), H.y + 66, s->ret1y >= 0 ? C_GAIN : C_LOSS);
    char why[200]; float fit = advisor_fit(u, &a->prof, a->ex, why, sizeof why);
    float fx = H.x + H.width - 150;
    {   int na = 0; for (int i = 0; i < a->alerts.n; i++) na += !strcmp(a->alerts.a[i].sym, s->sym);
        char al[32]; snprintf(al, sizeof al, na ? "Alerts (%d)" : "Alert me", na);
        if (button((Rectangle){ fx - 130, H.y + 16, 120, 34 }, al, B_SECONDARY)) alert_open(a, a->ex); }
    ring(fx + 50, H.y + 58, 38, 8, fit / 100.0f, fit >= 62 ? C_GAIN : fit >= 42 ? C_WARN : C_LOSS, C_CARD2);
    char fs[8]; snprintf(fs, sizeof fs, "%.0f", fit); text_c(F.s20, fs, fx + 50, H.y + 46, C_TEXT);
    text_c(F.r12, "fit for you", fx + 50, H.y + 100, C_DIM);
    price_chart(a, s, (Rectangle){ H.x + 14, H.y + 112, H.width - 28, 190 });

    /* AI analysis */
    float y2 = H.y + 312 + G, h2 = R.y + R.height - y2, aw = R.width * 0.55f;
    Rectangle A = { R.x, y2, aw, h2 };
    card(A);
    text(F.s18, "AI news analysis", A.x + 22, A.y + 18, C_TEXT);
    bool busy = (s->status & 1) && net_pending();
    if (s->ai.valid && button((Rectangle){ A.x + A.width - 168 - 140, A.y + 14, 130, 30 }, "Read the story", B_SECONDARY)) paper_open(a, a->ex, 0);
    if (button((Rectangle){ A.x + A.width - 168, A.y + 14, 150, 30 }, busy ? "Working..." : "Re-read the news", B_SECONDARY)) {
        if (a->ai.provider == AI_OFFLINE || (a->ai.provider != AI_LOCAL && !a->ai.key[0])) { toast(C_WARN, "Turn on an AI in Settings first"); go(a, S_SETTINGS); }
        else { a->want_ai[a->ex] = 1; net_enqueue(JOB_NEWS, a->ex); s->status |= 1; toast(C_CYAN, "Reading today's news for %s...", s->sym); }
    }
    if (busy) spinner(A.x + A.width - 182, A.y + 29, 7, C_CYAN);
    float ay = A.y + 54;
    if (s->ai.valid) {
        float sent = s->ai.sentiment;
        const char *mood = sent > 0.25f ? "Positive news" : sent < -0.25f ? "Negative news" : "Mixed news";
        pill(A.x + 22, ay, mood, sent > 0.25f ? C_GAIN : sent < -0.25f ? C_LOSS : C_WARN);
        char rk[64]; snprintf(rk, sizeof rk, "risk x%.1f", s->ai.vol_mult);
        pill(A.x + 30 + pill_w(mood), ay, rk, s->ai.vol_mult > 1.15f ? C_LOSS : s->ai.vol_mult < 0.97f ? C_GAIN : C_DIM);
        if (s->ai.event_prob > 0.09f) { char ev[64]; snprintf(ev, sizeof ev, "%.0f%% chance of a sudden %.0f%% drop", s->ai.event_prob * 100, s->ai.event_size * 100); pill(A.x + 38 + pill_w(mood) + pill_w(rk), ay, ev, C_WARN); }
        ay += 34;
        ay += text_wrap(F.r14, s->ai.summary, A.x + 22, ay, A.width - 44, C_TEXT, 20) + 8;
        for (int k = 0; k < s->ai.nf && ay < A.y + A.height - 120; k++) {
            Factor *f = &s->ai.f[k];
            Color c = f->impact > 0 ? C_GAIN : f->impact < 0 ? C_LOSS : C_DIM;
            for (int b = 0; b < 3; b++) rrect((Rectangle){ A.x + 22 + b * 9, ay + 4, 7, 12 }, 2, b < abs(f->impact) ? c : C_CARD2);
            text(F.s13, f->category, A.x + 56, ay + 2, c);
            float hh = text_wrap(F.r13, f->text, A.x + 56 + 90, ay + 2, A.width - 56 - 90 - 22, C_DIM, 17);
            ay += (hh > 17 ? hh : 17) + 10;
        }
        /* news feed (LINKED LIST) */
        if (s->news && ay < A.y + A.height - 60) {
            DrawLine((int)A.x + 22, (int)ay, (int)(A.x + A.width - 22), (int)ay, C_BORDER);
            text(F.m13, "Headlines it read", A.x + 22, ay + 8, C_FAINT);
            ay += 30;
            for (NewsItem *n = s->news; n && ay < A.y + A.height - 46; n = n->next) {
                char line[300]; snprintf(line, sizeof line, "%s  \xc2\xb7  %s", n->when, n->source);
                text(F.r12, line, A.x + 22, ay, C_FAINT);
                ui_scissor((float)A.x + 160, (int)ay - 2, (int)A.width - 180, 18);
                text(F.r13, n->title, A.x + 160, ay - 1, C_DIM);
                EndScissorMode();
                ay += 20;
            }
        }
        char by[96]; snprintf(by, sizeof by, "by %s  \xc2\xb7  %s", s->ai.by, s->ai.when);
        text(F.r12, by, A.x + 22, A.y + A.height - 22, C_FAINT);
    } else text_wrap(F.r14, "No analysis yet. Click 'Re-read the news' to let the AI read the latest headlines.", A.x + 22, ay, A.width - 44, C_FAINT, 20);

    /* what-if */
    Rectangle W = { R.x + aw + G, y2, R.width - aw - G, h2 };
    card(W);
    text(F.s18, "What if I invest...", W.x + 22, W.y + 18, C_TEXT);
    input((Rectangle){ W.x + 22, W.y + 52, W.width - 44, 44 }, a->ex_amt, sizeof a->ex_amt, "amount", I_MONEY);
    long long amt = atoll(a->ex_amt);
    if (amt > 0 && (a->ex_out_for != a->ex || a->ex_out_amt != amt)) {
        risk_stock_outcome(u, a->ex, (float)amt, 252, &a->ex_out, 11);
        a->ex_out_for = a->ex; a->ex_out_amt = amt;
    }
    if (amt <= 0) { text(F.r14, "Type an amount to see the range.", W.x + 22, W.y + 112, C_FAINT); return; }
    Outcome *o = &a->ex_out;
    text(F.r13, "...for one year, here are 3,000 possible outcomes:", W.x + 22, W.y + 108, C_DIM);
    bool tight = W.height < 470;
    Rectangle hg = { W.x + 22, W.y + 132, W.width - 44, tight ? 62 : 86 };
    float mx = 1; for (int b = 0; b < 40; b++) if (o->hist[b] > mx) mx = o->hist[b];
    for (int b = 0; b < 40; b++) {
        float v = o->hist_lo + (o->hist_hi - o->hist_lo) * (b + 0.5f) / 40;
        float bh = o->hist[b] / mx * hg.height;
        rrect((Rectangle){ hg.x + b * hg.width / 40 + 1, hg.y + hg.height - bh, hg.width / 40 - 2, bh }, 2, v < amt ? fade(C_LOSS, 0.8f) : fade(C_GAIN, 0.8f));
    }
    float zx = hg.x + (amt - o->hist_lo) / (o->hist_hi - o->hist_lo) * hg.width;
    if (zx > hg.x && zx < hg.x + hg.width) { DrawLineEx((Vector2){ zx, hg.y - 4 }, (Vector2){ zx, hg.y + hg.height + 4 }, 2, C_TEXT); text_c(F.r12, "you put in", zx, hg.y + hg.height + 6, C_DIM); }
    text(F.r12, "lose", hg.x, hg.y + hg.height + 6, C_LOSS); text_r(F.r12, "gain", hg.x + hg.width, hg.y + hg.height + 6, C_GAIN);
    float ry = hg.y + hg.height + 28, rh = tight ? 26 : 30;
    struct { const char *l; double v; Color c; } rows[] = {
        { "Good year (1 in 20)", o->p[4] - amt, C_GAIN }, { "Most likely", o->p[2] - amt, C_CYAN },
        { "Bad year (1 in 20)", o->p[0] - amt, C_LOSS }, { "Worst 5% on average", -o->cvar95, C_LOSS } };
    for (int k = 0; k < 4; k++) { if (tight && k == 3) continue; text(F.r14, rows[k].l, W.x + 22, ry, C_DIM); text_r(F.n18, inr_signed(rows[k].v), W.x + W.width - 22, ry - 2, rows[k].c); ry += rh; }
    float c19, c08; risk_stress(u, a->ex, &c19, &c08);
    text(F.r14, "If a Covid-style crash hit", W.x + 22, ry, C_DIM); text_r(F.n18, inr_signed(amt * c19), W.x + W.width - 22, ry - 2, C_LOSS); ry += rh;
    char pl[80]; snprintf(pl, sizeof pl, "Chance of losing money in a year: %.0f%%", o->prob_loss * 100);
    rrect((Rectangle){ W.x + 22, ry + 6, W.width - 44, 36 }, 10, fade(o->prob_loss > 0.4f ? C_LOSS : C_WARN, 0.12f));
    text_c(F.s14, pl, W.x + W.width / 2, ry + 16, o->prob_loss > 0.4f ? C_LOSS : C_WARN);
    ui_scissor((float)W.x + 22, (int)(ry + 52), (int)W.width - 44, (int)(W.y + W.height - ry - 56));
    text_wrap(F.r12, why, W.x + 22, ry + 54, W.width - 44, C_FAINT, 16);
    EndScissorMode();
}
