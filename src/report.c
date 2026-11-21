/*
 * report.c - "Download report": a 4-page PDF of your plan, portfolio, the
 * newspaper page and how Vardhan works.
 *
 * Each page is drawn with the app's own drawing code into an off-screen
 * texture (always in the printable Paper look), then written into a PDF as a
 * compressed image. The PDF file format is written by hand: objects, a
 * cross-reference table and a trailer.
 */
#include "app.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define PAGE_W 1280.0f          /* logical page, landscape A4 proportions */
#define PAGE_H 905.0f
#define PX_W   1754             /* 150 dpi */
#define PX_H   1240

/* ------------------------------- tiny PDF writer ------------------------------- */
typedef struct { FILE *f; long off[64]; int nobj; int pages[8]; int npages; } Pdf;

static void obj_begin(Pdf *p, int id) { p->off[id] = ftell(p->f); fprintf(p->f, "%d 0 obj\n", id); }

static unsigned adler32(const unsigned char *d, int n) {
    unsigned a = 1, b = 0;
    for (int i = 0; i < n; i++) { a = (a + d[i]) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}

/* raw DEFLATE from raylib, wrapped as a zlib stream (what PDF's FlateDecode expects) */
static unsigned char *zlib(const unsigned char *d, int n, int *outn) {
    int cn = 0; unsigned char *c = CompressData(d, n, &cn);
    unsigned char *z = malloc((size_t)cn + 6);
    z[0] = 0x78; z[1] = 0x9C; memcpy(z + 2, c, (size_t)cn);
    unsigned ad = adler32(d, n);
    z[cn + 2] = (unsigned char)(ad >> 24); z[cn + 3] = (unsigned char)(ad >> 16); z[cn + 4] = (unsigned char)(ad >> 8); z[cn + 5] = (unsigned char)ad;
    MemFree(c); *outn = cn + 6;
    return z;
}

/* one page = page object + content stream + image */
static void pdf_page(Pdf *p, const unsigned char *rgb, int w, int h) {
    int img = p->nobj++, cont = p->nobj++, page = p->nobj++;
    int zn; unsigned char *z = zlib(rgb, w * h * 3, &zn);
    obj_begin(p, img);
    fprintf(p->f, "<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode /Length %d >>\nstream\n", w, h, zn);
    fwrite(z, 1, (size_t)zn, p->f); fprintf(p->f, "\nendstream\nendobj\n");
    free(z);
    char cs[96]; int cl = snprintf(cs, sizeof cs, "q 842 0 0 595 0 0 cm /Im%d Do Q", img);
    obj_begin(p, cont); fprintf(p->f, "<< /Length %d >>\nstream\n%s\nendstream\nendobj\n", cl, cs);
    obj_begin(p, page);
    fprintf(p->f, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 842 595] /Resources << /XObject << /Im%d %d 0 R >> >> /Contents %d 0 R >>\nendobj\n", img, img, cont);
    p->pages[p->npages++] = page;
}

static bool pdf_open(Pdf *p, const char *path) {
    memset(p, 0, sizeof *p);
    p->f = fopen(path, "wb"); if (!p->f) return false;
    fprintf(p->f, "%%PDF-1.4\n%%\xe2\xe3\xcf\xd3\n");
    p->nobj = 3;                                  /* 1 = catalog, 2 = page tree */
    return true;
}
static void pdf_close(Pdf *p) {
    obj_begin(p, 1); fprintf(p->f, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    obj_begin(p, 2); fprintf(p->f, "<< /Type /Pages /Count %d /Kids [", p->npages);
    for (int i = 0; i < p->npages; i++) fprintf(p->f, " %d 0 R", p->pages[i]);
    fprintf(p->f, " ] >>\nendobj\n");
    long x = ftell(p->f);
    fprintf(p->f, "xref\n0 %d\n0000000000 65535 f \n", p->nobj);
    for (int i = 1; i < p->nobj; i++) fprintf(p->f, "%010ld 00000 n \n", p->off[i]);
    fprintf(p->f, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", p->nobj, x);
    fclose(p->f);
}

/* ------------------------------- page drawing ------------------------------- */
static const char *today_long(void) { static char b[48]; time_t t = time(NULL); strftime(b, sizeof b, "%d %B %Y", localtime(&t)); return b; }

static void page_frame(App *a, int no, const char *title) {
    DrawRectangle(0, 0, (int)PAGE_W, (int)PAGE_H, (Color){ 253, 251, 247, 255 });
    logo_mark(48, 36, 40);
    text(F.s20, "Vardhan", 98, 38, C_TEXT);
    char l[160]; snprintf(l, sizeof l, "Investment report for %s  \xc2\xb7  %s", a->prof.name, today_long());
    text(F.r13, l, 99, 64, C_DIM);
    text_r(F.b28, title, PAGE_W - 48, 40, C_TEXT);
    DrawRectangle(48, 94, (int)PAGE_W - 96, 2, C_TEXT);
    snprintf(l, sizeof l, "Page %d of 4", no);
    text_r(F.r12, l, PAGE_W - 48, PAGE_H - 34, C_FAINT);
    text(F.r12, "Education only, not investment advice. Simulations re-sample real price history; real markets can do worse.", 48, PAGE_H - 34, C_FAINT);
}

static float section(const char *t, float x, float y, float w) {
    text(F.s18, t, x, y, C_TEXT);
    DrawRectangle((int)x, (int)(y + 30), (int)w, 1, C_BORDER);
    return y + 42;
}
static void kv(const char *k, const char *v, float x, float y, float w, Color vc) {
    text(F.r14, k, x, y, C_DIM); text_r(F.s14, v, x + w, y, vc);
    for (float xx = x; xx < x + w; xx += 4) DrawPixel((int)xx, (int)y + 22, C_BORDER);
}

static void page_plan(App *a) {
    Profile *pr = &a->prof; Plan *p = a->plan; char v[96];
    page_frame(a, 1, "Your plan");
    float L = 48, y = 120, cw = (PAGE_W - 96 - 40) / 2, R = L + cw + 40;
    /* left: you + goals */
    float yy = section("About you", L, y, cw);
    snprintf(v, sizeof v, "%d", pr->age); kv("Age", v, L, yy, cw, C_TEXT); yy += 28;
    kv("Risk profile", risk_label(pr->risk), L, yy, cw, C_TEXT); yy += 28;
    kv("Investing now", inr((double)pr->lump), L, yy, cw, C_TEXT); yy += 28;
    snprintf(v, sizeof v, "%s / month", inr((double)pr->sip)); kv("Every month", v, L, yy, cw, C_TEXT); yy += 28;
    kv("Markets", pr->markets == 1 ? "India" : pr->markets == 2 ? "USA" : "India and USA", L, yy, cw, C_TEXT); yy += 44;
    yy = section("Your goals", L, yy, cw);
    for (int g = 0; g < pr->ngoals; g++) {
        snprintf(v, sizeof v, "%s in %d yr", inr_short((double)pr->goals[g].target), pr->goals[g].years);
        text(F.s14, pr->goals[g].name, L, yy, C_TEXT); text(F.r13, v, L, yy + 20, C_DIM);
        snprintf(v, sizeof v, "%.0f%% chance", p->goal_prob[g] * 100);
        text_r(F.s16, v, L + cw, yy, p->goal_prob[g] >= 0.7f ? C_GAIN : p->goal_prob[g] >= 0.4f ? C_WARN : C_LOSS);
        snprintf(v, sizeof v, "likely %s", inr_short((double)p->goal_p50[g])); text_r(F.r13, v, L + cw, yy + 22, C_DIM);
        yy += 52;
    }
    yy += 10;
    yy = section("The next 12 months (3,000 simulated years)", L, yy, cw);
    long long base = pr->lump > 0 ? pr->lump : pr->sip * 12;
    kv("Good year (1 in 20)", inr_signed((double)(p->good - base)), L, yy, cw, C_GAIN); yy += 28;
    kv("Most likely", inr_signed((double)(p->likely - base)), L, yy, cw, C_TEXT); yy += 28;
    kv("Bad year (1 in 20)", inr_signed((double)-p->var95), L, yy, cw, C_LOSS); yy += 28;
    kv("Average of the worst 5%", inr_signed((double)-p->cvar95), L, yy, cw, C_LOSS); yy += 28;
    kv("If a Covid-style crash hit", inr_signed((double)p->stress_covid), L, yy, cw, C_LOSS); yy += 28;
    kv("If 2008 happened again", inr_signed((double)p->stress_2008), L, yy, cw, C_LOSS); yy += 28;
    snprintf(v, sizeof v, "%.0f%%", p->prob_loss * 100); kv("Chance of ending below what you put in", v, L, yy, cw, C_TEXT);
    /* right: allocation */
    float ry = section("Where your money goes", R, y, cw);
    float fr[12]; Color col[12];
    for (int k = 0; k < p->npicks && k < 12; k++) { fr[k] = p->picks[k].weight; col[k] = p->picks[k].stock >= 0 ? pick_color(k) : C_FAINT; }
    donut(R + 90, ry + 90, 80, 24, fr, col, p->npicks, 1);
    snprintf(v, sizeof v, "%.0f%%", p->equity * 100); text_c(F.b28, v, R + 90, ry + 70, C_TEXT); text_c(F.r12, "in stocks", R + 90, ry + 104, C_DIM);
    float ly = ry;
    for (int k = 0; k < p->npicks && k < 10; k++) {
        const Pick *pk = &p->picks[k];
        DrawCircle((int)(R + 210), (int)ly + 9, 5, col[k]);
        text(F.s14, pk->stock >= 0 ? a->u->s[pk->stock].sym : "Safe deposit", R + 224, ly, C_TEXT);
        snprintf(v, sizeof v, "%.0f%%   %s", pk->weight * 100, inr_short((double)pk->amount)); text_r(F.r14, v, R + cw, ly, C_DIM);
        ly += 24;
    }
    ry = fmaxf(ry + 200, ly + 16);
    ry = section("Why these picks", R, ry, cw);
    for (int k = 0; k < p->npicks && ry < PAGE_H - 80; k++) {
        const Pick *pk = &p->picks[k];
        text(F.s13, pk->stock >= 0 ? a->u->s[pk->stock].sym : "Deposit", R, ry, C_TEXT);
        ry += text_wrap(F.r13, pk->why, R + 100, ry, cw - 100, C_DIM, 18) + 6;
    }
}

static void page_portfolio(App *a) {
    char v[160];
    page_frame(a, 2, "Your portfolio");
    float L = 48, y = 120, W = PAGE_W - 96;
    double inv, val; port_totals(a->u, &a->port, &inv, &val);
    bool ok; double x = port_xirr(a->u, &a->port, &ok);
    struct { const char *k; char v[48]; Color c; } t[4];
    snprintf(t[0].v, 48, "%s", inr(inv)); t[0].k = "Invested"; t[0].c = C_TEXT;
    snprintf(t[1].v, 48, "%s", inr(val)); t[1].k = "Worth today"; t[1].c = C_TEXT;
    snprintf(t[2].v, 48, "%s (%+.1f%%)", inr_signed(val - inv), inv > 0 ? (val / inv - 1) * 100 : 0); t[2].k = "Profit / loss"; t[2].c = val >= inv ? C_GAIN : C_LOSS;
    if (ok) snprintf(t[3].v, 48, "%+.1f%% a year", x * 100); else snprintf(t[3].v, 48, "too early"); t[3].k = "XIRR"; t[3].c = C_TEXT;
    float tw = (W - 3 * 20) / 4;
    for (int k = 0; k < 4; k++) {
        Rectangle b = { L + k * (tw + 20), y, tw, 86 };
        rrect(b, 10, C_CARD2);
        text(F.r14, t[k].k, b.x + 18, b.y + 14, C_DIM);
        text(F.n24, t[k].v, b.x + 18, b.y + 40, t[k].c);
    }
    y += 116;
    if (!a->port.n) { text(F.r16, "No investments recorded yet.", L, y, C_DIM); return; }
    float cw = (W - 40) / 2, R = L + cw + 40;
    float yy = section("Holdings", L, y, cw);
    text(F.r12, "INVESTED", L + cw * 0.46f, yy - 6, C_FAINT); text(F.r12, "NOW", L + cw * 0.70f, yy - 6, C_FAINT); text_r(F.r12, "RETURN", L + cw, yy - 6, C_FAINT);
    yy += 12;
    Holding hs[24]; int nh = port_holdings(a->u, &a->port, hs, 24);
    for (int k = 0; k < nh && yy < PAGE_H - 80; k++) {
        text(F.s14, !strcmp(hs[k].sym, "DEPOSIT") ? "Safe deposit" : hs[k].sym, L, yy, C_TEXT);
        text(F.r14, inr(hs[k].invested), L + cw * 0.46f, yy, C_DIM);
        text(F.r14, inr(hs[k].value), L + cw * 0.70f, yy, C_TEXT);
        double rp = hs[k].value / hs[k].invested - 1;
        snprintf(v, sizeof v, "%+.1f%%", rp * 100); text_r(F.s14, v, L + cw, yy, rp >= 0 ? C_GAIN : C_LOSS);
        yy += 28;
    }
    float ry = section("Transactions (newest first)", R, y, cw);
    int shown = 0;
    for (Txn *tx = a->port.head; tx && ry < PAGE_H - 200; tx = tx->next, shown++) {
        snprintf(v, sizeof v, "%d-%02d-%02d", tx->date / 10000, tx->date / 100 % 100, tx->date % 100);
        text(F.r13, v, R, ry, C_DIM);
        text(F.s14, !strcmp(tx->sym, "DEPOSIT") ? "Safe deposit" : tx->sym, R + 110, ry, C_TEXT);
        text_r(F.r14, inr(tx->amount), R + cw, ry, C_TEXT);
        ry += 24;
    }
    if (a->port.n > shown) { snprintf(v, sizeof v, "...and %d more", a->port.n - shown); text(F.r13, v, R, ry, C_FAINT); ry += 24; }
    ry += 12;
    ry = section("Alerts", R, ry, cw);
    if (!a->alerts.n) text(F.r13, "No alerts set.", R, ry, C_FAINT);
    for (int i = 0; i < a->alerts.n && ry < PAGE_H - 60; i++) {
        Alert *al = &a->alerts.a[i];
        if (al->kind == ALERT_NEWS) snprintf(v, sizeof v, "%s: news turns bad", al->sym);
        else snprintf(v, sizeof v, "%s: price %s %.0f", al->sym, al->kind == ALERT_BELOW ? "falls below" : "rises above", al->level);
        bool on = alert_severity(a->u, al) > 0;
        text(F.r14, v, R, ry, C_TEXT); text_r(F.s13, on ? "TRIGGERED" : "watching", R + cw, ry, on ? C_LOSS : C_DIM);
        ry += 24;
    }
}

static void page_method(App *a) {
    page_frame(a, 4, "How Vardhan works");
    float L = 48, y = 120, W = PAGE_W - 96, cw = (W - 40) / 2, R = L + cw + 40;
    float yy = section("Method", L, y, cw);
    const char *m[] = {
        "Profile: risk = 55% quiz answers + 25% age + 20% time to the nearest goal.",
        "Picks: every stock gets a fit score for this profile; a max-heap hands back the best first. Stocks that move together (correlation above 0.6) are linked in a graph, and BFS keeps one per cluster.",
        "Outlook: 3,000 simulated years by re-sampling real 5-day blocks of price history, adjusted by the AI's reading of recent news (bigger swings, chance of a sudden drop, drift).",
        "Downside: Value at Risk (1 in 20 bad year), Expected Shortfall (average of the worst 5%), plus Covid-2020 and 2008 crash tests scaled by each stock's beta.",
        "Goals: month-by-month simulations including the monthly SIP give the chance of reaching each target.",
        "Proof: the plan is rebuilt from prices one year old and replayed against what happened; a rolling test checks how often a '1 in 20' bad month really occurred.",
        "Portfolio: prices on purchase days are found by binary search; XIRR is solved with Newton's method." };
    for (int i = 0; i < 7; i++) { DrawCircle((int)L + 5, (int)yy + 9, 3, C_VIOLET); yy += text_wrap(F.r14, m[i], L + 18, yy, cw - 18, C_TEXT, 20) + 10; }
    float ry = section("Data structures used", R, y, cw);
    const char *ds[][3] = {
        { "Hash table", "ticker -> stock, accounts", "O(1)" }, { "Trie", "search box, names in questions", "O(len)" },
        { "Max-heap", "ranking picks, urgent alerts", "O(log n)" }, { "Graph + BFS", "diversification clusters", "O(V+E)" },
        { "Segment tree", "chart range high / low", "O(log n)" }, { "Queue / deque", "background work, chat first", "O(1)" },
        { "Linked list", "news feed, transactions", "O(1) add" }, { "Stack", "back navigation", "O(1)" },
        { "Binary search", "price on a given date", "O(log n)" } };
    text(F.r12, "STRUCTURE", R, ry - 6, C_FAINT); text(F.r12, "USED FOR", R + 150, ry - 6, C_FAINT); text_r(F.r12, "COST", R + cw, ry - 6, C_FAINT);
    ry += 14;
    for (int i = 0; i < 9; i++) {
        text(F.s14, ds[i][0], R, ry, C_TEXT); text(F.r14, ds[i][1], R + 150, ry, C_DIM); text_r(F.s14, ds[i][2], R + cw, ry, C_VIOLET);
        ry += 28;
    }
    ry += 16;
    ry = section("Data", R, ry, cw);
    int live = 0; for (int i = 0; i < a->u->n; i++) live += a->u->s[i].live && !a->u->s[i].hidden;
    char v[200]; snprintf(v, sizeof v, "Prices: %s. News: Google News headlines read by %s. Bundled analyses were fact-checked against published reports.",
                          live ? "real daily closes from Yahoo Finance" : "sample prices (download real prices in Settings)", a->ai.provider == AI_LOCAL ? "an on-device AI (Ollama)" : "the AI");
    text_wrap(F.r14, v, R, ry, cw, C_TEXT, 20);
}

/* ------------------------------- public ------------------------------- */
bool report_make(App *a, char *path_out, int n) {
    int old = THEME;
    if (old != THEME_PAPER) theme_set(THEME_PAPER);           /* print in the paper look */
    RenderTexture2D rt = LoadRenderTexture(PX_W, PX_H);
    char dir[400]; snprintf(dir, sizeof dir, "%s/../reports", a->u->dir);
    MakeDirectory(dir);
    time_t tt = time(NULL); char d[16]; strftime(d, sizeof d, "%Y%m%d_%H%M", localtime(&tt));
    snprintf(path_out, n, "%s/Vardhan_Report_%s_%s.pdf", dir, a->prof.name, d);
    Pdf pdf;
    if (!pdf_open(&pdf, path_out)) { UnloadRenderTexture(rt); if (old != THEME_PAPER) theme_set(old); return false; }
    unsigned char *rgb = malloc((size_t)PX_W * PX_H * 3);
    for (int pg = 0; pg < 4; pg++) {
        BeginTextureMode(rt);
        ClearBackground(WHITE);
        BeginMode2D((Camera2D){ .zoom = PX_W / PAGE_W });
        if (pg == 0) page_plan(a);
        else if (pg == 1) page_portfolio(a);
        else if (pg == 2) {
            int s = -1; float worst = 1;
            for (int k = 0; k < a->plan->npicks; k++) { int i = a->plan->picks[k].stock; if (i >= 0 && a->u->s[i].ai.valid && a->u->s[i].ai.sentiment < worst) { worst = a->u->s[i].ai.sentiment; s = i; } }
            if (s < 0) s = a->u->idx_in;
            paper_print(a, s, 0, 0, PAGE_W, PAGE_H);
        } else page_method(a);
        EndMode2D();
        EndTextureMode();
        Image im = LoadImageFromTexture(rt.texture);
        ImageFlipVertical(&im);
        ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
        memcpy(rgb, im.data, (size_t)PX_W * PX_H * 3);
        UnloadImage(im);
        pdf_page(&pdf, rgb, PX_W, PX_H);
    }
    free(rgb);
    pdf_close(&pdf);
    UnloadRenderTexture(rt);
    if (old != THEME_PAPER) theme_set(old);
    return true;
}
