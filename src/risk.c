/*
 * risk.c - "how much could I gain, and how much could I lose?"
 *
 * Method: Monte Carlo simulation with a historical bootstrap.
 *   1. Take the stock's real daily returns from the last ~2 years.
 *   2. Build thousands of possible next years by re-sampling those days in
 *      random order (5-day blocks, so calm and stormy weeks stay together).
 *   3. Adjust with the AI's reading of the news:
 *        vol_mult   - news makes the stock jumpier (e.g. CEO resigned: x1.6)
 *        drift_tilt - news changes the expected return (e.g. record profit +4%)
 *        event      - a chance of one sudden shock (e.g. 22% chance of -30%)
 *   4. Report percentiles in rupees: good year (P95), likely (P50),
 *      bad year (P5), and the average of the worst 5% (CVaR / Expected Shortfall).
 * Nobody can predict a price; this shows the RANGE and how likely each part is.
 */
#include "model.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define NSIM 3000

static unsigned long long rng_s;
static inline unsigned long long rnext(void) {
    rng_s ^= rng_s >> 12; rng_s ^= rng_s << 25; rng_s ^= rng_s >> 27;
    return rng_s * 2685821657736338717ULL;
}
static inline float runit(void) { return (float)((rnext() >> 11) * (1.0 / 9007199254740992.0)); }

static int cmpf(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return x < y ? -1 : x > y; }

/* daily log returns, demeaned, most recent `max` days */
static int returns(const Stock *s, float *out, int max) {
    int start = s->n - 1 - max; if (start < 0) start = 0;
    int k = 0; double m = 0;
    for (int i = start + 1; i < s->n; i++) { out[k] = logf(s->close[i] / s->close[i - 1]); m += out[k]; k++; }
    if (!k) return 0;
    m /= k;
    for (int i = 0; i < k; i++) out[i] -= (float)m;
    return k;
}

static float adj_drift(const Stock *s) { return s->mu + (s->ai.valid ? s->ai.drift_tilt : 0); }
static float adj_volm(const Stock *s) { float v = s->ai.valid ? s->ai.vol_mult : 1; return v < 0.5f ? 0.5f : v > 2.5f ? 2.5f : v; }

void risk_stock_outcome(const Universe *u, int i, float amount, int days, Outcome *o, unsigned seed) {
    const Stock *s = &u->s[i];
    static float r[MAX_DAYS], fin[NSIM];
    int n = returns(s, r, 500);
    memset(o, 0, sizeof *o);
    if (n < 20) return;
    rng_s = 0x9E3779B97F4A7C15ULL ^ seed ^ (unsigned long long)(i * 7919);
    float mu = adj_drift(s) / 252.0f, vm = adj_volm(s);
    float ep = s->ai.valid ? s->ai.event_prob * days / 252.0f : 0, es = s->ai.valid ? s->ai.event_size : 0;
    double sum = 0;
    for (int p = 0; p < NSIM; p++) {
        float lr = 0;
        for (int d = 0; d < days; d += 5) {
            int k = (int)(runit() * (n - 5));
            for (int j = 0; j < 5 && d + j < days; j++) lr += mu + vm * r[k + j];
        }
        if (runit() < ep) lr += logf(1 + es);                 /* news-driven shock */
        fin[p] = amount * expf(lr);
        sum += fin[p];
    }
    qsort(fin, NSIM, sizeof(float), cmpf);
    const float q[5] = { 0.05f, 0.25f, 0.50f, 0.75f, 0.95f };
    for (int k = 0; k < 5; k++) o->p[k] = fin[(int)(q[k] * (NSIM - 1))];
    o->mean = (float)(sum / NSIM);
    int loss = 0; for (int p = 0; p < NSIM; p++) if (fin[p] < amount) loss++;
    o->prob_loss = (float)loss / NSIM;
    o->var95 = amount - o->p[0];
    double tail = 0; int nt = NSIM / 20;
    for (int p = 0; p < nt; p++) tail += fin[p];
    o->cvar95 = amount - (float)(tail / nt);
    o->hist_lo = fin[NSIM / 200]; o->hist_hi = fin[NSIM - 1 - NSIM / 200];
    for (int p = 0; p < NSIM; p++) {
        int b = (int)((fin[p] - o->hist_lo) / (o->hist_hi - o->hist_lo + 1e-6f) * 40);
        if (b >= 0 && b < 40) o->hist[b]++;
    }
}

