/*
 * backtest.c - "Proof it works".
 *
 * 1. Time machine: rebuild the user's plan using ONLY the prices that
 *    existed one year ago (the last 252 trading days are hidden), predict the
 *    range, then reveal what really happened and check it landed inside.
 * 2. Calibration: a 95% "bad month" limit should be broken about 5% of the
 *    time. We walk through history (rolling 1-year window, step 5 days) and
 *    count how often each stock actually fell further than predicted.
 */
#include "model.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

#define HIDE 252
static Universe past;                        /* big: keep off the stack */

bool bt_run(Universe *u, const Profile *pr, Backtest *bt) {
    memset(bt, 0, sizeof *bt);
    past = *u;                               /* map/trie pointers are shared read-only */
    for (int i = 0; i < past.n; i++) {
        Stock *s = &past.s[i];
        if (u->s[i].n < HIDE + 60) { snprintf(bt->msg, sizeof bt->msg, "Not enough price history for %s", s->sym); return false; }
        s->n -= HIDE;
        memset(&s->seg, 0, sizeof s->seg);
        s->news = NULL; s->ai.valid = false; s->status = 0;
    }
    uni_compute_stats(&past);
    advisor_build_plan(&past, pr, &bt->plan);
    for (int i = 0; i < past.n; i++) seg_free(&past.s[i].seg);
    if (!bt->plan.ready) { snprintf(bt->msg, sizeof bt->msg, "Could not build the past plan"); return false; }

    const Stock *ref = &u->s[u->idx_in];
    bt->from = ref->date[ref->n - 1 - HIDE]; bt->to = ref->date[ref->n - 1];
    bt->amount = pr->lump > 0 ? pr->lump : pr->sip * 12;
    double A = (double)bt->amount;

    /* what really happened, week by week, to the plan built a year ago */
    for (int w = 0; w < FAN_STEPS; w++) {
        int d = w * 5 > HIDE ? HIDE : w * 5;
        double v = 0;
        for (int k = 0; k < bt->plan.npicks; k++) {
            const Pick *pk = &bt->plan.picks[k];
            if (pk->stock < 0) { v += pk->weight * A * exp(0.07 * d / 252.0); continue; }
            const Stock *s = &u->s[pk->stock];
            int b = s->n - 1 - HIDE;
            v += pk->weight * A * s->close[b + d] / s->close[b];
        }
        bt->actual[w] = (float)v;
        const Stock *ix = ref; int b = ix->n - 1 - HIDE;
        bt->index[w] = (float)(A * ix->close[b + d] / ix->close[b]);
        if (bt->actual[w] >= bt->plan.fan[0][w] && bt->actual[w] <= bt->plan.fan[4][w]) bt->weeks_inside++;
    }
    float fin = bt->actual[FAN_STEPS - 1];
    bt->inside = fin >= bt->plan.fan[0][FAN_STEPS - 1] && fin <= bt->plan.fan[4][FAN_STEPS - 1];
    bt->in_middle = fin >= bt->plan.fan[1][FAN_STEPS - 1] && fin <= bt->plan.fan[3][FAN_STEPS - 1];
    /* worst point along the way vs the predicted bad-year number */
    float lo = 1e30f; for (int w = 0; w < FAN_STEPS; w++) if (bt->actual[w] < lo) lo = bt->actual[w];
    bt->worst_dip = lo - (float)A;

    /* the "friend's tip": the single stock that had risen most in the year before */
    int hot = -1; float best = -1;
    for (int i = 0; i < past.n; i++) if (!past.s[i].hidden && !past.s[i].fund && past.s[i].ret1y > best) { best = past.s[i].ret1y; hot = i; }
    bt->hot = hot;
    if (hot >= 0) {
        const Stock *s = &u->s[hot]; int b = s->n - 1 - HIDE;
        float mn = 1e30f;
        for (int d = 0; d <= HIDE; d++) { float v = (float)(A * s->close[b + d] / s->close[b]); if (v < mn) mn = v; }
        bt->hot_final = (float)(A * s->close[s->n - 1] / s->close[b]);
        bt->hot_dip = mn - (float)A;
        bt->hot_past_ret = best;
    }

    /* VaR calibration on every investable stock */
    for (int i = 0; i < u->n && bt->ncal < MAX_STOCKS; i++) {
        const Stock *s = &u->s[i]; if (s->hidden) continue;
        int tests = 0, breaks = 0; static float r21[MAX_DAYS];
        for (int t = 252; t + 21 < s->n; t += 5) {
            int m = 0;
            for (int j = t - 252; j + 21 <= t; j++) r21[m++] = s->close[j + 21] / s->close[j] - 1;
            /* 5th percentile by partial selection */
            int kth = (int)(0.05f * m);
            for (int a2 = 0; a2 <= kth; a2++) { int mi = a2; for (int b2 = a2 + 1; b2 < m; b2++) if (r21[b2] < r21[mi]) mi = b2; float tmp = r21[a2]; r21[a2] = r21[mi]; r21[mi] = tmp; }
            float var = r21[kth], real = s->close[t + 21] / s->close[t] - 1;
            tests++; if (real < var) breaks++;
        }
        if (tests < 5) continue;
        bt->cal[bt->ncal].stock = i; bt->cal[bt->ncal].tests = tests; bt->cal[bt->ncal].breaks = breaks; bt->ncal++;
        bt->tests += tests; bt->breaks += breaks;
    }
    bt->ok = true;
    return true;
}
