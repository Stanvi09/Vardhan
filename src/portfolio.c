/*
 * portfolio.c - what you actually invested, and what it's worth now.
 *
 * Every investment is a transaction kept in a LINKED LIST (newest first) and
 * saved to the user's own file. To value one, we find the price on the day you
 * bought with BINARY SEARCH over the sorted trading dates, then compare it with
 * today's price. XIRR (your yearly return, taking timing into account) is
 * solved with Newton's method.
 */
#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SAFE_RATE 0.07

/* days between two YYYYMMDD dates (simple civil-date arithmetic) */
static long days_from_civil(int ymd) {
    int y = ymd / 10000, m = ymd / 100 % 100, d = ymd % 100;
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}
long port_days(int a, int b) { return days_from_civil(b) - days_from_civil(a); }

/* BINARY SEARCH: last trading day on or before `date` (-1 if none) */
int price_index(const Stock *s, int date) {
    int lo = 0, hi = s->n - 1, ans = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (s->date[mid] <= date) { ans = mid; lo = mid + 1; } else hi = mid - 1;
    }
    return ans;
}

int port_today(const Universe *u) { const Stock *s = &u->s[u->idx_in]; return s->n ? s->date[s->n - 1] : 20261120; }

/* date `months` before today */
int port_months_ago(const Universe *u, int months) {
    int t = port_today(u), y = t / 10000, m = t / 100 % 100, d = t % 100;
    m -= months; while (m <= 0) { m += 12; y--; }
    if (d > 28) d = 28;
    return y * 10000 + m * 100 + d;
}

void port_free(Portfolio *p) { Txn *t = p->head; while (t) { Txn *n = t->next; free(t); t = n; } p->head = NULL; p->n = 0; }

/* Inserts keeping the list sorted newest-first. */
Txn *port_add(Portfolio *p, Universe *u, int date, const char *sym, double amount) {
    Txn *t = calloc(1, sizeof *t);
    t->date = date; t->amount = amount; snprintf(t->sym, sizeof t->sym, "%s", sym);
    int i = strcmp(sym, "DEPOSIT") ? uni_find(u, sym) : -1;
    if (i >= 0) { int k = price_index(&u->s[i], date); if (k < 0) k = 0; t->price = u->s[i].close[k]; t->date = u->s[i].date[k]; }
    else t->price = 1;
    Txn **pp = &p->head;
    while (*pp && (*pp)->date > t->date) pp = &(*pp)->next;
    t->next = *pp; *pp = t; p->n++;
    return t;
}

bool port_remove(Portfolio *p, Txn *t) {
    for (Txn **pp = &p->head; *pp; pp = &(*pp)->next)
        if (*pp == t) { *pp = t->next; free(t); p->n--; return true; }
    return false;
}

/* value of one transaction on `date` */
double txn_value(const Universe *u, const Txn *t, int date) {
    if (date < t->date) return 0;
    if (!strcmp(t->sym, "DEPOSIT")) return t->amount * pow(1 + SAFE_RATE, port_days(t->date, date) / 365.0);
    int i = uni_find((Universe *)u, t->sym); if (i < 0) return t->amount;
    int k = price_index(&u->s[i], date); if (k < 0) return t->amount;
    return t->amount * u->s[i].close[k] / t->price;
}

void port_totals(const Universe *u, const Portfolio *p, double *invested, double *value) {
    int today = port_today(u); double inv = 0, val = 0;
    for (Txn *t = p->head; t; t = t->next) { inv += t->amount; val += txn_value(u, t, today); }
    *invested = inv; *value = val;
}

/* holdings: total invested and value per symbol */
int port_holdings(const Universe *u, const Portfolio *p, Holding *out, int max) {
    int n = 0, today = port_today(u);
    for (Txn *t = p->head; t; t = t->next) {
        int k = 0; while (k < n && strcmp(out[k].sym, t->sym)) k++;
        if (k == n) { if (n == max) continue; memset(&out[n], 0, sizeof out[n]); snprintf(out[n].sym, sizeof out[n].sym, "%s", t->sym); n++; }
        out[k].invested += t->amount; out[k].value += txn_value(u, t, today);
    }
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && out[j].value > out[j - 1].value; j--) { Holding x = out[j]; out[j] = out[j - 1]; out[j - 1] = x; }
    return n;
}

/* XIRR by Newton's method: the yearly rate r with sum(cf * (1+r)^(-days/365)) = 0 */
double port_xirr(const Universe *u, const Portfolio *p, bool *ok) {
    double inv, val; port_totals(u, p, &inv, &val);
    *ok = false;
    if (!p->head || inv <= 0) return 0;
    int today = port_today(u);
    long span = 0; for (Txn *t = p->head; t; t = t->next) { long d = port_days(t->date, today); if (d > span) span = d; }
    if (span < 30) return 0;                                   /* too short to annualise honestly */
    double r = 0.1;
    for (int it = 0; it < 60; it++) {
        double f = val, df = 0;
        for (Txn *t = p->head; t; t = t->next) {
            double y = port_days(t->date, today) / 365.0;
            f -= t->amount * pow(1 + r, y);
            df -= t->amount * y * pow(1 + r, y - 1);
        }
        if (fabs(df) < 1e-9) break;
        double nr = r - f / df;
        if (nr <= -0.99) nr = -0.99;
        if (fabs(nr - r) < 1e-7) { r = nr; *ok = true; break; }
        r = nr;
    }
    if (!*ok) return 0;
    return r;
}

/* value and invested amount at n evenly spaced dates up to today (for the chart) */
int port_history(const Universe *u, const Portfolio *p, double *val, double *inv, int n) {
    if (!p->head) return 0;
    int first = p->head->date; for (Txn *t = p->head; t; t = t->next) if (t->date < first) first = t->date;
    const Stock *ix = &u->s[u->idx_in];
    int a = price_index(ix, first); if (a < 0) a = 0;
    int b = ix->n - 1; if (b <= a) b = a + 1;
    for (int k = 0; k < n; k++) {
        int idx = a + (int)((long)(b - a) * k / (n - 1)); if (idx >= ix->n) idx = ix->n - 1;
        int d = ix->date[idx]; double v = 0, iv = 0;
        for (Txn *t = p->head; t; t = t->next) if (t->date <= d) { v += txn_value(u, t, d); iv += t->amount; }
        val[k] = v; inv[k] = iv;
    }
    return n;
}

bool port_save(const Portfolio *p, const char *dir, const char *file) {
    char path[400]; snprintf(path, sizeof path, "%.300s/%s", dir, file);
    FILE *f = fopen(path, "w"); if (!f) return false;
    for (Txn *t = p->head; t; t = t->next) fprintf(f, "%d|%s|%.2f|%.4f\n", t->date, t->sym, t->amount, t->price);
    fclose(f); return true;
}

void port_load(Portfolio *p, const char *dir, const char *file) {
    port_free(p);
    char path[400], line[200]; snprintf(path, sizeof path, "%.300s/%s", dir, file);
    FILE *f = fopen(path, "r"); if (!f) return;
    Txn *tail = NULL;
    while (fgets(line, sizeof line, f)) {
        Txn t = { 0 };
        if (sscanf(line, "%d|%15[^|]|%lf|%lf", &t.date, t.sym, &t.amount, &t.price) != 4) continue;
        Txn *n = malloc(sizeof *n); *n = t; n->next = NULL;
        if (tail) tail->next = n; else p->head = n;
        tail = n; p->n++;
    }
    fclose(f);
}
