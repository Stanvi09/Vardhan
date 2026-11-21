/*
 * advisor.c - turns the profile + risk numbers into a personal plan.
 *
 *   1. Score every stock for THIS investor (0-100 "fit").
 *   2. Push all scores into a MAX-HEAP, pop the best one at a time.
 *   3. Skip a stock if it is linked in the correlation GRAPH to one already
 *      chosen (it would fall at the same time), so the plan is diversified.
 *   4. Size positions: steadier stocks get more weight; a safe deposit
 *      bucket is kept, larger when goals are near.
 */
#include "model.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static float comfort_vol(const Profile *p) { return 0.12f + 0.28f * p->risk; }
static int nearest_goal_years(const Profile *p) { int h = 30; for (int i = 0; i < p->ngoals; i++) if (p->goals[i].years < h) h = p->goals[i].years; return p->ngoals ? h : 5; }
static bool market_ok(const Profile *p, const Stock *s) { return (p->markets & (s->mkt == MKT_IN ? 1 : 2)) != 0; }

float advisor_fit(const Universe *u, const Profile *pr, int i, char *why, int len) {
    const Stock *s = &u->s[i];
    float vm = s->ai.valid ? s->ai.vol_mult : 1, vol = s->vol * vm;
    float mu = s->mu + (s->ai.valid ? s->ai.drift_tilt + s->ai.event_prob * s->ai.event_size : 0);
    float rf = s->mkt == MKT_IN ? 0.065f : 0.045f;
    float sharpe = (mu - rf) / (vol > 0.05f ? vol : 0.05f);
    float cv = comfort_vol(pr);
    int h = nearest_goal_years(pr);
    float fit = 55 + 35 * sharpe - 120 * fmaxf(0, vol - cv) + 12 * (s->ai.valid ? s->ai.sentiment : 0);
    if (h < 3) fit -= 40 * vol;
    if (s->fund) fit += 8;
    if (fit < 0) fit = 0;
    if (fit > 100) fit = 100;
    if (why) {
        const char *top = NULL; int worst = 0, best = 0;
        for (int k = 0; k < s->ai.nf; k++) {
            if (s->ai.f[k].impact < worst) { worst = s->ai.f[k].impact; }
            if (s->ai.f[k].impact > best) { best = s->ai.f[k].impact; top = s->ai.f[k].text; }
        }
        if (vol > cv + 0.05f) snprintf(why, len, "Swings about %.0f%% a year, more than your %s comfort level (%.0f%%).", vol * 100, risk_label(pr->risk), cv * 100);
        else if (s->fund) snprintf(why, len, "Spreads your money across the whole market, the steadiest way to own stocks.");
        else if (top && best >= 2) snprintf(why, len, "%s", top);
        else snprintf(why, len, "Moves about %.0f%% a year, within your comfort level, with a reasonable expected return.", vol * 100);
        (void)worst;
    }
    return fit;
}

void advisor_build_plan(Universe *u, const Profile *pr, Plan *plan) {
    memset(plan, 0, sizeof *plan);
    /* horizon = goals' years weighted by their size: a small near goal should
     * not force a big long-term goal into deposits */
    double tw = 0, ty = 0;
    for (int g = 0; g < pr->ngoals; g++) { tw += (double)pr->goals[g].target; ty += (double)pr->goals[g].target * pr->goals[g].years; }
    int h = tw > 0 ? (int)(ty / tw + 0.5) : 5;
    float eq = h < 2 ? 0.15f : h < 5 ? 0.40f : h < 8 ? 0.65f : 0.80f;
    eq *= 0.6f + 0.6f * pr->risk;
    if (pr->age > 35) eq -= 0.006f * (pr->age - 35);
    if (eq < 0.10f) eq = 0.10f;
    if (eq > 0.95f) eq = 0.95f;
    plan->equity = eq;

    MaxHeap heap; heap_init(&heap);
    for (int i = 0; i < u->n; i++) {
        if (u->s[i].hidden || !market_ok(pr, &u->s[i]) || u->s[i].n < 30) continue;
        heap_push(&heap, advisor_fit(u, pr, i, NULL, 0), i);         /* HEAP */
    }
    plan->heap_considered = heap.n;
    int want = 4 + (pr->risk > 0.5f) + (pr->markets == 3 ? 2 : 0);
    if (want > MAX_PICKS - 1) want = MAX_PICKS - 1;
    int chosen[MAX_PICKS], nc = 0; float fits[MAX_PICKS];
    char sectors[MAX_PICKS][24];
    HeapItem it;
    while (nc < want && heap_pop(&heap, &it)) {
        if (it.key < 25) break;
        const Stock *s = &u->s[it.id];
        bool clash = false;
        for (int k = 0; k < nc; k++) {
            if (u->corr.edge[it.id][chosen[k]] && !s->fund && !u->s[chosen[k]].fund) clash = true;   /* GRAPH */
            if (!s->fund && !strcmp(sectors[k], s->sector)) clash = true;
        }
        if (clash) continue;
        chosen[nc] = it.id; fits[nc] = it.key; snprintf(sectors[nc], 24, "%s", s->sector); nc++;
    }
    /* weights: (fit / volatility), capped at 35% of the stock part */
    float tot = 0, w[MAX_PICKS];
    for (int k = 0; k < nc; k++) {
        const Stock *s = &u->s[chosen[k]];
        float v = s->vol * (s->ai.valid ? s->ai.vol_mult : 1);
        w[k] = (fits[k] / 100.0f) / (v > 0.08f ? v : 0.08f);
        tot += w[k];
    }
    for (int pass = 0; pass < 3 && tot > 0; pass++) {
        float t2 = 0;
        for (int k = 0; k < nc; k++) { w[k] /= tot; if (w[k] > 0.35f) w[k] = 0.35f; t2 += w[k]; }
        tot = t2;
    }
    plan->npicks = 0;
    for (int k = 0; k < nc; k++) {
        Pick *p = &plan->picks[plan->npicks++];
        p->stock = chosen[k];
        p->weight = eq * w[k] / (tot > 0 ? tot : 1);
        p->fit = fits[k];
        advisor_fit(u, pr, chosen[k], p->why, sizeof p->why);
    }
    Pick *safe = &plan->picks[plan->npicks++];
    safe->stock = -1; safe->weight = 1 - eq; safe->fit = 100;
    snprintf(safe->why, sizeof safe->why, "Fixed deposit or debt fund at about 7%% a year. Money you will need soon should not be in stocks.");
    long long base = pr->lump > 0 ? pr->lump : pr->sip * 12;
    for (int k = 0; k < plan->npicks; k++) plan->picks[k].amount = (long long)(plan->picks[k].weight * base);
    plan->clusters = u->corr.ncomp;

    risk_build_plan_outlook(u, plan, base);
    for (int g = 0; g < pr->ngoals; g++) {
        const Goal *G = &pr->goals[g];
        long long start = pr->lump / (pr->ngoals ? pr->ngoals : 1), sip = pr->sip / (pr->ngoals ? pr->ngoals : 1);
        plan->goal_prob[g] = risk_goal_probability(u, plan, start, sip, G->years, G->target, &plan->goal_p50[g]);
        /* SIP needed for ~70% confidence, using a cautious growth rate */
        float r = plan->exp_ret - 0.5f * plan->vol; if (r < 0.03f) r = 0.03f;
        float mr = r / 12; int n = G->years * 12;
        double fv_start = start * pow(1 + mr, n), need = G->target - fv_start;
        plan->goal_sip_needed[g] = need <= 0 ? 0 : (long long)(need * mr / (pow(1 + mr, n) - 1));
    }
    plan->ready = true;
}

