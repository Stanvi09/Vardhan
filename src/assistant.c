/*
 * assistant.c - "Ask Vardhan".
 *
 * Every question gets two layers:
 *   1. An INSTANT answer computed from Vardhan's own numbers (simulations,
 *      fit score, AI news factors). Always available, even offline.
 *   2. An AI explanation: the LLM receives a compact fact sheet about the
 *      user, their plan and the stocks in the question, and must answer only
 *      from those facts (grounding), so it cannot invent prices.
 */
#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

static const char *rs(double v) {                    /* ₹1,23,456 (Indian grouping), signed optional */
    static char b[8][40]; static int k; char *o = b[k++ & 7], d[32], g[40];
    snprintf(d, sizeof d, "%lld", (long long)llround(fabs(v)));
    int n = (int)strlen(d), j = 0;
    for (int i = 0; i < n; i++) { g[j++] = d[i]; int rem = n - 1 - i; if (rem > 0 && (rem == 3 || (rem > 3 && (rem - 3) % 2 == 0))) g[j++] = ','; }
    g[j] = 0;
    snprintf(o, 40, "%s\xe2\x82\xb9%s", v < -0.5 ? "-" : "", g);
    return o;
}
static const char *srs(double v) { static char b[8][44]; static int k; char *o = b[k++ & 7]; snprintf(o, 44, "%s%s", v >= 0 ? "+" : "", rs(v)); return o; }

static bool pre(const char *p, const char *w) { for (; *w; p++, w++) if (tolower((unsigned char)*p) != *w) return false; return true; }
static bool eq(const char *a, const char *b) { for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false; return !*a && !*b; }

static bool has(const char *q, const char *w) {      /* case-insensitive substring */
    size_t L = strlen(w);
    for (const char *p = q; *p; p++) { size_t i = 0; while (i < L && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)w[i])) i++; if (i == L) return true; }
    return false;
}

/* Finds stocks named in a question: exact tickers via the HASH TABLE, then
 * company-name words (4+ letters) via the TRIE. */
int chat_find_stocks(Universe *u, const char *q, int *out, int max) {
    int n = 0; char w[48];
    const char *p = q;
    while (*p && n < max) {
        while (*p && !isalnum((unsigned char)*p) && *p != '&') p++;
        int k = 0;
        while (*p && (isalnum((unsigned char)*p) || *p == '&') && k < 47) w[k++] = *p++;
        w[k] = 0;
        if (k < 2) continue;
        int id = hm_get(&u->map, w);
        if (id < 0 && k >= 4) { int t[4]; static const char *skip[] = { "will", "what", "much", "should", "could", "many", "money", "goal", "plan", "year", "with", "this", "that", "from", "have", "riskiest", "risky", "stock", "stocks", "invest", "lose", "loss", "bank", "india" };
            bool sk = false; for (size_t s = 0; s < sizeof skip / sizeof skip[0]; s++) if (eq(w, skip[s])) sk = true;
            if (!sk && trie_find(&u->trie, w, t, 4) >= 1) id = t[0]; }
        if (id >= 0 && !u->s[id].hidden) { bool dup = false; for (int i = 0; i < n; i++) if (out[i] == id) dup = true; if (!dup) out[n++] = id; }
    }
    return n;
}

/* "50k", "50,000", "Rs 2 lakh", "1.5L", "₹10000" -> rupees (0 if none) */
long long chat_amount(const char *q) {
    for (const char *p = q; *p; p++) {
        if (!isdigit((unsigned char)*p)) continue;
        char num[32]; int k = 0;
        while ((isdigit((unsigned char)*p) || *p == ',' || *p == '.') && k < 31) { if (*p != ',') num[k++] = *p; p++; }
        num[k] = 0;
        double v = atof(num);
        while (*p == ' ') p++;
        if (tolower((unsigned char)*p) == 'k') v *= 1e3;
        else if (pre(p, "lakh") || pre(p, "lac") || ((*p == 'L' || *p == 'l') && !isalpha((unsigned char)p[1]))) v *= 1e5;
        else if (pre(p, "crore") || pre(p, "cr")) v *= 1e7;
        if (v >= 500) return (long long)v;            /* ignore small numbers like "2 years" */
    }
    return 0;
}

static int which_goal(const Profile *p, const char *q) {
    static const char *kw[G_KINDS][4] = { { "house", "home", "flat", "property" }, { "master", "mba", "education", "college" },
        { "retire", "pension", "old age", "" }, { "car", "bike", "vehicle", "" }, { "travel", "trip", "vacation", "holiday" }, { "wealth", "rich", "grow", "" } };
    for (int g = 0; g < p->ngoals; g++) {
        if (has(q, p->goals[g].name)) return g;
        for (int k = 0; k < 4; k++) if (kw[p->goals[g].kind][k][0] && has(q, kw[p->goals[g].kind][k])) return g;
    }
    return 0;
}

