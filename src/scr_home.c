/* scr_home.c - Home: what you invested, what it's worth, and where it is. */
#include "app.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>

void port_attach(App *a, const char *profile_file) {
    snprintf(a->port_file, sizeof a->port_file, "txn_%s", profile_file);
    port_load(&a->port, a->u->dir, a->port_file);
}
static void port_store(App *a) { if (a->port_file[0]) port_save(&a->port, a->u->dir, a->port_file); }

static float ease(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return 1 - powf(1 - t, 3); }
static const char *nice_date(int d) {
    static char b[4][24]; static int k; char *o = b[k++ & 3];
    static const char *mo[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    snprintf(o, 24, "%d %s %d", d % 100, mo[(d / 100 % 100 + 11) % 12], d / 10000);
    return o;
}
static const char *stock_name(App *a, const char *sym) {
    if (!strcmp(sym, "DEPOSIT")) return "Safe deposit";
    int i = uni_find(a->u, sym); return i >= 0 ? a->u->s[i].name : sym;
}
static Color hero0(void) { return THEME == THEME_PAPER ? (Color){ 52, 46, 40, 255 } : THEME == THEME_NIGHT ? (Color){ 79, 70, 229, 255 } : (Color){ 91, 33, 182, 255 }; }
static Color hero1(void) { return THEME == THEME_PAPER ? (Color){ 18, 16, 14, 255 } : THEME == THEME_NIGHT ? (Color){ 124, 92, 255, 255 } : (Color){ 192, 96, 250, 255 }; }

/* pill button for use on top of the coloured card */
static bool hpill(Rectangle r, const char *label, bool solid) {
    bool h = hover(r);
    rrect(r, r.height / 2 / (THEME == THEME_PAPER ? 0.45f : THEME == THEME_AMETHYST ? 1.45f : 1), solid ? (h ? (Color){ 255, 255, 255, 235 } : WHITE) : fade(WHITE, h ? 0.24f : 0.14f));
    text_c(F.s14, label, r.x + r.width / 2, r.y + (r.height - fsz(F.s14)) / 2 - 1, solid ? hero0() : WHITE);
    if (h) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    return h && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

static void open_drawer(App *a, int mode) {
    a->inv_open = true; a->inv_mode = mode; a->inv_t = ui_time(); a->inv_when = 0;
    if (mode == 1) snprintf(a->inv_amt, sizeof a->inv_amt, "%lld", a->prof.lump > 0 ? a->prof.lump : a->prof.sip * 12);
    else { a->inv_amt[0] = 0; a->inv_sym[0] = 0; }
}

/* ------------------------------ add investment ------------------------------ */
static void drawer(App *a) {
    Rectangle D = { ui_w() - 470.0f, 0, 470, ui_h() };
    float t = ui_time() - a->inv_t, sl = ease(t / 0.25f);
    DrawRectangle(0, 0, (int)ui_w() + 1, (int)ui_h() + 1, fade(BLACK, 0.45f * sl));
    D.x += (1 - sl) * D.width;
    rrect(D, 0, C_CARD); DrawLine((int)D.x, 0, (int)D.x, (int)D.height, C_BORDER);
    ui_modal(true, D);
    float x = D.x + 30, w = D.width - 60, y = 48;
    bool plan = a->inv_mode == 1;
    text(F.s24, plan ? "Invest my plan" : "Add an investment", x, y, C_TEXT);
    if (button((Rectangle){ D.x + D.width - 66, y - 2, 36, 32 }, "x", B_GHOST)) a->inv_open = false;
    y += 40;
    text_wrap(F.r14, plan ? "Splits the amount across your plan's holdings, in the plan's proportions."
                          : "Record money you put into a stock or a deposit. Vardhan tracks what it's worth from then on.", x, y, w, C_DIM, 20);
    y += 60;
    if (!plan) {
        text(F.m14, "What did you buy?", x, y, C_DIM); y += 26;
        float cx = x, cy = y;
        int opts[12], no = 0;
        for (int k = 0; k < a->plan->npicks && no < 11; k++) if (a->plan->picks[k].stock >= 0) opts[no++] = a->plan->picks[k].stock;
        for (int k = 0; k <= no; k++) {
            const char *sym = k < no ? a->u->s[opts[k]].sym : "DEPOSIT";
            const char *lab = k < no ? sym : "Deposit";
            float cw = text_w(F.m14, lab) + 28;
            if (cx + cw > x + w) { cx = x; cy += 42; }
            if (chip((Rectangle){ cx, cy, cw, 34 }, lab, !strcmp(a->inv_sym, sym))) snprintf(a->inv_sym, sizeof a->inv_sym, "%s", sym);
            cx += cw + 8;
        }
        y = cy + 46;
        text(F.r13, "Or type any ticker:", x, y + 14, C_FAINT);
        static char typed[16];
        input((Rectangle){ x + 150, y, w - 150, 42 }, typed, sizeof typed, "e.g. TCS", I_TEXT);
        if (typed[0]) { int i = uni_find(a->u, typed); if (i >= 0 && !a->u->s[i].hidden) snprintf(a->inv_sym, sizeof a->inv_sym, "%s", a->u->s[i].sym); }
        y += 62;
    }
    text(F.m14, "How much?", x, y, C_DIM); y += 26;
    input((Rectangle){ x, y, w, 50 }, a->inv_amt, sizeof a->inv_amt, "e.g. 50000", I_MONEY);
    y += 70;
    text(F.m14, "When?", x, y, C_DIM); y += 26;
    const char *wl[] = { "Today", "1 month ago", "3 months ago", "6 months ago", "1 year ago" }; int wm[] = { 0, 1, 3, 6, 12 };
    float cx = x;
    for (int k = 0; k < 5; k++) {
        float cw = text_w(F.m14, wl[k]) + 26;
        if (cx + cw > x + w) { cx = x; y += 42; }
        if (chip((Rectangle){ cx, y, cw, 34 }, wl[k], a->inv_when == wm[k])) a->inv_when = wm[k];
        cx += cw + 8;
    }
    y += 50;
    if (a->inv_when) text_wrap(F.r13, "Backdated entries use the real price on that day, so you can see how it would have done.", x, y, w, C_FAINT, 18);
    long long amt = atoll(a->inv_amt);
    bool ok = amt > 0 && (plan || a->inv_sym[0]);
    char lab[96];
    if (plan) snprintf(lab, sizeof lab, "Invest %s across %d holdings", inr((double)amt), a->plan->npicks);
    else snprintf(lab, sizeof lab, "Add %s", amt > 0 ? inr((double)amt) : "investment");
    if (button((Rectangle){ x, D.height - 100, w, 52 }, lab, ok ? B_PRIMARY : B_SECONDARY) && ok) {
        int date = a->inv_when ? port_months_ago(a->u, a->inv_when) : port_today(a->u);
        if (plan) {
            for (int k = 0; k < a->plan->npicks; k++) {
                const Pick *pk = &a->plan->picks[k];
                double part = amt * pk->weight; if (part < 1) continue;
                port_add(&a->port, a->u, date, pk->stock >= 0 ? a->u->s[pk->stock].sym : "DEPOSIT", part);
            }
        } else port_add(&a->port, a->u, date, a->inv_sym, (double)amt);
        port_store(a);
        a->inv_open = false; a->scr_t = ui_time();
        toast(C_GAIN, "Added %s to your portfolio", inr((double)amt)); ui_sound(1);
    }
    ui_modal(false, D);
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && GetMousePosition().x < D.x && t > 0.3f) a->inv_open = false;
}

/* ---------------------------------- home ---------------------------------- */
void draw_home(App *a, Rectangle r) {
    Profile *pr = &a->prof;
    float t = ui_time() - a->scr_t, an = ease(t / 1.0f);
    Rectangle DR = { ui_w() - 470.0f, 0, 470, ui_h() };
    if (a->inv_open) ui_modal(true, DR);

    /* greeting */
    time_t tt = time(NULL); int hr = localtime(&tt)->tm_hour;
    const char *gr = hr < 12 ? "Good morning" : hr < 17 ? "Good afternoon" : "Good evening";
    DrawCircle((int)r.x + 26, (int)r.y + 26, 26, fade(C_VIOLET, 0.25f));
    DrawRing((Vector2){ r.x + 26, r.y + 26 }, 24.5f, 26, 0, 360, 48, C_VIOLET);
    char ini[2] = { pr->name[0] ? pr->name[0] : 'Y', 0 };
    text_c(F.s20, ini, r.x + 26, r.y + 13, C_TEXT);
    char l[200]; snprintf(l, sizeof l, "%s, %s", gr, pr->name);
    text(F.b28, l, r.x + 68, r.y - 2, C_TEXT);
    if (a->alert_heap.n) draw_alert_banner(a, (Rectangle){ r.x + 68, r.y + 34, r.width - 68 - 410, 36 });
    else text(F.r16, "Here's how your money is doing", r.x + 68, r.y + 36, C_DIM);
    if (button((Rectangle){ r.x + r.width - 200, r.y + 4, 200, 46 }, "+  Add investment", B_PRIMARY)) open_drawer(a, 0);
    if (button((Rectangle){ r.x + r.width - 390, r.y + 4, 180, 46 }, a->report_req ? "Making PDF..." : "Download report", B_SECONDARY) && !a->report_req) { a->report_req = 2; toast(C_CYAN, "Making your PDF report..."); }

    double inv, val; port_totals(a->u, &a->port, &inv, &val);
    bool ox; double xirr = port_xirr(a->u, &a->port, &ox);
    double gain = val - inv, gp = inv > 0 ? gain / inv : 0;
    float y = r.y + 78, G = 20, hA = (r.height - 78) * 0.50f;
    float hw = r.width * 0.60f;

    /* ---------------- hero card ---------------- */
    Rectangle H = { r.x, y, hw, hA };
    grad_card(H, 22, hero0(), hero1());
    glow(H.x + H.width * 0.85f, H.y + 20, 200, WHITE, 0.10f);
    float hx = H.x + 30, hy = H.y + 26;
    text(F.m14, "Portfolio value", hx, hy, fade(WHITE, 0.75f));
    if (a->port.n) { snprintf(l, sizeof l, "%d investment%s", a->port.n, a->port.n == 1 ? "" : "s"); text_r(F.m14, l, H.x + H.width - 30, hy, fade(WHITE, 0.7f)); }
    hy += 24;
    text(THEME == THEME_PAPER ? F.d56 : F.b48, inr(val * an), hx, hy - (THEME == THEME_PAPER ? 6 : 0), WHITE);
    hy += 64;
    if (a->port.n) {
        char g[64]; snprintf(g, sizeof g, "%s %s (%+.1f%%)", gain >= 0 ? "\xe2\x96\xb2" : "\xe2\x96\xbc", inr_signed(gain), gp * 100);
        float gw = text_w(F.s14, g) + 24;
        rrect((Rectangle){ hx, hy, gw, 30 }, 15, gain >= 0 ? fade((Color){ 52, 211, 153, 255 }, 0.25f) : fade((Color){ 248, 113, 113, 255 }, 0.28f));
        text(F.s14, g, hx + 12, hy + 6, WHITE);
        snprintf(l, sizeof l, "Invested %s", inr(inv)); text(F.m14, l, hx + gw + 14, hy + 6, fade(WHITE, 0.8f));
        if (ox) { snprintf(l, sizeof l, "XIRR %+.1f%% a year", xirr * 100); text(F.m14, l, hx + gw + 26 + text_w(F.m14, "Invested ") + text_w(F.m14, inr(inv)), hy + 6, fade(WHITE, 0.8f)); }
        /* value over time */
        static double hv[60], hi[60];
        int n = port_history(a->u, &a->port, hv, hi, 60);
        Rectangle C = { H.x + 24, hy + 44, H.width - 48, H.y + H.height - hy - 44 - 70 };
        if (n > 1 && C.height > 30) {
            double lo = 1e18, top = -1e18; for (int k = 0; k < n; k++) { lo = fmin(lo, fmin(hv[k], hi[k])); top = fmax(top, fmax(hv[k], hi[k])); }
            if (top - lo < 1) top = lo + 1;
            lo -= (top - lo) * 0.1;
            int upto = (int)(an * (n - 1));
            for (int k = 0; k < upto; k++) {
                float x0 = C.x + C.width * k / (n - 1), x1 = C.x + C.width * (k + 1) / (n - 1);
                float y0 = C.y + C.height * (1 - (hv[k] - lo) / (top - lo)), y1 = C.y + C.height * (1 - (hv[k + 1] - lo) / (top - lo));
                DrawTriangle((Vector2){ x0, y0 }, (Vector2){ x0, C.y + C.height }, (Vector2){ x1, C.y + C.height }, fade(WHITE, 0.10f));
                DrawTriangle((Vector2){ x0, y0 }, (Vector2){ x1, C.y + C.height }, (Vector2){ x1, y1 }, fade(WHITE, 0.10f));
                DrawLineEx((Vector2){ x0, y0 }, (Vector2){ x1, y1 }, 2.5f, WHITE);
                float iy0 = C.y + C.height * (1 - (hi[k] - lo) / (top - lo)), iy1 = C.y + C.height * (1 - (hi[k + 1] - lo) / (top - lo));
                if (k % 2 == 0) DrawLineEx((Vector2){ x0, iy0 }, (Vector2){ x1, iy1 }, 1.2f, fade(WHITE, 0.45f));
            }
            text_r(F.r12, "\xe2\x80\x94 value    - - invested", H.x + H.width - 30, H.y + 50, fade(WHITE, 0.7f));
        }
    } else {
        text_wrap(F.r16, "Nothing invested yet. Put your plan to work: Vardhan will track what it's worth every day, including backdated entries priced from real history.", hx, hy, H.width - 60, fade(WHITE, 0.85f), 24);
        float qy = hy + 76;
        text(F.s16, "Curious? See what your plan would be worth today if you had started:", hx, qy, WHITE);
        const char *ql[] = { "3 months ago", "6 months ago", "1 year ago" }; int qm[] = { 3, 6, 12 };
        for (int k = 0; k < 3; k++)
            if (hpill((Rectangle){ hx + k * 160, qy + 32, 150, 38 }, ql[k], false)) {
                long long amt = pr->lump > 0 ? pr->lump : pr->sip * 12; int d = port_months_ago(a->u, qm[k]);
                for (int j = 0; j < a->plan->npicks; j++) { const Pick *pk = &a->plan->picks[j];
                    if (amt * pk->weight >= 1) port_add(&a->port, a->u, d, pk->stock >= 0 ? a->u->s[pk->stock].sym : "DEPOSIT", amt * pk->weight); }
                port_store(a); a->scr_t = ui_time();
                toast(C_GAIN, "Your plan, started %s, priced from real history", ql[k]); ui_sound(1);
            }
    }
    float by = H.y + H.height - 62;
    if (hpill((Rectangle){ hx, by, 170, 40 }, a->port.n ? "Add investment" : "Invest my plan", true)) open_drawer(a, a->port.n ? 0 : 1);
    if (hpill((Rectangle){ hx + 182, by, 170, 40 }, a->port.n ? "Invest my plan" : "Add one stock", false)) open_drawer(a, a->port.n ? 1 : 0);
    if (hpill((Rectangle){ hx + 364, by, 150, 40 }, "View my plan", false)) go(a, S_PLAN);

    /* ---------------- allocation ---------------- */
    Rectangle A = { r.x + hw + G, y, r.width - hw - G, hA };
    card(A);
    text(F.s18, a->port.n ? "Allocation" : "Planned allocation", A.x + 24, A.y + 20, C_TEXT);
    Holding hs[24]; int nh = port_holdings(a->u, &a->port, hs, 24);
    float fr[24]; Color col[24]; const char *nm[24]; int nn = 0; double tot = 0;
    if (nh) { for (int k = 0; k < nh && nn < 9; k++) { fr[nn] = (float)hs[k].value; nm[nn] = hs[k].sym; col[nn] = !strcmp(hs[k].sym, "DEPOSIT") ? C_FAINT : pick_color(nn); tot += hs[k].value; nn++; } }
    else for (int k = 0; k < a->plan->npicks && nn < 9; k++) { const Pick *pk = &a->plan->picks[k]; fr[nn] = pk->weight; nm[nn] = pk->stock >= 0 ? a->u->s[pk->stock].sym : "Deposit"; col[nn] = pk->stock >= 0 ? pick_color(nn) : C_FAINT; tot += pk->weight; nn++; }
    for (int k = 0; k < nn; k++) fr[k] = tot > 0 ? fr[k] / (float)tot : 0;
    float dcx = A.x + 24 + fminf(110, (A.height - 70) / 2), dcy = A.y + 56 + (A.height - 70) / 2, dr = fminf(100, (A.height - 80) / 2);
    donut(dcx, dcy, dr, dr * 0.28f, fr, col, nn, an);
    snprintf(l, sizeof l, "%d", nn); text_c(F.b36, l, dcx, dcy - 26, C_TEXT);
    text_c(F.r13, nh ? "holdings" : "in plan", dcx, dcy + 14, C_DIM);
    float lx = dcx + dr + 30, ly = A.y + 60;
    for (int k = 0; k < nn && ly < A.y + A.height - 24; k++) {
        DrawCircle((int)lx + 5, (int)ly + 9, 5, col[k]);
        text(F.m14, !strcmp(nm[k], "DEPOSIT") ? "Deposit" : nm[k], lx + 18, ly, C_TEXT);
        snprintf(l, sizeof l, "%.0f%%", fr[k] * 100); text_r(F.s14, l, A.x + A.width - 24, ly, C_DIM);
        ly += 28;
    }

    /* ---------------- quick access ---------------- */
    float y2 = y + hA + G, h2 = r.y + r.height - y2;
    Rectangle Q = { r.x, y2, r.width * 0.38f, h2 };
    card(Q);
    text(F.s18, "Quick access", Q.x + 24, Q.y + 20, C_TEXT);
    struct { const char *t; int ic; Scr s; } qa[6] = {
        { "My plan", 0, S_PLAN }, { "Ask Vani", 1, S_ASK }, { "Explore", 2, S_EXPLORE },
        { "Friend's tip", 3, S_FRIEND }, { "Proof", 4, S_PROOF }, { "The Times", 8, S_COUNT } };
    float tw = (Q.width - 48 - 2 * 14) / 3, th = (Q.height - 70 - 14) / 2;
    for (int k = 0; k < 6; k++) {
        Rectangle b = { Q.x + 24 + (k % 3) * (tw + 14), Q.y + 58 + (k / 3) * (th + 14), tw, th };
        bool h = hover(b);
        rrect(b, 16, h ? C_HOVER : C_CARD2);
        float icx = b.x + b.width / 2, icy = b.y + b.height / 2 - 14;
        DrawCircle((int)icx, (int)icy, 22, fade(C_VIOLET, h ? 0.35f : 0.20f));
        nav_icon(qa[k].ic, icx, icy, h ? C_TEXT : C_CYAN);
        text_c(F.m14, qa[k].t, icx, icy + 30, C_TEXT);
        if (h) { SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (qa[k].s == S_COUNT) { int w = -1; for (int i = 0; i < a->plan->npicks; i++) if (a->plan->picks[i].stock >= 0) { w = a->plan->picks[i].stock; break; } paper_open(a, w >= 0 ? w : 0, 0); }
            else go(a, qa[k].s); } }
    }

    /* ---------------- holdings / transactions ---------------- */
    Rectangle T = { Q.x + Q.width + G, y2, r.width - Q.width - G, h2 };
    card(T);
    if (chip((Rectangle){ T.x + 22, T.y + 16, 110, 32 }, "Holdings", a->home_tab == 0)) a->home_tab = 0;
    if (chip((Rectangle){ T.x + 140, T.y + 16, 130, 32 }, "Transactions", a->home_tab == 1)) a->home_tab = 1;
    float ry = T.y + 64, rx = T.x + 22, rw = T.width - 44;
    if (!a->port.n) {
        text(F.r14, "Your investments will appear here.", rx, ry + 10, C_DIM);
    } else if (a->home_tab == 0) {
        text(F.r12, "INVESTED", rx + rw * 0.50f, ry - 4, C_FAINT); text(F.r12, "VALUE NOW", rx + rw * 0.70f, ry - 4, C_FAINT); text_r(F.r12, "RETURN", rx + rw, ry - 4, C_FAINT);
        ry += 18;
        for (int k = 0; k < nh && ry + 44 < T.y + T.height; k++) {
            Holding *h = &hs[k];
            DrawCircle((int)rx + 18, (int)ry + 20, 18, fade(!strcmp(h->sym, "DEPOSIT") ? C_FAINT : pick_color(k), 0.25f));
            char in2[3] = { h->sym[0], h->sym[1], 0 }; text_c(F.s13, in2, rx + 18, ry + 12, C_TEXT);
            text(F.s14, !strcmp(h->sym, "DEPOSIT") ? "Deposit" : h->sym, rx + 48, ry + 4, C_TEXT);
            text(F.r12, stock_name(a, h->sym), rx + 48, ry + 23, C_DIM);
            text(F.n14, inr(h->invested), rx + rw * 0.50f, ry + 11, C_DIM);
            text(F.n14, inr(h->value), rx + rw * 0.70f, ry + 11, C_TEXT);
            double rp = h->invested > 0 ? h->value / h->invested - 1 : 0;
            snprintf(l, sizeof l, "%+.1f%%", rp * 100); text_r(F.s14, l, rx + rw, ry + 11, rp >= 0 ? C_GAIN : C_LOSS);
            ry += 48;
        }
    } else {
        for (Txn *tx = a->port.head; tx && ry + 44 < T.y + T.height; tx = tx->next) {
            Rectangle row = { rx - 8, ry - 2, rw + 16, 46 };
            bool h = hover(row);
            if (h) rrect(row, 10, C_CARD2);
            DrawCircle((int)rx + 18, (int)ry + 20, 18, fade(C_GAIN, 0.2f));
            DrawTriangle((Vector2){ rx + 18, ry + 11 }, (Vector2){ rx + 11, ry + 22 }, (Vector2){ rx + 25, ry + 22 }, C_GAIN);
            DrawRectangle((int)rx + 15, (int)ry + 21, 6, 8, C_GAIN);
            snprintf(l, sizeof l, "Bought %s", !strcmp(tx->sym, "DEPOSIT") ? "Deposit" : tx->sym);
            text(F.s14, l, rx + 48, ry + 4, C_TEXT);
            text(F.r12, nice_date(tx->date), rx + 48, ry + 23, C_DIM);
            double v = txn_value(a->u, tx, port_today(a->u));
            text_r(F.n14, inr(tx->amount), rx + rw - (h ? 40 : 0), ry + 4, C_TEXT);
            snprintf(l, sizeof l, "now %s", inr(v)); text_r(F.r12, l, rx + rw - (h ? 40 : 0), ry + 24, v >= tx->amount ? C_GAIN : C_LOSS);
            if (h && button((Rectangle){ rx + rw - 30, ry + 8, 30, 28 }, "x", B_GHOST)) { port_remove(&a->port, tx); port_store(a); break; }
            ry += 50;
        }
    }
    if (a->inv_open) { ui_modal(false, DR); drawer(a); }
}
