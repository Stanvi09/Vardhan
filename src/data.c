/* data.c - stock universe, price cache, AI-analysis cache, statistics. */
#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* Universe. "sample" numbers are only used until real prices are
 * downloaded: approximate price, yearly volatility, beta, yearly drift. */
static const struct {
    const char *sym, *yahoo, *name, *sector; int mkt; bool fund, hidden;
    float px, vol, beta, drift;
} U[] = {
    { "NIFTY",      "^NSEI",         "Nifty 50 Index",            "Index",         MKT_IN, false, true,  25500, 0.15f, 1.0f, 0.10f },
    { "SPX",        "^GSPC",         "S&P 500 Index",             "Index",         MKT_US, false, true,  7340,  0.17f, 1.0f, 0.08f },
    { "NIFTYBEES",  "NIFTYBEES.NS",  "Nifty 50 Index Fund",       "Index fund",    MKT_IN, true,  false, 285,   0.15f, 1.0f, 0.10f },
    { "RELIANCE",   "RELIANCE.NS",   "Reliance Industries",       "Energy",        MKT_IN, false, false, 1367,  0.24f, 1.0f, 0.06f },
    { "HDFCBANK",   "HDFCBANK.NS",   "HDFC Bank",                 "Banking",       MKT_IN, false, false, 740,   0.22f, 1.0f, 0.06f },
    { "ICICIBANK",  "ICICIBANK.NS",  "ICICI Bank",                "Banking",       MKT_IN, false, false, 1565,  0.22f, 1.05f, 0.12f },
    { "SBIN",       "SBIN.NS",       "State Bank of India",       "Banking",       MKT_IN, false, false, 1135,  0.27f, 1.2f, 0.15f },
    { "TCS",        "TCS.NS",        "Tata Consultancy Services", "IT services",   MKT_IN, false, false, 2460,  0.26f, 0.7f, -0.08f },
    { "INFY",       "INFY.NS",       "Infosys",                   "IT services",   MKT_IN, false, false, 1109,  0.30f, 0.8f, -0.12f },
    { "BHARTIARTL", "BHARTIARTL.NS", "Bharti Airtel",             "Telecom",       MKT_IN, false, false, 2106,  0.22f, 0.8f, 0.04f },
    { "ITC",        "ITC.NS",        "ITC",                       "FMCG",          MKT_IN, false, false, 275,   0.24f, 0.6f, -0.22f },
    { "HINDUNILVR", "HINDUNILVR.NS", "Hindustan Unilever",        "FMCG",          MKT_IN, false, false, 1963,  0.20f, 0.5f, 0.02f },
    { "ASIANPAINT", "ASIANPAINT.NS", "Asian Paints",              "Consumer",      MKT_IN, false, false, 2418,  0.25f, 0.7f, 0.05f },
    { "SUNPHARMA",  "SUNPHARMA.NS",  "Sun Pharma",                "Pharma",        MKT_IN, false, false, 1862,  0.23f, 0.5f, 0.10f },
    { "ADANIENT",   "ADANIENT.NS",   "Adani Enterprises",         "Infrastructure",MKT_IN, false, false, 2745,  0.45f, 1.5f, 0.04f },
    { "TMPV",       "TMPV.NS",       "Tata Motors PV",            "Auto",          MKT_IN, false, false, 259,   0.38f, 1.3f, -0.10f },
    { "SPY",        "SPY",           "S&P 500 Index Fund",        "Index fund",    MKT_US, true,  false, 735,   0.17f, 1.0f, 0.08f },
    { "AAPL",       "AAPL",          "Apple",                     "Big Tech",      MKT_US, false, false, 270,   0.27f, 1.1f, 0.12f },
    { "MSFT",       "MSFT",          "Microsoft",                 "Big Tech",      MKT_US, false, false, 510,   0.25f, 1.0f, 0.14f },
    { "GOOGL",      "GOOGL",         "Alphabet (Google)",         "Big Tech",      MKT_US, false, false, 290,   0.30f, 1.1f, 0.15f },
    { "AMZN",       "AMZN",          "Amazon",                    "E-commerce",    MKT_US, false, false, 230,   0.32f, 1.2f, 0.08f },
    { "META",       "META",          "Meta Platforms",            "Big Tech",      MKT_US, false, false, 638,   0.38f, 1.3f, 0.18f },
    { "NVDA",       "NVDA",          "Nvidia",                    "Semiconductors",MKT_US, false, false, 150,   0.50f, 1.8f, -0.05f },
    { "TSLA",       "TSLA",          "Tesla",                     "Auto / EV",     MKT_US, false, false, 400,   0.55f, 1.9f, 0.00f },
    { "JPM",        "JPM",           "JPMorgan Chase",            "Banking",       MKT_US, false, false, 310,   0.22f, 0.9f, 0.10f },
};
#define NU ((int)(sizeof U / sizeof U[0]))