/* --------------------------- instant answer --------------------------- */
void chat_offline(Universe *u, const Profile *pr, const Plan *pl, const char *q, char *out, int n) {
    int ids[4]; int ns = chat_find_stocks(u, q, ids, 4);
    long long amt = chat_amount(q); if (amt <= 0) amt = 50000;
    int k = 0; out[0] = 0;
    #define W(...) do { if (k < n) k += snprintf(out + k, n - k, __VA_ARGS__); } while (0)
    if (ns > 0) {
        int g = which_goal(pr, q);
        for (int s = 0; s < ns && s < 2; s++) {
            Verdict v; Outcome o;
            advisor_friend_check(u, pr, ids[s], amt, g, &v, &o);
            W("%s: %s (fit %.0f/100).\n", u->s[ids[s]].name, v.headline, v.fit);
            for (int r = 0; r < v.nreasons && r < 3; r++) W("- %s\n", v.reasons[r]);
            W("If you put %s in for a year: most likely %s, in a bad year %s, and a %.0f%% chance of ending with less than you started.\n",
              rs((double)amt), srs(o.p[2] - amt), srs(o.p[0] - amt), o.prob_loss * 100);
            if (v.nalt && s == 0) { W("Better fits for you right now: "); for (int a = 0; a < v.nalt; a++) W("%s%s", u->s[v.alternatives[a]].sym, a + 1 < v.nalt ? ", " : ".\n"); }
            if (s + 1 < ns && s == 0) W("\n");
        }
    } else if (has(q, "lose") || has(q, "loss") || has(q, "bad year") || has(q, "crash") || has(q, "worst") || has(q, "safe")) {
        long long base = pr->lump > 0 ? pr->lump : pr->sip * 12;
        W("With %s invested in your plan, over the next year:\n", rs((double)base));
        W("- A bad year (1 in 20): %s\n- Average of the worst 5%%: %s\n- If a Covid-style crash hit: %s\n- If 2008 happened again: %s\n",
          srs((double)-pl->var95), srs((double)-pl->cvar95), srs((double)pl->stress_covid), srs((double)pl->stress_2008));
        W("Chance of ending the year below what you put in: %.0f%%. About %.0f%% is kept in safe deposits to limit this.\n", pl->prob_loss * 100, (1 - pl->equity) * 100);
    } else if (has(q, "riskiest") || has(q, "most risky") || has(q, "risky")) {
        int best = -1; float bv = 0;
        for (int i = 0; i < pl->npicks; i++) { int s = pl->picks[i].stock; if (s < 0) continue; float v = u->s[s].vol * (u->s[s].ai.valid ? u->s[s].ai.vol_mult : 1); if (v > bv) { bv = v; best = s; } }
        if (best >= 0) {
            W("The riskiest holding in your plan is %s: it typically moves about %.0f%% in a year", u->s[best].name, bv * 100);
            if (u->s[best].ai.valid && u->s[best].ai.nf) W(", and the news shows: %s", u->s[best].ai.f[0].text);
            W("\nIt is kept to a small share, so a bad year for it alone has a limited effect on your total.\n");
        }
    } else if (has(q, "deposit") || has(q, "fd") || has(q, "why")) {
        W("Your plan keeps %.0f%% in stocks and %.0f%% in safe deposits.\n", pl->equity * 100, (1 - pl->equity) * 100);
        W("That's because you're a %s investor and ", risk_label(pr->risk));
        int h = 30, gi = 0; for (int g = 0; g < pr->ngoals; g++) if (pr->goals[g].years < h) { h = pr->goals[g].years; gi = g; }
        if (pr->ngoals) W("your nearest goal (%s) is %d year%s away. Money needed soon shouldn't be in stocks: a crash just before you need it can't be undone.\n", pr->goals[gi].name, h, h == 1 ? "" : "s");
        else W("you have no near-term goal.\n");
    } else {
        W("Here's where you stand:\n");
        for (int g = 0; g < pr->ngoals; g++)
            W("- %s (%s in %d yr): %.0f%% chance, likely to have %s%s\n", pr->goals[g].name, rs((double)pr->goals[g].target), pr->goals[g].years,
              pl->goal_prob[g] * 100, rs((double)pl->goal_p50[g]), pl->goal_prob[g] >= 0.7f ? " - on track" : "");
        W("Ask me about any stock, e.g. \"Should I put 50k in Reliance for my %s?\"\n", pr->ngoals ? pr->goals[0].name : "goal");
    }
    while (k > 0 && k < n && out[k - 1] == '\n') out[--k] = 0;
    #undef W
}

