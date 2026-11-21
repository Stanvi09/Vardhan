/* scr_hood.c - "Under the hood": the data structures, live. */
#include "app.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static void ds_card(Rectangle c, const char *name, const char *where, const char *why, const char *speed, const char *live, Color col) {
    card(c);
    text(F.s16, name, c.x + 18, c.y + 16, C_TEXT);
    pill(c.x + 26 + text_w(F.s16, name), c.y + 14, where, col);
    float y = c.y + 46;
    y += text_wrap(F.r13, why, c.x + 18, y, c.width - 36, C_DIM, 18) + 4;
    text(F.m13, speed, c.x + 18, y, col);
    Rectangle lv = { c.x + 14, c.y + c.height - 38, c.width - 28, 26 };
    rrect(lv, 8, C_BG2);
    DrawCircle((int)lv.x + 12, (int)lv.y + 13, 3, C_GAIN);
    ui_scissor((float)lv.x, (int)lv.y, (int)lv.width - 6, (int)lv.height);
    text(F.r12, live, lv.x + 22, lv.y + 6, C_TEXT);
    EndScissorMode();
}

void draw_hood(App *a, Rectangle r) {
    Universe *u = a->u; Plan *p = a->plan;
    header("Under the hood", "The data structures that power Vardhan, with live numbers.", r);
    float y = r.y + 88, G = 16, gw = r.width * 0.56f;
    float cw = (gw - G) / 2, ch = (r.y + r.height - y - 3 * G) / 4;
    char l[8][160];
    int ri = uni_find(u, "RELIANCE");
    snprintf(l[0], 160, "%d tickers  \xc2\xb7  'RELIANCE' -> slot found in %d probe%s", u->map.count, u->map.last_probes, u->map.last_probes == 1 ? "" : "s");
    int ids[8]; int nf = trie_find(&u->trie, "TA", ids, 8);
    snprintf(l[1], 160, "%d nodes  \xc2\xb7  'TA' -> %d stocks in %d steps", u->trie.nodes, nf, u->trie.last_steps);
    snprintf(l[2], 160, "%d stocks scored  \xc2\xb7  top pick: %s", p->heap_considered, p->npicks > 1 && p->picks[0].stock >= 0 ? u->s[p->picks[0].stock].sym : "-");
    int edges = 0; for (int i = 0; i < u->n; i++) for (int j = i + 1; j < u->n; j++) edges += u->corr.edge[i][j];
    snprintf(l[3], 160, "%d links  \xc2\xb7  %d clusters found by BFS", edges, u->corr.ncomp);
    float lo, hi; if (ri >= 0) seg_query(&u->s[ri].seg, u->s[ri].n - 60, u->s[ri].n - 1, &lo, &hi); else lo = hi = 0;
    snprintf(l[4], 160, "RELIANCE last 60 days: high %.0f  low %.0f", hi, lo);
    snprintf(l[5], 160, "%d job%s waiting  \xc2\xb7  %s", net_pending(), net_pending() == 1 ? "" : "s", net_current()[0] ? net_current() : "idle");
    int nn = 0; for (int i = 0; i < u->n; i++) nn += news_count(u->s[i].news);
    snprintf(l[6], 160, "%d headlines stored across all stocks", nn);
    snprintf(l[7], 160, "%d screen%s deep  \xc2\xb7  side mouse button goes back", a->nav.top, a->nav.top == 1 ? "" : "s");
    (void)ri;
    struct { const char *n, *w, *why, *sp; Color c; } D[8] = {
        { "Hash table", "Ticker lookup", "Turns 'NVDA' into a slot number, so any stock is found instantly without scanning the list.", "O(1) average", C_BLUE },
        { "Trie", "Search box", "Stores tickers and company-name words letter by letter. 'tata' finds TCS and Tata Motors.", "O(length of query)", C_CYAN },
        { "Max-heap", "Your top picks", "Every stock gets a fit score for you; the heap hands back the best one each time.", "O(log n) per pick", C_VIOLET },
        { "Graph + BFS", "Diversification", "Stocks that move together (correlation > 0.6) are linked. Linked stocks are never both picked.", "BFS: O(V + E)", C_PINK },
        { "Segment tree", "Price charts", "Highest and lowest price over any dragged date range, without scanning every day.", "O(log n) per query", C_GAIN },
        { "Queue", "Background work", "Download prices, fetch news, ask the AI: done first-come, first-served by a worker.", "O(1) enqueue/dequeue", C_WARN },
        { "Linked list", "News feed", "Each stock keeps its headlines newest-first; new ones are added at the front.", "O(1) add at front", C_BLUE },
        { "Stack", "Back button", "Every screen you open is pushed; going back pops the last one.", "O(1) push/pop", C_CYAN },
    };
    for (int i = 0; i < 8; i++) ds_card((Rectangle){ r.x + (i % 2) * (cw + G), y + (i / 2) * (ch + G), cw, ch }, D[i].n, D[i].w, D[i].why, D[i].sp, l[i], D[i].c);

    /* correlation graph visual */
    Rectangle Gc = { r.x + gw + G, y, r.width - gw - G, r.y + r.height - y };
    card(Gc);
    text(F.s18, "Which stocks move together?", Gc.x + 22, Gc.y + 18, C_TEXT);
    text_wrap(F.r13, "Lines join stocks whose daily moves are strongly correlated (> 0.6). Colours are the clusters BFS found. Your plan takes at most one company from each cluster (index funds are already spread out), so one bad event can't hit everything you own.", Gc.x + 22, Gc.y + 46, Gc.width - 44, C_DIM, 18);
    float cx = Gc.x + Gc.width / 2, cy = Gc.y + Gc.height / 2 + 50, R = fminf(Gc.width - 150, Gc.height - 180) / 2 - 30;
    int vis[MAX_STOCKS], nv = 0;
    for (int i = 0; i < u->n; i++) if (!u->s[i].hidden) vis[nv++] = i;
    Vector2 pos[MAX_STOCKS];
    float spin = ui_time() * 0.03f;
    for (int k = 0; k < nv; k++) { float an = spin + 2 * PI * k / nv; pos[vis[k]] = (Vector2){ cx + cosf(an) * R, cy + sinf(an) * R }; }
    bool inplan[MAX_STOCKS] = { 0 };
    for (int k = 0; k < p->npicks; k++) if (p->picks[k].stock >= 0) inplan[p->picks[k].stock] = true;
    int hv = -1;
    for (int k = 0; k < nv; k++) if (CheckCollisionPointCircle(GetMousePosition(), pos[vis[k]], 16)) hv = vis[k];
    for (int a1 = 0; a1 < nv; a1++) for (int b1 = a1 + 1; b1 < nv; b1++) {
        int i = vis[a1], j = vis[b1];
        if (!u->corr.edge[i][j]) continue;
        bool hl = hv == i || hv == j;
        DrawLineEx(pos[i], pos[j], hl ? 2.5f : 1 + 2 * (u->corr.w[i][j] - 0.6f) * 2.5f, fade(pick_color(u->corr.comp[i]), hl ? 0.9f : 0.35f));
    }
    for (int k = 0; k < nv; k++) {
        int i = vis[k];
        Color c = pick_color(u->corr.comp[i]);
        if (inplan[i]) glow(pos[i].x, pos[i].y, 30, c, 0.5f);
        DrawCircleV(pos[i], hv == i ? 13 : 10, c);
        if (inplan[i]) DrawRing(pos[i], 13, 15, 0, 360, 24, C_TEXT);
        float an = atan2f(pos[i].y - cy, pos[i].x - cx);
        float tx = pos[i].x + cosf(an) * 30, ty = pos[i].y + sinf(an) * 22 - 7;
        const char *nm = u->s[i].sym;
        text(F.m13, nm, cosf(an) < -0.2f ? tx - text_w(F.m13, nm) : cosf(an) > 0.2f ? tx : tx - text_w(F.m13, nm) / 2, ty, inplan[i] ? C_TEXT : C_DIM);
    }
    if (hv >= 0) {
        char t[160]; int deg = graph_degree(&u->corr, hv);
        snprintf(t, sizeof t, "%s: linked to %d stock%s  \xc2\xb7  cluster %d", u->s[hv].name, deg, deg == 1 ? "" : "s", u->corr.comp[hv] + 1);
        text_c(F.s14, t, cx, Gc.y + Gc.height - 40, C_TEXT);
    } else text_c(F.r13, "White rings = stocks in your plan. Hover a dot to see its links.", cx, Gc.y + Gc.height - 40, C_FAINT);
}