/* --------------------------------- helpers --------------------------------- */
static unsigned rs = 12345;
static float frand(void) { rs = rs * 1103515245u + 12345u; return ((rs >> 8) & 0xFFFFFF) / (float)0x1000000; }
static float gauss(void) { float a = frand() + 1e-7f, b = frand(); return sqrtf(-2 * logf(a)) * cosf(6.2831853f * b); }

static int prev_weekday(int d) {
    struct tm t = {0}; t.tm_year = d / 10000 - 1900; t.tm_mon = d / 100 % 100 - 1; t.tm_mday = d % 100; t.tm_hour = 12;
    do { t.tm_mday--; mktime(&t); } while (t.tm_wday == 0 || t.tm_wday == 6);
    return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
}

float to_inr(const Universe *u, int i, float px) { return u->s[i].mkt == MKT_US ? px * u->usdinr : px; }
int uni_find(Universe *u, const char *t) { return hm_get(&u->map, t); }

/* ---------------------------- sample price series ---------------------------- */
/* Used only until real prices are downloaded (shown as "SAMPLE" in the app).
 * Built from a market factor + sector factor + company noise so stocks in the
 * same sector move together, plus a few shocks matching real November 2026 news. */
static void make_sample(Universe *u) {
    int days = 500;
    int dates[MAX_DAYS];
    int d = 20261121;
    for (int k = days - 1; k >= 0; k--) { d = prev_weekday(d); dates[k] = d; }
    float mkt[2][MAX_DAYS], sect[16][MAX_DAYS];
    char sectors[16][24]; int ns = 0;
    rs = 2026;
    for (int m = 0; m < 2; m++) for (int k = 0; k < days; k++) mkt[m][k] = gauss() * (m ? 0.17f : 0.15f) / sqrtf(252);
    /* recent US tech sell-off and Indian rally (Nov 2026) */
    for (int k = days - 12; k < days; k++) { mkt[1][k] -= 0.004f; mkt[0][k] += 0.003f; }
    for (int k = 0; k < days; k++) for (int q = 0; q < 16; q++) sect[q][k] = gauss() * 0.12f / sqrtf(252);

    for (int i = 0; i < u->n; i++) {
        Stock *s = &u->s[i];
        int si = -1;
        for (int q = 0; q < ns; q++) if (!strcmp(sectors[q], s->sector)) si = q;
        if (si < 0 && ns < 16) { si = ns; strcpy(sectors[ns++], s->sector); }
        int m = s->mkt;
        float beta = U[i].beta, vol = U[i].vol, mv = m ? 0.17f : 0.15f;
        float idio = sqrtf(fmaxf(vol * vol - beta * beta * mv * mv - 0.0144f * (s->hidden || s->fund ? 0 : 1), 0.0025f)) / sqrtf(252);
        float r[MAX_DAYS], lp[MAX_DAYS], mean = 0;
        for (int k = 0; k < days; k++) {
            r[k] = beta * mkt[m][k];
            if (!s->hidden && !s->fund) r[k] += sect[si][k] + gauss() * idio;
            if (s->hidden || s->fund) r[k] = mkt[m][k];
            mean += r[k];
        }
        mean /= days;
        float logp = 0;                    /* remove random drift, use the intended yearly drift */
        for (int k = 0; k < days; k++) { logp += r[k] - mean + U[i].drift / 252; lp[k] = logp; }
        /* news-matching shocks */
        if (!strcmp(s->sym, "NVDA")) { for (int k = days - 13; k < days; k++) lp[k] -= 0.097f; for (int k = days - 7; k < days; k++) lp[k] -= 0.06f; }
        if (!strcmp(s->sym, "RELIANCE")) for (int k = days - 14; k < days; k++) lp[k] += 0.11f;
        if (!strcmp(s->sym, "INFY") || !strcmp(s->sym, "TCS")) for (int k = days - 4; k < days; k++) lp[k] -= 0.03f;
        float last = lp[days - 1];
        for (int k = 0; k < days; k++) { s->date[k] = dates[k]; s->close[k] = U[i].px * expf(lp[k] - last); }
        s->n = days;
        s->live = false;
        strcpy(s->updated, "sample data");
    }
}

