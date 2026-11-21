/*
 * alerts.c - "Tell me if NVDA falls below X, or if its news turns bad."
 *
 * Alerts are checked whenever prices or news change and every time you sign
 * in. Triggered alerts go into a MAX-HEAP keyed by how serious they are, so the
 * banner always shows the most urgent one first.
 */
#include "model.h"
#include <stdio.h>
#include <string.h>

void alerts_load(AlertBook *b, const char *dir, const char *file) {
    memset(b, 0, sizeof *b);
    char p[400], line[160]; snprintf(p, sizeof p, "%.300s/%s", dir, file);
    FILE *f = fopen(p, "r"); if (!f) return;
    while (fgets(line, sizeof line, f) && b->n < MAX_ALERTS) {
        Alert *x = &b->a[b->n]; int fired = 0;
        if (sscanf(line, "%15[^|]|%d|%f|%d", x->sym, &x->kind, &x->level, &fired) == 4) { x->dismissed = fired != 0; b->n++; }
    }
    fclose(f);
}

bool alerts_save(const AlertBook *b, const char *dir, const char *file) {
    char p[400]; snprintf(p, sizeof p, "%.300s/%s", dir, file);
    FILE *f = fopen(p, "w"); if (!f) return false;
    for (int i = 0; i < b->n; i++) fprintf(f, "%s|%d|%.4f|%d\n", b->a[i].sym, b->a[i].kind, b->a[i].level, b->a[i].dismissed);
    fclose(f); return true;
}

bool alerts_add(AlertBook *b, const char *sym, int kind, float level) {
    if (b->n == MAX_ALERTS) return false;
    Alert *x = &b->a[b->n++]; memset(x, 0, sizeof *x);
    snprintf(x->sym, sizeof x->sym, "%s", sym); x->kind = kind; x->level = level;
    return true;
}

void alerts_remove(AlertBook *b, int i) { if (i < 0 || i >= b->n) return; memmove(&b->a[i], &b->a[i + 1], (size_t)(b->n - i - 1) * sizeof(Alert)); b->n--; }

/* How far past its line an alert is (0 = not triggered). */
float alert_severity(Universe *u, const Alert *x) {
    int i = uni_find(u, x->sym); if (i < 0) return 0;
    const Stock *s = &u->s[i];
    switch (x->kind) {
    case ALERT_BELOW: return s->price < x->level ? (x->level - s->price) / x->level + 0.01f : 0;
    case ALERT_ABOVE: return s->price > x->level ? (s->price - x->level) / x->level + 0.01f : 0;
    case ALERT_NEWS:  return s->ai.valid && s->ai.sentiment <= -0.2f ? -s->ai.sentiment : 0;
    }
    return 0;
}

/* Fills the heap with every triggered, not-dismissed alert. Returns how many. */
int alerts_check(Universe *u, const AlertBook *b, MaxHeap *h) {
    heap_init(h);
    for (int i = 0; i < b->n; i++) {
        if (b->a[i].dismissed) continue;
        float sv = alert_severity(u, &b->a[i]);
        if (sv > 0) heap_push(h, sv, i);
    }
    return h->n;
}

void alert_text(Universe *u, const Alert *x, char *out, int n) {
    int i = uni_find(u, x->sym); const Stock *s = i >= 0 ? &u->s[i] : NULL;
    const char *cur = s && s->mkt == MKT_US ? "$" : "\xe2\x82\xb9";
    switch (x->kind) {
    case ALERT_BELOW: snprintf(out, n, "%s fell below %s%.2f (now %s%.2f)", x->sym, cur, x->level, cur, s ? s->price : 0); break;
    case ALERT_ABOVE: snprintf(out, n, "%s rose above %s%.2f (now %s%.2f)", x->sym, cur, x->level, cur, s ? s->price : 0); break;
    default: snprintf(out, n, "The news for %s has turned bad: %s", x->sym, s && s->ai.valid ? (s->ai.headline[0] ? s->ai.headline : s->ai.summary) : ""); break;
    }
}