/* Historical crises: Covid crash (Feb-Mar 2020) Nifty -38%, S&P -34%;
 * 2008 global financial crisis Nifty -60%, S&P -55%. A stock's expected
 * fall is its beta (how strongly it follows the market) times the index fall,
 * made worse if the news already makes it fragile. */
void risk_stress(const Universe *u, int i, float *covid, float *gfc) {
    const Stock *s = &u->s[i];
    float b = s->beta; if (b < 0.4f) b = 0.4f; if (b > 2.2f) b = 2.2f;
    float frag = s->ai.valid && s->ai.vol_mult > 1 ? 1 + (s->ai.vol_mult - 1) * 0.5f : 1;
    float c = (s->mkt == MKT_IN ? 0.38f : 0.34f) * b * frag, g = (s->mkt == MKT_IN ? 0.60f : 0.55f) * b * frag;
    *covid = -(c > 0.9f ? 0.9f : c);
    *gfc = -(g > 0.95f ? 0.95f : g);
}

/* --------------------------- portfolio outlook --------------------------- */
#define SAFE_RATE 0.07f
void risk_build_plan_outlook(const Universe *u, Plan *p, long long amount) {
    static float R[MAX_PICKS][MAX_DAYS], path[NSIM][FAN_STEPS], fin[NSIM];
    int n[MAX_PICKS], nmin = 100000;
    for (int k = 0; k < p->npicks; k++) {
        if (p->picks[k].stock < 0) { n[k] = 0; continue; }
        n[k] = returns(&u->s[p->picks[k].stock], R[k], 500);
        if (n[k] < nmin) nmin = n[k];
    }
    if (nmin > 400) nmin = 400;
    if (nmin < 20) nmin = 20;
    rng_s = 0xC0FFEE1234ULL;
    double sum = 0;
    for (int s = 0; s < NSIM; s++) {
        float v[MAX_PICKS];
        for (int k = 0; k < p->npicks; k++) v[k] = p->picks[k].weight * amount;
        path[s][0] = (float)amount;
        int step = 1;
        bool shocked[MAX_PICKS] = { 0 };
        for (int d = 0; d < 252; d += 5) {
            int off = (int)(runit() * (nmin - 5));                /* same days for every stock -> keeps correlation */
            for (int k = 0; k < p->npicks; k++) {
                int st = p->picks[k].stock;
                if (st < 0) { v[k] *= expf(SAFE_RATE * 5 / 252.0f); continue; }
                const Stock *x = &u->s[st];
                float mu = adj_drift(x) / 252.0f, vm = adj_volm(x), lr = 0;
                for (int j = 0; j < 5; j++) lr += mu + vm * R[k][n[k] - nmin + off + j];
                if (!shocked[k] && x->ai.valid && runit() < x->ai.event_prob * 5 / 252.0f) { lr += logf(1 + x->ai.event_size); shocked[k] = true; }
                v[k] *= expf(lr);
            }
            float tot = 0; for (int k = 0; k < p->npicks; k++) tot += v[k];
            if (step < FAN_STEPS) path[s][step++] = tot;
        }
        while (step < FAN_STEPS) { path[s][step] = path[s][step - 1]; step++; }
        fin[s] = path[s][FAN_STEPS - 1];
        sum += fin[s];
    }
    /* percentiles at each week -> the "fan" */
    static float col[NSIM];
    const float q[5] = { 0.05f, 0.25f, 0.50f, 0.75f, 0.95f };
    for (int t = 0; t < FAN_STEPS; t++) {
        for (int s = 0; s < NSIM; s++) col[s] = path[s][t];
        qsort(col, NSIM, sizeof(float), cmpf);
        for (int k = 0; k < 5; k++) p->fan[k][t] = col[(int)(q[k] * (NSIM - 1))];
    }
    qsort(fin, NSIM, sizeof(float), cmpf);
    p->good = (long long)p->fan[4][FAN_STEPS - 1];
    p->likely = (long long)p->fan[2][FAN_STEPS - 1];
    p->bad = (long long)p->fan[0][FAN_STEPS - 1];
    p->worst = (long long)fin[NSIM / 100];
    p->var95 = amount - p->bad;
    double tail = 0; for (int s = 0; s < NSIM / 20; s++) tail += fin[s];
    p->cvar95 = amount - (long long)(tail / (NSIM / 20));
    int loss = 0; for (int s = 0; s < NSIM; s++) if (fin[s] < amount) loss++;
    p->prob_loss = (float)loss / NSIM;
    /* crash tests */
    float c = 0, g = 0;
    for (int k = 0; k < p->npicks; k++) {
        if (p->picks[k].stock < 0) continue;
        float sc, sg; risk_stress(u, p->picks[k].stock, &sc, &sg);
        c += p->picks[k].weight * sc; g += p->picks[k].weight * sg;
    }
    p->stress_covid = (long long)(amount * c);
    p->stress_2008 = (long long)(amount * g);
    /* summary numbers */
    double er = 0, ev = 0;
    for (int k = 0; k < p->npicks; k++) {
        if (p->picks[k].stock < 0) { er += p->picks[k].weight * SAFE_RATE; continue; }
        const Stock *x = &u->s[p->picks[k].stock];
        er += p->picks[k].weight * adj_drift(x);
        ev += p->picks[k].weight * x->vol * adj_volm(x);
    }
    p->exp_ret = (float)er;
    p->vol = (float)ev * 0.8f;                  /* diversification lowers it below the weighted average */
}