/* ---------------------------------- cache ---------------------------------- */
static void path(const Universe *u, char *out, int n, const char *sub, const char *sym, const char *ext) {
    snprintf(out, n, "%.200s/%s/%s.%s", u->dir, sub, sym, ext);
}

bool uni_save_prices(Universe *u, int i) {
    char p[400]; path(u, p, sizeof p, "prices", u->s[i].sym, "csv");
    FILE *f = fopen(p, "w");
    if (!f) return false;
    fprintf(f, "#%s,%s\n", u->s[i].live ? "live" : "sample", u->s[i].updated);
    for (int k = 0; k < u->s[i].n; k++) fprintf(f, "%d,%.4f\n", u->s[i].date[k], u->s[i].close[k]);
    fclose(f);
    return true;
}

static bool load_prices(Universe *u, int i) {
    char p[400], line[128]; path(u, p, sizeof p, "prices", u->s[i].sym, "csv");
    FILE *f = fopen(p, "r");
    if (!f) return false;
    Stock *s = &u->s[i]; int n = 0; bool live = false; char upd[32] = "";
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') { live = !strncmp(line + 1, "live", 4); char *c = strchr(line, ','); if (c) { snprintf(upd, sizeof upd, "%s", c + 1); upd[strcspn(upd, "\r\n")] = 0; } continue; }
        int d; float px;
        if (sscanf(line, "%d,%f", &d, &px) == 2 && px > 0 && n < MAX_DAYS) { s->date[n] = d; s->close[n] = px; n++; }
    }
    fclose(f);
    if (n < 30) return false;
    s->n = n; s->live = live; snprintf(s->updated, sizeof s->updated, "%s", upd);
    return true;
}

/* Analyses are small text files so they can be read and edited by hand:
 *   when=..., by=..., sentiment=..., vol_mult=..., drift_tilt=..., event_prob=...,
 *   event_size=..., summary=..., factor=Category|impact|text            */
bool uni_save_analysis(Universe *u, int i) {
    char p[400]; path(u, p, sizeof p, "analysis", u->s[i].sym, "txt");
    FILE *f = fopen(p, "w");
    if (!f) return false;
    Analysis *a = &u->s[i].ai;
    fprintf(f, "when=%s\nby=%s\nsentiment=%.2f\nvol_mult=%.2f\ndrift_tilt=%.3f\nevent_prob=%.2f\nevent_size=%.2f\nsummary=%s\n",
            a->when, a->by, a->sentiment, a->vol_mult, a->drift_tilt, a->event_prob, a->event_size, a->summary);
    if (a->headline[0]) fprintf(f, "headline=%s\n", a->headline);
    for (int k = 0; k < a->nf; k++) fprintf(f, "factor=%s|%d|%s\n", a->f[k].category, a->f[k].impact, a->f[k].text);
    for (NewsItem *n = u->s[i].news; n; n = n->next) fprintf(f, "news=%s|%s|%s\n", n->when, n->source, n->title);
    fclose(f);
    return true;
}