/* ------------------------------ AI prompt ------------------------------ */
void chat_prompt(Universe *u, const Profile *pr, const Plan *pl, const char *history, const char *q, char *out, int n) {
    int k = 0;
    #define W(...) do { if (k < n) k += snprintf(out + k, n - k, __VA_ARGS__); } while (0)
    W("You are Vani, the friendly, honest money guide inside the Vardhan app, helping first-time investors. Answer the user's question in under 90 words, "
      "in plain simple English, using ONLY the facts below (never invent prices or numbers). Always mention the downside with a rupee number. "
      "Use 2-4 short bullet points if helpful. Do not recommend leverage or trading. End with nothing else.\n\n");
    W("USER: %s, age %d, %s investor (risk %.2f). Invests %s now + %s per month.\n", pr->name, pr->age, risk_label(pr->risk), pr->risk, rs((double)pr->lump), rs((double)pr->sip));
    for (int g = 0; g < pr->ngoals; g++)
        W("GOAL: %s, %s in %d years, chance %.0f%%, likely %s, extra monthly needed for 70%%: %s\n", pr->goals[g].name, rs((double)pr->goals[g].target),
          pr->goals[g].years, pl->goal_prob[g] * 100, rs((double)pl->goal_p50[g]), rs((double)(pl->goal_sip_needed[g] > pr->sip ? pl->goal_sip_needed[g] - pr->sip / (pr->ngoals ? pr->ngoals : 1) : 0)));
    W("PLAN: %.0f%% stocks, %.0f%% safe deposit. Holdings:", pl->equity * 100, (1 - pl->equity) * 100);
    for (int i = 0; i < pl->npicks; i++) W(" %s %.0f%%;", pl->picks[i].stock < 0 ? "Deposit" : u->s[pl->picks[i].stock].sym, pl->picks[i].weight * 100);
    long long base = pr->lump > 0 ? pr->lump : pr->sip * 12;
    W("\nPLAN NEXT 12 MONTHS on %s: good year %s, likely %s, bad year %s, worst-5%% average %s, Covid-style crash %s, 2008 repeat %s, chance of loss %.0f%%.\n",
      rs((double)base), srs((double)(pl->good - base)), srs((double)(pl->likely - base)), srs((double)(pl->bad - base)), srs((double)-pl->cvar95),
      srs((double)pl->stress_covid), srs((double)pl->stress_2008), pl->prob_loss * 100);
    int ids[4]; int ns = chat_find_stocks(u, q, ids, 3);
    long long amt = chat_amount(q); if (amt <= 0) amt = 50000;
    int g = which_goal(pr, q);
    for (int s = 0; s < ns; s++) {
        Stock *st = &u->s[ids[s]];
        Verdict v; Outcome o; advisor_friend_check(u, pr, ids[s], amt, g, &v, &o);
        W("\nSTOCK %s (%s, %s): price %.2f %s, 1-year return %.0f%%, typical yearly swing %.0f%%, biggest fall in 2 years %.0f%%. Fit for this user %.0f/100 -> \"%s\".\n",
          st->name, st->sym, st->sector, st->price, st->mkt == MKT_US ? "USD" : "INR", st->ret1y * 100, st->vol * (st->ai.valid ? st->ai.vol_mult : 1) * 100, st->maxdd * 100, v.fit, v.headline);
        W("If %s invested for 1 year: likely %s, bad year %s, worst-5%% average %s, chance of loss %.0f%%.\n", rs((double)amt), srs(o.p[2] - amt), srs(o.p[0] - amt), srs(-o.cvar95), o.prob_loss * 100);
        if (st->ai.valid) {
            W("NEWS (%s): %s\n", st->ai.when, st->ai.summary);
            for (int f = 0; f < st->ai.nf; f++) W("- [%s %+d] %s\n", st->ai.f[f].category, st->ai.f[f].impact, st->ai.f[f].text);
        }
        if (v.nalt) { W("Better-fitting alternatives:"); for (int a = 0; a < v.nalt; a++) W(" %s", u->s[v.alternatives[a]].sym); W("\n"); }
    }
    if (history && history[0]) W("\nEARLIER IN THIS CHAT:\n%s\n", history);
    W("\nQUESTION: %s\nANSWER:", q);
    #undef W
}