/* ------------------------------ friend's tip ------------------------------ */
void advisor_friend_check(Universe *u, const Profile *pr, int i, long long amount, int goal, Verdict *v, Outcome *o) {
    memset(v, 0, sizeof *v);
    const Stock *s = &u->s[i];
    v->fit = advisor_fit(u, pr, i, NULL, 0);
    risk_stock_outcome(u, i, (float)amount, 252, o, 77);
    int years = goal >= 0 && goal < pr->ngoals ? pr->goals[goal].years : nearest_goal_years(pr);
    float vol = s->vol * (s->ai.valid ? s->ai.vol_mult : 1), cv = comfort_vol(pr);
    int red = 0;
    for (int k = 0; k < s->ai.nf; k++) if (s->ai.f[k].impact <= -2) red++;

    v->verdict = v->fit >= 62 && red == 0 ? 0 : v->fit >= 42 && red <= 1 ? 1 : 2;
    static const char *H[] = { "Good fit for you", "Only in a small amount", "Risky for you right now" };
    snprintf(v->headline, sizeof v->headline, "%s", H[v->verdict]);

    #define R(...) do { if (v->nreasons < 5) snprintf(v->reasons[v->nreasons++], 200, __VA_ARGS__); } while (0)
    if (vol > cv + 0.03f) R("It typically swings about %.0f%% in a year. Your %s comfort level is about %.0f%%.", vol * 100, risk_label(pr->risk), cv * 100);
    else R("Its usual ups and downs (about %.0f%% a year) are within your comfort level.", vol * 100);
    for (int k = 0; k < s->ai.nf && v->nreasons < 4; k++)
        if (s->ai.f[k].impact <= -2) R("News red flag (%s): %s", s->ai.f[k].category, s->ai.f[k].text);
    if (years <= 3 && s->maxdd < -0.25f) R("Your goal is only %d year%s away, but this stock has fallen %.0f%% from a peak in the last two years.", years, years == 1 ? "" : "s", -s->maxdd * 100);
    long long base = pr->lump > 0 ? pr->lump : pr->sip * 12;
    if (base > 0 && amount > base / 5) R("This would be %lld%% of your money in a single company. Experts suggest keeping any one stock under 20%%.", amount * 100 / base);
    if (red == 0 && v->nreasons < 5) for (int k = 0; k < s->ai.nf; k++) if (s->ai.f[k].impact >= 2) { R("Positive news: %s", s->ai.f[k].text); break; }

    /* better alternatives: highest fit in the same market, steadier than this one (HEAP) */
    MaxHeap h; heap_init(&h);
    for (int j = 0; j < u->n; j++) {
        if (j == i || u->s[j].hidden || u->s[j].mkt != s->mkt || u->s[j].n < 30) continue;
        float f = advisor_fit(u, pr, j, NULL, 0);
        if (f > v->fit + 5) heap_push(&h, f, j);
    }
    HeapItem it;
    while (v->nalt < 3 && heap_pop(&h, &it)) v->alternatives[v->nalt++] = it.id;
    #undef R
}