static void load_analysis(Universe *u, int i) {
    char p[400], line[700]; path(u, p, sizeof p, "analysis", u->s[i].sym, "txt");
    FILE *f = fopen(p, "r");
    if (!f) return;
    Analysis *a = &u->s[i].ai; memset(a, 0, sizeof *a);
    a->vol_mult = 1;
    NewsItem *rev = NULL;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *v = strchr(line, '='); if (!v) continue; *v++ = 0;
        if (!strcmp(line, "when")) snprintf(a->when, sizeof a->when, "%s", v);
        else if (!strcmp(line, "by")) snprintf(a->by, sizeof a->by, "%s", v);
        else if (!strcmp(line, "sentiment")) a->sentiment = (float)atof(v);
        else if (!strcmp(line, "vol_mult")) a->vol_mult = (float)atof(v);
        else if (!strcmp(line, "drift_tilt")) a->drift_tilt = (float)atof(v);
        else if (!strcmp(line, "event_prob")) a->event_prob = (float)atof(v);
        else if (!strcmp(line, "event_size")) a->event_size = (float)atof(v);
        else if (!strcmp(line, "summary")) snprintf(a->summary, sizeof a->summary, "%s", v);
        else if (!strcmp(line, "headline")) snprintf(a->headline, sizeof a->headline, "%s", v);
        else if (!strcmp(line, "factor") && a->nf < MAX_FACTORS) {
            char *b1 = strchr(v, '|'); if (!b1) continue; *b1++ = 0;
            char *b2 = strchr(b1, '|'); if (!b2) continue; *b2++ = 0;
            Factor *x = &a->f[a->nf++];
            snprintf(x->category, sizeof x->category, "%s", v); x->impact = atoi(b1); snprintf(x->text, sizeof x->text, "%s", b2);
        } else if (!strcmp(line, "news")) {
            char *b1 = strchr(v, '|'); if (!b1) continue; *b1++ = 0;
            char *b2 = strchr(b1, '|'); if (!b2) continue; *b2++ = 0;
            rev = news_push_front(rev, b2, b1, v);
        }
    }
    fclose(f);
    /* reverse so the file order (newest first) is kept */
    NewsItem *out = NULL; while (rev) { NewsItem *n = rev->next; rev->next = out; out = rev; rev = n; }
    news_free(u->s[i].news); u->s[i].news = out;
    a->valid = a->nf > 0 || a->summary[0];
}
void uni_load_analyses(Universe *u) { for (int i = 0; i < u->n; i++) load_analysis(u, i); }
void uni_reload_analysis(Universe *u, int i) { load_analysis(u, i); }
bool uni_reload_prices(Universe *u, int i) { return load_prices(u, i); }

bool uni_load_cache(Universe *u) {
    bool any = false;
    for (int i = 0; i < u->n; i++) any |= load_prices(u, i);
    char p[400]; snprintf(p, sizeof p, "%.200s/fx.txt", u->dir);
    FILE *f = fopen(p, "r");
    if (f) { float r; if (fscanf(f, "%f", &r) == 1 && r > 50 && r < 200) u->usdinr = r; fclose(f); }
    return any;
}

/* --------------------------------- setup --------------------------------- */
void uni_init(Universe *u, const char *dir) {
    memset(u, 0, sizeof *u);
    snprintf(u->dir, sizeof u->dir, "%s", dir);
    u->usdinr = 84.5f;
    hm_init(&u->map);
    trie_init(&u->trie);
    for (int i = 0; i < NU && i < MAX_STOCKS; i++) {
        Stock *s = &u->s[u->n];
        snprintf(s->sym, sizeof s->sym, "%s", U[i].sym);
        snprintf(s->yahoo, sizeof s->yahoo, "%s", U[i].yahoo);
        snprintf(s->name, sizeof s->name, "%s", U[i].name);
        snprintf(s->sector, sizeof s->sector, "%s", U[i].sector);
        s->mkt = U[i].mkt; s->fund = U[i].fund; s->hidden = U[i].hidden;
        s->ai.vol_mult = 1;
        hm_put(&u->map, s->sym, u->n);                         /* HASH TABLE */
        if (!s->hidden) {                                      /* TRIE: ticker + each name word */
            trie_add(&u->trie, s->sym, u->n);
            char w[64]; snprintf(w, sizeof w, "%s", s->name);
            for (char *t = strtok(w, " ()&"); t; t = strtok(NULL, " ()&")) if (strlen(t) > 1) trie_add(&u->trie, t, u->n);
        }
        if (!strcmp(s->sym, "NIFTY")) u->idx_in = u->n;
        if (!strcmp(s->sym, "SPX")) u->idx_us = u->n;
        u->n++;
    }
    make_sample(u);
}

