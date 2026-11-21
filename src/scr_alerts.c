/* scr_alerts.c - setting alerts (Explore) and the alert banner (Home). */
#include "app.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

void alerts_attach(App *a, const char *profile_file) {
    snprintf(a->alert_file, sizeof a->alert_file, "alerts_%s", profile_file);
    alerts_load(&a->alerts, a->u->dir, a->alert_file);
    alerts_refresh(a);
}
void alerts_refresh(App *a) { alerts_check(a->u, &a->alerts, &a->alert_heap); }
static void store(App *a) { if (a->alert_file[0]) alerts_save(&a->alerts, a->u->dir, a->alert_file); alerts_refresh(a); }

void alerts_demo(App *a) {
    snprintf(a->alert_file, sizeof a->alert_file, "alerts_profile_demo.txt");
    memset(&a->alerts, 0, sizeof a->alerts);
    alerts_add(&a->alerts, "NVDA", ALERT_NEWS, 0);
    int r = uni_find(a->u, "RELIANCE"); if (r >= 0) alerts_add(&a->alerts, "RELIANCE", ALERT_BELOW, floorf(a->u->s[r].price * 0.9f));
    alerts_save(&a->alerts, a->u->dir, a->alert_file);
}

void alert_open(App *a, int stock) {
    a->al_open = true; a->al_stock = stock; a->al_kind = ALERT_BELOW; a->al_t = ui_time();
    snprintf(a->al_level, sizeof a->al_level, "%.0f", floorf(a->u->s[stock].price * 0.9f));
}

/* Home: the most urgent triggered alert (top of the max-heap) */
void draw_alert_banner(App *a, Rectangle r) {
    if (a->alert_heap.n == 0) return;
    HeapItem top = a->alert_heap.a[0];                    /* peek: the root of the heap is the most serious */
    Alert *x = &a->alerts.a[top.id];
    char t[300]; alert_text(a->u, x, t, sizeof t);
    float pulse = 0.5f + 0.5f * sinf(ui_time() * 3);
    rrect(r, 12, fade(C_LOSS, 0.14f)); rline(r, 12, fade(C_LOSS, 0.5f + 0.3f * pulse));
    /* bell */
    float bx = r.x + 22, by = r.y + r.height / 2;
    DrawCircleSector((Vector2){ bx, by - 2 }, 8, 180, 360, 16, C_LOSS);
    DrawRectangle((int)bx - 8, (int)by - 2, 16, 6, C_LOSS);
    DrawCircle((int)bx, (int)by + 6, 2.5f, C_LOSS);
    char lab[340]; snprintf(lab, sizeof lab, "%s%s", a->alert_heap.n > 1 ? "" : "", t);
    float maxw = r.width - 250;
    ui_scissor(r.x + 40, r.y, maxw, r.height);
    text(F.s14, lab, r.x + 40, r.y + (r.height - fsz(F.s14)) / 2, C_TEXT);
    EndScissorMode();
    if (a->alert_heap.n > 1) { char m[32]; snprintf(m, sizeof m, "+%d more", a->alert_heap.n - 1); text(F.r12, m, r.x + r.width - 210, r.y + (r.height - 14) / 2, C_DIM); }
    int si = uni_find(a->u, x->sym);
    if (si >= 0 && button((Rectangle){ r.x + r.width - 140, r.y + 4, 64, r.height - 8 }, "View", B_LINK)) paper_open(a, si, 0);
    if (button((Rectangle){ r.x + r.width - 76, r.y + 4, 70, r.height - 8 }, "Dismiss", B_GHOST)) { x->dismissed = true; store(a); }
}

/* Explore: a small window to set an alert for one stock */
void draw_alert_modal(App *a) {
    if (!a->al_open) return;
    Stock *s = &a->u->s[a->al_stock];
    float W = ui_w(), H = ui_h(), e = 1 - powf(1 - fminf(1, (ui_time() - a->al_t) / 0.2f), 3);
    DrawRectangle(0, 0, (int)W + 1, (int)H + 1, fade(BLACK, 0.5f * e));
    Rectangle M = { W / 2 - 260, H / 2 - 230 + (1 - e) * 20, 520, 460 };
    ui_modal(true, M);
    card(M);
    float x = M.x + 28, y = M.y + 24, w = M.width - 56;
    char t[120]; snprintf(t, sizeof t, "Alert me about %s", s->sym);
    text(F.s24, t, x, y, C_TEXT);
    if (button((Rectangle){ M.x + M.width - 56, y - 2, 32, 30 }, "x", B_GHOST)) a->al_open = false;
    y += 40;
    const char *cur = s->mkt == MKT_US ? "$" : "\xe2\x82\xb9";
    snprintf(t, sizeof t, "Price now %s%.2f. The alert shows on Home the next time it's true.", cur, s->price);
    text_wrap(F.r14, t, x, y, w, C_DIM, 20); y += 46;
    const char *kn[] = { "Price falls below", "Price rises above", "News turns bad" };
    for (int k = 0; k < 3; k++) if (chip((Rectangle){ x + k * 156, y, 150, 36 }, kn[k], a->al_kind == k)) {
        a->al_kind = k;
        snprintf(a->al_level, sizeof a->al_level, "%.0f", floorf(s->price * (k == ALERT_ABOVE ? 1.1f : 0.9f)));
    }
    y += 54;
    if (a->al_kind != ALERT_NEWS) {
        snprintf(t, sizeof t, "Price (%s)", cur); text(F.m14, t, x, y, C_DIM); y += 24;
        input((Rectangle){ x, y, w, 48 }, a->al_level, sizeof a->al_level, "e.g. 1200", I_NUMBER);
        y += 62;
    } else { text_wrap(F.r14, "Triggers when Vani's reading of the latest headlines turns clearly negative.", x, y, w, C_DIM, 20); y += 52; }
    if (button((Rectangle){ x, y, w, 48 }, "Save alert", B_PRIMARY)) {
        float lv = (float)atof(a->al_level);
        if (a->al_kind != ALERT_NEWS && lv <= 0) toast(C_WARN, "Enter a price");
        else { alerts_add(&a->alerts, s->sym, a->al_kind, lv); store(a); toast(C_GAIN, "Alert saved for %s", s->sym); }
    }
    y += 64;
    /* this stock's alerts */
    text(F.m14, "Your alerts for this stock", x, y, C_DIM); y += 26;
    int shown = 0;
    for (int i = 0; i < a->alerts.n && y < M.y + M.height - 34; i++) {
        Alert *al = &a->alerts.a[i]; if (strcmp(al->sym, s->sym)) continue;
        char l[120];
        if (al->kind == ALERT_NEWS) snprintf(l, sizeof l, "News turns bad");
        else snprintf(l, sizeof l, "Price %s %s%.0f", al->kind == ALERT_BELOW ? "falls below" : "rises above", cur, al->level);
        bool on = alert_severity(a->u, al) > 0 && !al->dismissed;
        DrawCircle((int)x + 6, (int)y + 9, 4, on ? C_LOSS : C_GAIN);
        text(F.r14, l, x + 18, y, C_TEXT);
        text(F.r12, on ? "triggered" : al->dismissed ? "dismissed" : "watching", x + 230, y + 2, on ? C_LOSS : C_DIM);
        if (button((Rectangle){ x + w - 70, y - 4, 70, 26 }, "Remove", B_GHOST)) { alerts_remove(&a->alerts, i); store(a); break; }
        y += 30; shown++;
    }
    if (!shown) text(F.r13, "None yet.", x, y, C_FAINT);
    ui_modal(false, M);
}
