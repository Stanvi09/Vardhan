/* scr_friend.c - "My friend told me to buy X": is it right for ME? */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void draw_friend(App *a, Rectangle r) {
    Universe *u = a->u; Profile *pr = &a->prof;
    header("Friend's tip check", "Someone told you to buy a stock? Check if it fits YOUR goals before you invest.", r);
    float y = r.y + 88;
    Rectangle Q = { r.x, y, r.width, 150 };
    card(Q);
    glow(Q.x + Q.width - 120, Q.y + 40, 200, C_PINK, 0.08f);
    text(F.m14, "Which stock did they suggest?", Q.x + 24, Q.y + 20, C_DIM);
    Rectangle sb = { Q.x + 24, Q.y + 44, 360, 50 };
    /* TRIE suggestions (computed first so a click can select) */
    int ids[6], n = 0;
    bool open = ui_focused(a->fr_q) && a->fr_q[0];
    if (open) n = trie_find(&u->trie, a->fr_q, ids, 6);
    Rectangle dd = { sb.x, sb.y + 54, sb.width, 10.0f + n * 40 };
    for (int i = 0; i < n; i++) {
        Rectangle row = { dd.x + 4, dd.y + 5 + i * 40, dd.width - 8, 38 };
        if (hover(row) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { snprintf(a->fr_q, sizeof a->fr_q, "%s", u->s[ids[i]].sym); a->fr_stock = ids[i]; ui_focus(a->fr_amt); open = false; }
    }
    bool e1 = input(sb, a->fr_q, sizeof a->fr_q, "Type a name: Nvidia, Tata, Adani...", I_TEXT);
    text(F.m14, "How much would you put in?", Q.x + 404, Q.y + 20, C_DIM);
    bool e2 = input((Rectangle){ Q.x + 404, Q.y + 44, 220, 50 }, a->fr_amt, sizeof a->fr_amt, "50000", I_MONEY);
    text(F.m14, "This money is for", Q.x + 644, Q.y + 20, C_DIM);
    float cx = Q.x + 644;
    for (int g = 0; g < pr->ngoals; g++) {
        float w = text_w(F.m13, pr->goals[g].name) + 28;
        if (cx + w > Q.x + Q.width - 200) break;
        if (chip((Rectangle){ cx, Q.y + 54, w, 32 }, pr->goals[g].name, a->fr_goal == g)) a->fr_goal = g;
        cx += w + 6;
    }
    int s = uni_find(u, a->fr_q);
    if (s < 0 && a->fr_q[0]) { int t[1]; if (trie_find(&u->trie, a->fr_q, t, 1) == 1) s = t[0]; }
    if (button((Rectangle){ Q.x + Q.width - 184, Q.y + 44, 160, 50 }, "Check it", B_PRIMARY) || e1 || e2) {
        if (s < 0 || u->s[s].hidden) toast(C_WARN, "Pick a stock from the suggestions");
        else if (atoll(a->fr_amt) <= 0) toast(C_WARN, "Enter an amount");
        else {
            a->fr_stock = s;
            advisor_friend_check(u, pr, s, atoll(a->fr_amt), a->fr_goal, &a->fr_v, &a->fr_o);
            a->fr_done = true; a->fr_t = ui_time(); ui_unfocus();
        }
    }
    text(F.r12, "We compare the stock's risk, the latest news and your goal's timeline. Not investment advice.", Q.x + 24, Q.y + 112, C_FAINT);

    if (a->fr_done && a->fr_stock >= 0) {
        Stock *st = &u->s[a->fr_stock];
        Verdict *v = &a->fr_v; Outcome *o = &a->fr_o;
        long long amt = atoll(a->fr_amt); if (amt <= 0) amt = 1;
        float t = (ui_time() - a->fr_t) / 0.5f; if (t > 1) t = 1;
        Color vc = v->verdict == 0 ? C_GAIN : v->verdict == 1 ? C_WARN : C_LOSS;
        float y2 = y + 150 + 18, lw = r.width * 0.58f;
        Rectangle V = { r.x, y2, lw, r.y + r.height - y2 };
        card(V);
        glow(V.x + 70, V.y + 70, 160, vc, 0.18f * t);
        DrawCircle((int)V.x + 66, (int)V.y + 66, 34, fade(vc, 0.18f));
        DrawRing((Vector2){ V.x + 66, V.y + 66 }, 30, 34, 0, 360 * t, 48, vc);
        const char *ic = v->verdict == 0 ? "\xe2\x9c\x93" : "!";
        text_c(F.b36, ic, V.x + 66, V.y + 46, vc);
        text(F.b28, v->headline, V.x + 120, V.y + 30, vc);
        char sub[128]; snprintf(sub, sizeof sub, "%s (%s)  \xc2\xb7  fit score %.0f/100 for a %s investor", st->name, st->sym, v->fit, risk_label(pr->risk));
        text(F.r14, sub, V.x + 120, V.y + 70, C_DIM);
        float ry = V.y + 124;
        text(F.s14, "Why", V.x + 26, ry, C_TEXT); ry += 28;
        for (int k = 0; k < v->nreasons; k++) {
            bool bad = !strncmp(v->reasons[k], "News red", 8) || !strncmp(v->reasons[k], "It typically", 12) || strstr(v->reasons[k], "more than") || strstr(v->reasons[k], "fallen") || strstr(v->reasons[k], "single company");
            bool good = !strncmp(v->reasons[k], "Positive", 8) || strstr(v->reasons[k], "within your");
            Color c = bad ? C_LOSS : good ? C_GAIN : C_DIM;
            DrawCircle((int)V.x + 32, (int)ry + 9, 4, c);
            ry += text_wrap(F.r14, v->reasons[k], V.x + 46, ry, V.width - 72, C_TEXT, 20) + 10;
        }
        if (v->nalt) {
            ry = fmaxf(ry + 6, V.y + V.height - 118);
            text(F.s14, "Better fits for you right now", V.x + 26, ry, C_TEXT);
            for (int k = 0; k < v->nalt; k++) {
                Stock *x = &u->s[v->alternatives[k]];
                float bw = (V.width - 52 - 2 * 10) / 3;
                Rectangle b = { V.x + 26 + k * (bw + 10), ry + 28, bw, 56 };
                bool h = hover(b);
                rrect(b, 12, h ? C_HOVER : C_CARD2);
                text(F.s14, x->sym, b.x + 14, b.y + 10, C_TEXT);
                char fs[48]; snprintf(fs, sizeof fs, "fit %.0f  \xc2\xb7  %s", advisor_fit(u, pr, v->alternatives[k], NULL, 0), risk_word(x->vol * (x->ai.valid ? x->ai.vol_mult : 1)));
                text(F.r12, fs, b.x + 14, b.y + 32, C_DIM);
                if (h) { SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) paper_open(a, v->alternatives[k], amt); }
            }
        }

        Rectangle O = { r.x + lw + 18, y2, r.width - lw - 18, r.y + r.height - y2 };
        card(O);
        char tl[96]; snprintf(tl, sizeof tl, "If you put %s in for a year", inr((double)amt));
        text(F.s18, tl, O.x + 22, O.y + 20, C_TEXT);
        /* range bar */
        float lo = o->p[0], hi = o->p[4], span = hi - lo; if (span < 1) span = 1;
        Rectangle bar = { O.x + 22, O.y + 96, O.width - 44, 16 };
        for (int i = 0; i < 40; i++) {
            float v0 = lo + span * i / 40;
            rrect((Rectangle){ bar.x + bar.width * i / 40, bar.y, bar.width / 40 + 1, bar.height }, 0, mix(C_LOSS, C_GAIN, (v0 - lo) / span));
        }
        float zx = bar.x + (amt - lo) / span * bar.width;
        if (zx > bar.x && zx < bar.x + bar.width) { DrawRectangle((int)zx - 1, (int)bar.y - 8, 3, (int)bar.height + 16, C_TEXT); text_c(F.r12, "you put in", zx, bar.y - 26, C_DIM); }
        float mx = bar.x + (o->p[2] - lo) / span * bar.width;
        DrawCircle((int)mx, (int)(bar.y + 8), 8, C_TEXT); DrawCircle((int)mx, (int)(bar.y + 8), 4, C_CYAN);
        text(F.n14, inr_signed(lo - amt), bar.x, bar.y + 26, C_LOSS);
        text_r(F.n14, inr_signed(hi - amt), bar.x + bar.width, bar.y + 26, C_GAIN);
        text(F.r12, "bad year", bar.x, bar.y + 46, C_FAINT); text_r(F.r12, "good year", bar.x + bar.width, bar.y + 46, C_FAINT);
        float yy = O.y + 186;
        struct { const char *l; double v; Color c; } rows[] = {
            { "Most likely result", o->p[2] - amt, C_CYAN },
            { "Average of the worst 5%", -o->cvar95, C_LOSS },
        };
        for (int k = 0; k < 2; k++) { text(F.r14, rows[k].l, O.x + 22, yy, C_DIM); text_r(F.n18, inr_signed(rows[k].v), O.x + O.width - 22, yy - 2, rows[k].c); yy += 34; }
        float c19, c08; risk_stress(u, a->fr_stock, &c19, &c08);
        text(F.r14, "If a Covid-style crash hit", O.x + 22, yy, C_DIM); text_r(F.n18, inr_signed(amt * c19), O.x + O.width - 22, yy - 2, C_LOSS); yy += 34;
        text(F.r14, "If 2008 happened again", O.x + 22, yy, C_DIM); text_r(F.n18, inr_signed(amt * c08), O.x + O.width - 22, yy - 2, C_LOSS); yy += 44;
        Rectangle pl = { O.x + 22, yy, O.width - 44, 64 };
        rrect(pl, 12, fade(o->prob_loss > 0.4f ? C_LOSS : C_WARN, 0.12f));
        char pc[32]; snprintf(pc, sizeof pc, "%.0f%%", o->prob_loss * 100);
        text(F.b36, pc, pl.x + 18, pl.y + 12, o->prob_loss > 0.4f ? C_LOSS : C_WARN);
        text_wrap(F.r14, "chance you'd have LESS money after one year", pl.x + 30 + text_w(F.b36, pc), pl.y + 14, pl.width - 60 - text_w(F.b36, pc), C_TEXT, 19);
        yy += 80;
        float hb = (O.width - 44 - 12) / 2;
        if (button((Rectangle){ O.x + 22, yy, hb, 42 }, "Why? Read the news", B_SECONDARY)) paper_open(a, a->fr_stock, amt);
        if (button((Rectangle){ O.x + 22 + hb + 12, yy, hb, 42 }, "Explain this to me", B_SECONDARY)) {
            char q[200]; snprintf(q, sizeof q, "Should I put %s in %s for my %s?", inr((double)amt), st->name, pr->ngoals ? pr->goals[a->fr_goal < pr->ngoals ? a->fr_goal : 0].name : "goals");
            go(a, S_ASK); chat_send(a, q);
        }
        if (st->ai.valid) {
            yy += 58;
            char by[120]; snprintf(by, sizeof by, "News read by %s on %s", st->ai.by, st->ai.when);
            text(F.r12, by, O.x + 22, yy, C_FAINT);
        }
    } else {
        float y2 = y + 150 + 40;
        text_c(F.s20, "Most people invest because a friend said so.", r.x + r.width / 2, y2 + 60, C_TEXT);
        text_c(F.r16, "But your friend doesn't know your age, your goals, or how much you can afford to lose.", r.x + r.width / 2, y2 + 96, C_DIM);
        const char *tries[] = { "NVDA", "ADANIENT", "RELIANCE", "TCS" };
        float tx = r.x + r.width / 2 - 2 * 130;
        text_c(F.r13, "Try one:", r.x + r.width / 2, y2 + 150, C_FAINT);
        for (int i = 0; i < 4; i++) if (chip((Rectangle){ tx + i * 130, y2 + 176, 120, 36 }, tries[i], false)) {
            snprintf(a->fr_q, sizeof a->fr_q, "%s", tries[i]);
            int si = uni_find(u, tries[i]);
            if (si >= 0) { a->fr_stock = si; if (atoll(a->fr_amt) <= 0) strcpy(a->fr_amt, "50000"); advisor_friend_check(u, pr, si, atoll(a->fr_amt), a->fr_goal, &a->fr_v, &a->fr_o); a->fr_done = true; a->fr_t = ui_time(); }
        }
    }
    /* dropdown last (on top) */
    if (open && n > 0) {
        rrect(dd, 12, C_CARD2); rline(dd, 12, C_BORDER);
        for (int i = 0; i < n; i++) {
            Rectangle row = { dd.x + 4, dd.y + 5 + i * 40, dd.width - 8, 38 };
            if (hover(row)) { rrect(row, 8, C_HOVER); SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); }
            text(F.s14, u->s[ids[i]].sym, row.x + 12, row.y + 11, C_TEXT);
            text(F.r13, u->s[ids[i]].name, row.x + 120, row.y + 12, C_DIM);
        }
    }
}