void uni_free(Universe *u) {
    for (int i = 0; i < u->n; i++) { seg_free(&u->s[i].seg); news_free(u->s[i].news); u->s[i].news = NULL; }
    trie_free(&u->trie);
}

/* ------------------------------- statistics ------------------------------- */
/* Correlation of daily returns on the dates both stocks traded. */
float uni_corr(const Universe *u, int a, int b) {
    const Stock *x = &u->s[a], *y = &u->s[b];
    int i = 1, j = 1, n = 0;
    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
    int start_x = x->n > 260 ? x->n - 260 : 1, start_y = y->n > 260 ? y->n - 260 : 1;
    i = start_x; j = start_y;
    while (i < x->n && j < y->n) {
        if (x->date[i] < y->date[j]) { i++; continue; }
        if (x->date[i] > y->date[j]) { j++; continue; }
        double rx = log(x->close[i] / x->close[i - 1]), ry = log(y->close[j] / y->close[j - 1]);
        sx += rx; sy += ry; sxx += rx * rx; syy += ry * ry; sxy += rx * ry; n++;
        i++; j++;
    }
    if (n < 30) return 0;
    double cov = sxy / n - (sx / n) * (sy / n), vx = sxx / n - (sx / n) * (sx / n), vy = syy / n - (sy / n) * (sy / n);
    if (vx <= 0 || vy <= 0) return 0;
    return (float)(cov / sqrt(vx * vy));
}

void uni_compute_stats(Universe *u) {
    for (int i = 0; i < u->n; i++) {
        Stock *s = &u->s[i];
        if (s->n < 30) continue;
        s->price = s->close[s->n - 1];
        s->chg1d = s->close[s->n - 1] / s->close[s->n - 2] - 1;
        int y = s->n > 252 ? s->n - 252 : 0;
        s->ret1y = s->close[s->n - 1] / s->close[y] - 1;
        double sum = 0, sq = 0; int n = 0;
        for (int k = y + 1; k < s->n; k++) { double r = log(s->close[k] / s->close[k - 1]); sum += r; sq += r * r; n++; }
        double mean = sum / n, var = sq / n - mean * mean;
        s->vol = (float)sqrt(var * 252);
        float prior = s->mkt == MKT_IN ? 0.11f : 0.09f;
        s->mu = (float)(0.35 * mean * 252 + 0.65 * prior);          /* shrink noisy history toward the market */
        float peak = s->close[0], dd = 0;
        for (int k = 0; k < s->n; k++) { if (s->close[k] > peak) peak = s->close[k]; float x = s->close[k] / peak - 1; if (x < dd) dd = x; }
        s->maxdd = dd;
        seg_build(&s->seg, s->close, s->n);                         /* SEGMENT TREE */
    }
    for (int i = 0; i < u->n; i++) {
        Stock *s = &u->s[i];
        int ix = s->mkt == MKT_IN ? u->idx_in : u->idx_us;
        float c = uni_corr(u, i, ix);
        s->corr_index = c;
        s->beta = u->s[ix].vol > 0 ? c * s->vol / u->s[ix].vol : 1;
        if (s->hidden) s->beta = 1;
    }
    /* GRAPH of investable stocks that move together */
    graph_init(&u->corr, u->n);
    for (int a = 0; a < u->n; a++) for (int b = a + 1; b < u->n; b++) {
        if (u->s[a].hidden || u->s[b].hidden) continue;
        float c = uni_corr(u, a, b);
        if (c > 0.6f) graph_link(&u->corr, a, b, c);
    }
    graph_components(&u->corr);
}