/* --------------------------- goal probability --------------------------- */
/* Monthly simulation over the goal's horizon with monthly contributions.
 * News adjustments only apply to the first year: news fades, history stays. */
float risk_goal_probability(const Universe *u, const Plan *p, long long start, long long sip, int years, long long target, long long *p50) {
    static float R[MAX_PICKS][MAX_DAYS], fin[2000];
    int n[MAX_PICKS], nmin = 100000;
    for (int k = 0; k < p->npicks; k++) {
        if (p->picks[k].stock < 0) { n[k] = 0; continue; }
        n[k] = returns(&u->s[p->picks[k].stock], R[k], 500);
        if (n[k] < nmin) nmin = n[k];
    }
    if (nmin > 400) nmin = 400;
    if (nmin < 30) { *p50 = 0; return 0; }
    int months = years * 12; if (months < 1) months = 1; if (months > 360) months = 360;
    rng_s = 0xBADC0DE5ULL ^ (unsigned long long)target;
    int hit = 0; const int N = 2000;
    for (int s = 0; s < N; s++) {
        float v[MAX_PICKS];
        for (int k = 0; k < p->npicks; k++) v[k] = p->picks[k].weight * start;
        for (int m = 0; m < months; m++) {
            int off = (int)(runit() * (nmin - 21));
            for (int k = 0; k < p->npicks; k++) {
                int st = p->picks[k].stock;
                v[k] += p->picks[k].weight * sip;
                if (st < 0) { v[k] *= expf(SAFE_RATE / 12); continue; }
                const Stock *x = &u->s[st];
                bool news = m < 12 && x->ai.valid;
                float mu = (x->mu + (news ? x->ai.drift_tilt : 0)) / 252.0f, vm = news ? adj_volm(x) : 1, lr = 0;
                for (int j = 0; j < 21; j++) lr += mu + vm * R[k][n[k] - nmin + off + j];
                v[k] *= expf(lr);
            }
        }
        float tot = 0; for (int k = 0; k < p->npicks; k++) tot += v[k];
        fin[s] = tot;
        if (tot >= target) hit++;
    }
    qsort(fin, N, sizeof(float), cmpf);
    *p50 = (long long)fin[N / 2];
    float pr = (float)hit / N;
    return pr > 0.99f ? 0.99f : pr < 0.01f ? 0.01f : pr;       /* never claim certainty */
}
