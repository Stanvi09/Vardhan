/* scr_ask.c - "Ask Vani": chat grounded in the user's own plan. */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Renders light markdown (lines, "- " bullets, **bold** markers removed).
 * Returns the height; draws only when `draw` is true (so bubbles can be sized first). */
float chat_rich(Font f, const char *src, float x, float y, float w, Color c, bool draw) {
    static char clean[3000];
    int k = 0;
    for (const char *p = src; *p && k < 2999; p++) {
        if (p[0] == '*' && p[1] == '*') { p++; continue; }
        if (p[0] == '#' && (p == src || p[-1] == '\n')) { while (*p == '#' || *p == ' ') p++; p--; continue; }
        clean[k++] = *p;
    }
    clean[k] = 0;
    float lh = fsz(f) * 1.5f, yy = y;
    char *line = clean;
    while (line && *line) {
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        char *t = line; while (*t == ' ') t++;
        bool bullet = (t[0] == '-' || t[0] == '*') && t[1] == ' ';
        if (!strncmp(t, "\xe2\x80\xa2", 3)) { bullet = true; t += 1; }
        if (bullet) t += 2;
        float ind = bullet ? 16 : 0;
        if (!*t) { yy += lh * 0.5f; }
        else {
            if (bullet && draw) DrawCircle((int)(x + 5), (int)(yy + lh / 2 - 1), 2.5f, c);
            /* word wrap */
            char buf[600] = "", word[200], test[800];
            const char *p = t;
            while (*p) {
                int n = 0; while (*p && *p != ' ' && n < 199) word[n++] = *p++;
                word[n] = 0; while (*p == ' ') p++;
                snprintf(test, sizeof test, "%s%s%s", buf, buf[0] ? " " : "", word);
                if (buf[0] && text_w(f, test) > w - ind) { if (draw) text(f, buf, x + ind, yy, c); yy += lh; snprintf(buf, sizeof buf, "%s", word); }
                else snprintf(buf, sizeof buf, "%s", test);
            }
            if (buf[0]) { if (draw) text(f, buf, x + ind, yy, c); yy += lh; }
        }
        line = nl ? nl + 1 : NULL;
    }
    return yy - y;
}

static void push(App *a, int who, const char *t, const char *by) {
    if (a->nchat == CHAT_MAX) { memmove(a->chat, a->chat + 1, (CHAT_MAX - 1) * sizeof(ChatMsg)); a->nchat--; }
    ChatMsg *m = &a->chat[a->nchat++];
    m->who = who; snprintf(m->text, sizeof m->text, "%s", t); snprintf(m->by, sizeof m->by, "%s", by ? by : "");
    a->chat_stick = true;
}

void chat_send(App *a, const char *q) {
    if (!q[0] || a->chat_wait) return;
    char history[1400] = ""; int k = 0;
    for (int i = a->nchat - 4 < 0 ? 0 : a->nchat - 4; i < a->nchat; i++)
        if (a->chat[i].who != 1 && a->chat[i].who != 3) k += snprintf(history + k, sizeof history - k, "%s: %.280s\n", a->chat[i].who == 0 ? "User" : "Vani", a->chat[i].text);
    push(a, 0, q, NULL);
    static char ans[2400];
    chat_offline(a->u, &a->prof, a->plan, q, ans, sizeof ans);                 /* instant, always works */
    /* questions about the money already invested are answered from the portfolio */
    bool about_port = false;
    { const char *kw[] = { "portfolio", "invested", "my investments", "how am i doing", "how is my", "profit", "returns" };
      for (size_t i = 0; i < sizeof kw / sizeof kw[0]; i++) { const char *p = q; size_t L = strlen(kw[i]);
        for (; *p; p++) { size_t j = 0; while (j < L && p[j] && (p[j] | 32) == (kw[i][j] | 32)) j++; if (j == L) { about_port = true; break; } } } }
    if (about_port) {
        double inv, val; port_totals(a->u, &a->port, &inv, &val);
        int k = 0;
        if (!a->port.n) k = snprintf(ans, sizeof ans, "You haven't recorded any investments yet. On Home, press \"Invest my plan\" or \"Add investment\", and I'll track what it's worth every day.");
        else {
            bool ok; double x = port_xirr(a->u, &a->port, &ok);
            k += snprintf(ans + k, sizeof ans - k, "You've invested %s and it's worth %s today: %s (%+.1f%%).", inr(inv), inr(val), inr_signed(val - inv), inv > 0 ? (val / inv - 1) * 100 : 0);
            if (ok) k += snprintf(ans + k, sizeof ans - k, " That's about %+.1f%% a year (XIRR).", x * 100);
            Holding hs[24]; int nh = port_holdings(a->u, &a->port, hs, 24);
            int best = -1, worst = -1;
            for (int i = 0; i < nh; i++) { double r = hs[i].value / hs[i].invested - 1;
                if (best < 0 || r > hs[best].value / hs[best].invested - 1) best = i;
                if (worst < 0 || r < hs[worst].value / hs[worst].invested - 1) worst = i; }
            #define NM(i) (strcmp(hs[i].sym, "DEPOSIT") ? hs[i].sym : "Safe deposit")
            if (best >= 0 && nh > 1) k += snprintf(ans + k, sizeof ans - k, "\n- Best: %s (%+.1f%%)\n- Weakest: %s (%+.1f%%)", NM(best), (hs[best].value / hs[best].invested - 1) * 100, NM(worst), (hs[worst].value / hs[worst].invested - 1) * 100);
            #undef NM
        }
        (void)k;
        int h = (int)strlen(history);
        snprintf(history + h, sizeof history - h, "PORTFOLIO FACTS: %.600s\n", ans);
    }
    push(a, 1, ans, "Vani  \xc2\xb7  instant answer from your numbers");
    if (a->ai.provider != AI_OFFLINE && (a->ai.provider == AI_LOCAL || a->ai.key[0])) {
        static char prompt[16000];
        chat_prompt(a->u, &a->prof, a->plan, history, q, prompt, sizeof prompt);
        if (net_chat(prompt)) { a->chat_wait = true; a->chat_sent_t = ui_time(); }
    }
}

void chat_on_result(App *a, bool ok, const char *by) {
    a->chat_wait = false;
    if (ok) {
        static char rep[6000];
        net_chat_reply(rep, sizeof rep);
        char lab[64]; snprintf(lab, sizeof lab, "Vani (%s)  \xc2\xb7  read your plan and the news", by);
        if (rep[0]) push(a, 2, rep, lab);
    } else push(a, 3, "The AI isn't reachable right now. The answer above comes straight from Vardhan's own simulations.", NULL);
}

void draw_ask(App *a, Rectangle r) {
    header("Ask Vani", "Vani is your money guide. Her answers use your goals, your plan, real prices and the latest news.", r);
    float y = r.y + 88, G = 18, pw = 330;
    Rectangle C = { r.x, y, r.width - pw - G, r.y + r.height - y };
    card(C);
    Rectangle area = { C.x + 20, C.y + 16, C.width - 40, C.height - 100 };
    float bubw = area.width * 0.72f;

    /* measure everything first */
    float total = 0, hs[CHAT_MAX];
    for (int i = 0; i < a->nchat; i++) {
        ChatMsg *m = &a->chat[i];
        float h = m->who == 3 ? 24 : chat_rich(F.r14, m->text, 0, 0, bubw - 32, C_TEXT, false) + 28 + (m->who == 1 || m->who == 2 ? 22 : 0);
        hs[i] = h; total += h + 14;
    }
    static float last_poll;
    if (a->chat_wait && ui_time() - last_poll > 0.2f) { net_chat_partial(a->chat_live, sizeof a->chat_live); last_poll = ui_time(); }
    if (!a->chat_wait) a->chat_live[0] = 0;
    float live_h = a->chat_live[0] ? chat_rich(F.r14, a->chat_live, 0, 0, bubw - 32, C_TEXT, false) + 50 : 46;
    if (a->chat_wait) total += live_h + 14;
    float maxs = total - area.height; if (maxs < 0) maxs = 0;
    if (hover(area)) { float wv = GetMouseWheelMove(); if (wv != 0) { a->chat_scroll -= wv * 60; a->chat_stick = false; } }
    if (a->chat_stick) a->chat_scroll = maxs;
    if (a->chat_scroll < 0) a->chat_scroll = 0;
    if (a->chat_scroll > maxs) a->chat_scroll = maxs;
    if (a->chat_scroll >= maxs - 2) a->chat_stick = true;

    ui_scissor((float)area.x - 6, (int)area.y, (int)area.width + 12, (int)area.height);
    float yy = area.y - a->chat_scroll;
    if (!a->nchat) {
        logo_mark(area.x + area.width / 2 - 30, area.y + area.height / 2 - 120, 60);
        text_c(F.s20, "What would you like to know?", area.x + area.width / 2, area.y + area.height / 2 - 30, C_TEXT);
        text_c(F.r14, "Try a question on the right, or type your own below.", area.x + area.width / 2, area.y + area.height / 2 + 4, C_DIM);
    }
    for (int i = 0; i < a->nchat; i++) {
        ChatMsg *m = &a->chat[i];
        if (m->who == 3) { text(F.r13, m->text, area.x + 4, yy + 4, C_FAINT); yy += hs[i] + 14; continue; }
        bool user = m->who == 0;
        float tw = chat_rich(F.r14, m->text, 0, 0, bubw - 32, C_TEXT, false);
        float bw = bubw;
        if (user) { float lw = text_w(F.r14, m->text) + 34; if (lw < bw) bw = lw; }
        Rectangle b = { user ? area.x + area.width - bw : area.x, yy, bw, hs[i] };
        if (user) rrect(b, 14, C_VIOLET);
        else { rrect(b, 14, C_CARD2); if (m->who == 2) rline(b, 14, fade(C_VIOLET, 0.5f)); }
        float ty = b.y + 14;
        if (!user) {
            DrawCircle((int)b.x + 22, (int)ty + 7, 4, m->who == 2 ? C_VIOLET : C_CYAN);
            text(F.s13, m->by, b.x + 32, ty, m->who == 2 ? C_VIOLET : C_CYAN);
            ty += 22;
        }
        chat_rich(F.r14, m->text, b.x + 16, ty, bubw - 32, user ? WHITE : C_TEXT, true);
        (void)tw;
        yy += hs[i] + 14;
    }
    if (a->chat_wait && a->chat_live[0]) {                 /* words appear as the AI writes them */
        Rectangle b = { area.x, yy, bubw, live_h };
        rrect(b, 14, C_CARD2); rline(b, 14, fade(C_VIOLET, 0.5f));
        coin(b.x + 22, b.y + 21, 8, ui_time());
        text(F.s13, "On-device AI is writing...", b.x + 36, b.y + 14, C_VIOLET);
        chat_rich(F.r14, a->chat_live, b.x + 16, b.y + 36, bubw - 32, C_TEXT, true);
    } else if (a->chat_wait) {
        Rectangle b = { area.x, yy, 360, 46 };
        rrect(b, 14, C_CARD2);
        coin(b.x + 30, b.y + 21, 12, ui_time());
        float el = ui_time() - a->chat_sent_t;
        char t[96]; snprintf(t, sizeof t, a->ai.provider == AI_LOCAL ? (el < 6 ? "On-device AI is reading your plan..." : "On-device AI is reading your plan... %.0fs") : "AI is thinking... %.0fs", el);
        text(F.r13, t, b.x + 58, b.y + 15, C_DIM);
    }
    EndScissorMode();

    /* input */
    Rectangle in = { C.x + 20, C.y + C.height - 70, C.width - 150, 50 };
    bool enter = input(in, a->chat_in, sizeof a->chat_in, "e.g. Should I put 50k in Reliance for my Master's?", I_TEXT);
    if ((button((Rectangle){ C.x + C.width - 120, in.y, 100, 50 }, "Ask", a->chat_in[0] && !a->chat_wait ? B_PRIMARY : B_SECONDARY) || enter) && a->chat_in[0] && !a->chat_wait) {
        char q[300]; snprintf(q, sizeof q, "%s", a->chat_in); a->chat_in[0] = 0;
        chat_send(a, q);
    }
    if (!ui_focused(a->chat_in) && !a->chat_in[0] && a->scr == S_ASK && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) ui_focus(a->chat_in);

    /* side panel */
    Rectangle P = { r.x + r.width - pw, y, pw, r.y + r.height - y };
    card(P);
    text(F.s16, "Try asking", P.x + 20, P.y + 18, C_TEXT);
    char q1[120], q2[120];
    snprintf(q1, sizeof q1, "Should I put 50k in Nvidia for my %s?", a->prof.ngoals ? a->prof.goals[0].name : "goal");
    snprintf(q2, sizeof q2, "Am I on track for my %s?", a->prof.ngoals > 1 ? a->prof.goals[1].name : a->prof.ngoals ? a->prof.goals[0].name : "goals");
    const char *sug[] = { q1, "How much could I lose in a bad year?", "Which stock in my plan is the riskiest?", "Why is part of my money in deposits?", q2, "Is Adani Enterprises safe for me?" };
    float sy = P.y + 50;
    for (int i = 0; i < 6; i++) {
        float h = chat_rich(F.r13, sug[i], 0, 0, P.width - 72, C_TEXT, false) + 18;
        Rectangle b = { P.x + 14, sy, P.width - 28, h };
        bool hv = hover(b);
        rrect(b, 10, hv ? C_HOVER : C_BG2); rline(b, 10, hv ? C_VIOLET : C_BORDER);
        chat_rich(F.r13, sug[i], b.x + 14, b.y + 9, P.width - 72, hv ? C_TEXT : C_DIM, true);
        if (hv) { SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) chat_send(a, sug[i]); }
        sy += h + 8;
    }
    sy += 12;
    DrawLine((int)P.x + 20, (int)sy, (int)(P.x + P.width - 20), (int)sy, C_BORDER);
    sy += 14;
    text(F.s14, "What Vani knows about you", P.x + 20, sy, C_TEXT); sy += 28;
    char l[160];
    snprintf(l, sizeof l, "%s, %d  \xc2\xb7  %s investor", a->prof.name, a->prof.age, risk_label(a->prof.risk)); text(F.r13, l, P.x + 20, sy, C_DIM); sy += 22;
    snprintf(l, sizeof l, "%s now + %s / month", inr((double)a->prof.lump), inr((double)a->prof.sip)); text(F.r13, l, P.x + 20, sy, C_DIM); sy += 22;
    for (int g = 0; g < a->prof.ngoals && sy < P.y + P.height - 60; g++) { snprintf(l, sizeof l, "%s: %s in %d yr", a->prof.goals[g].name, inr_short((double)a->prof.goals[g].target), a->prof.goals[g].years); text(F.r13, l, P.x + 20, sy, C_DIM); sy += 22; }
    static const char *pn[] = { "AI off: instant answers only", "Gemini", "Cloud AI", "On-device AI (private, offline)" };
    DrawCircle((int)P.x + 26, (int)(P.y + P.height - 28), 4, a->ai.provider ? C_GAIN : C_DIM);
    text(F.r12, pn[a->ai.provider & 3], P.x + 38, P.y + P.height - 35, C_FAINT);
    if (a->nchat && button((Rectangle){ P.x + P.width - 90, P.y + P.height - 42, 76, 28 }, "Clear", B_GHOST)) a->nchat = 0;
}

/* ---------------------------------------------------------------------------
 * The Vani dock: a small chat bubble in the corner of every page. It shares
 * the conversation with the full Ask Vani screen.
 * ------------------------------------------------------------------------- */
static Rectangle dock_panel(void) { return (Rectangle){ ui_w() - 432, ui_h() - 642, 410, 560 }; }
static Rectangle dock_bubble(void) { return (Rectangle){ ui_w() - 86, ui_h() - 84, 64, 64 }; }
Rectangle vani_dock_rect(App *a) { return a->vani_open ? dock_panel() : dock_bubble(); }

static void dock_suggestions(App *a, const char **out, char buf[2][120]) {
    switch (a->scr) {
    case S_EXPLORE: snprintf(buf[0], 120, "Is %s right for me?", a->ex >= 0 ? a->u->s[a->ex].name : "this stock"); break;
    case S_FRIEND: snprintf(buf[0], 120, "Should I put 50k in %s?", a->fr_stock >= 0 ? a->u->s[a->fr_stock].name : "Nvidia"); break;
    case S_PLAN: snprintf(buf[0], 120, "Why is part of my money in deposits?"); break;
    case S_HOME: snprintf(buf[0], 120, "How is my portfolio doing?"); break;
    case S_PROOF: snprintf(buf[0], 120, "How much could I lose in a bad year?"); break;
    default: snprintf(buf[0], 120, "Which stock in my plan is the riskiest?"); break;
    }
    snprintf(buf[1], 120, a->scr == S_PROOF ? "Which stock in my plan is the riskiest?" : "How much could I lose in a bad year?");
    out[0] = buf[0]; out[1] = buf[1];
}

void draw_vani_dock(App *a) {
    if (a->scr == S_ASK || a->paper_on || a->wi_open || a->inv_open) return;
    Rectangle B = dock_bubble();
    float t = ui_time();
    if (!a->vani_open) {
        bool h = hover(B);
        float s = h ? 1.06f : 1;
        glow(B.x + 32, B.y + 32, 70, C_VIOLET, 0.35f);
        Rectangle bb = { B.x + 32 - 32 * s, B.y + 32 - 32 * s, 64 * s, 64 * s };
        grad_card(bb, 32 / (THEME == THEME_PAPER ? 0.45f : THEME == THEME_AMETHYST ? 1.45f : 1), THEME == THEME_PAPER ? (Color){ 40, 36, 32, 255 } : (Color){ 118, 64, 238, 255 }, THEME == THEME_PAPER ? (Color){ 14, 12, 10, 255 } : (Color){ 196, 100, 250, 255 });
        /* speech bubble icon */
        DrawRectangleRounded((Rectangle){ B.x + 17, B.y + 19, 30, 22 }, 0.5f, 8, WHITE);
        DrawTriangle((Vector2){ B.x + 22, B.y + 39 }, (Vector2){ B.x + 20, B.y + 47 }, (Vector2){ B.x + 30, B.y + 39 }, WHITE);
        for (int d = 0; d < 3; d++) DrawCircle((int)(B.x + 25 + d * 7), (int)(B.y + 30 + (a->chat_wait ? sinf(t * 8 - d) * 2 : 0)), 2.2f, THEME == THEME_PAPER ? (Color){ 30, 28, 24, 255 } : (Color){ 118, 64, 238, 255 });
        if (h) {
            rrect((Rectangle){ B.x - 150, B.y + 16, 140, 32 }, 10, C_CARD2);
            text(F.m14, "Ask Vani a doubt", B.x - 140, B.y + 24, C_TEXT);
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { a->vani_open = true; a->vani_t = t; a->chat_stick = true; }
        }
        return;
    }
    Rectangle P = dock_panel();
    float e = 1 - powf(1 - fminf(1, (t - a->vani_t) / 0.22f), 3);
    P.y += (1 - e) * 30;
    DrawRectangleRounded((Rectangle){ P.x + 4, P.y + 10, P.width, P.height }, 0.06f, 12, fade(BLACK, 0.35f * e));
    card(P);
    /* header */
    Rectangle Hh = { P.x, P.y, P.width, 64 };
    grad_card(Hh, 16, THEME == THEME_PAPER ? (Color){ 40, 36, 32, 255 } : (Color){ 104, 56, 230, 255 }, THEME == THEME_PAPER ? (Color){ 14, 12, 10, 255 } : (Color){ 176, 90, 245, 255 });
    DrawRectangle((int)P.x, (int)P.y + 48, (int)P.width, 16, THEME == THEME_PAPER ? (Color){ 27, 24, 21, 255 } : (Color){ 140, 73, 238, 255 });
    DrawCircle((int)P.x + 34, (int)P.y + 32, 18, fade(WHITE, 0.2f));
    text_c(F.s16, "V", P.x + 34, P.y + 22, WHITE);
    text(F.s16, "Vani", P.x + 62, P.y + 13, WHITE);
    DrawCircle((int)P.x + 66, (int)P.y + 43, 3.5f, (Color){ 52, 211, 153, 255 });
    text(F.r12, a->ai.provider == AI_OFFLINE ? "instant answers" : "your money guide  \xc2\xb7  online", P.x + 74, P.y + 36, fade(WHITE, 0.8f));
    {   Rectangle ex = { P.x + P.width - 118, P.y + 17, 72, 28 }, cl = { P.x + P.width - 42, P.y + 17, 28, 28 };
        bool he = hover(ex), hc = hover(cl);
        rrect(ex, 14, fade(WHITE, he ? 0.28f : 0.16f)); text_c(F.s13, "Expand", ex.x + ex.width / 2, ex.y + 7, WHITE);
        rrect(cl, 14, fade(WHITE, hc ? 0.28f : 0.16f)); text_c(F.s14, "\xc3\x97", cl.x + 14, cl.y + 5, WHITE);
        if (he || hc) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        if (he && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { ui_sound(0); a->vani_open = false; go(a, S_ASK); }
        if (hc && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { ui_sound(0); a->vani_open = false; } }

    /* messages */
    Rectangle area = { P.x + 14, P.y + 76, P.width - 28, P.height - 76 - 120 };
    float bw = area.width * 0.86f;
    float total = 0, hs[CHAT_MAX];
    for (int i = 0; i < a->nchat; i++) { hs[i] = a->chat[i].who == 3 ? 20 : chat_rich(F.r13, a->chat[i].text, 0, 0, bw - 24, C_TEXT, false) + 20 + (a->chat[i].who == 1 || a->chat[i].who == 2 ? 18 : 0); total += hs[i] + 10; }
    static float last_poll;
    if (a->chat_wait && ui_time() - last_poll > 0.2f) { net_chat_partial(a->chat_live, sizeof a->chat_live); last_poll = ui_time(); }
    float live_h = a->chat_live[0] ? chat_rich(F.r13, a->chat_live, 0, 0, bw - 24, C_TEXT, false) + 38 : 40;
    if (a->chat_wait) total += live_h + 10;
    float maxs = total - area.height; if (maxs < 0) maxs = 0;
    static float scroll;
    if (hover(area)) { float wv = GetMouseWheelMove(); if (wv != 0) { scroll -= wv * 50; a->chat_stick = false; } }
    if (a->chat_stick || scroll > maxs) scroll = maxs;
    if (scroll < 0) scroll = 0;
    if (scroll >= maxs - 2) a->chat_stick = true;
    ui_scissor(area.x - 2, area.y, area.width + 4, area.height);
    float yy = area.y - scroll;
    if (!a->nchat) {
        text_wrap(F.r14, "Hi! I'm Vani. Ask me anything about your money, a stock, or a word you don't understand. I use your goals, your plan and today's news.", area.x + 6, yy + 8, area.width - 12, C_DIM, 20);
    }
    for (int i = 0; i < a->nchat; i++) {
        ChatMsg *m = &a->chat[i];
        if (m->who == 3) { text(F.r12, m->text, area.x + 4, yy + 2, C_FAINT); yy += hs[i] + 10; continue; }
        bool user = m->who == 0;
        float w = bw; if (user) { float lw = text_w(F.r13, m->text) + 26; if (lw < w) w = lw; }
        Rectangle b = { user ? area.x + area.width - w : area.x, yy, w, hs[i] };
        rrect(b, 12, user ? C_VIOLET : C_CARD2);
        float ty = b.y + 10;
        if (!user) { text(F.s13, m->who == 2 ? "Vani \xc2\xb7 AI" : "Vani", b.x + 12, ty, m->who == 2 ? C_VIOLET : C_CYAN); ty += 18; }
        chat_rich(F.r13, m->text, b.x + 12, ty, bw - 24, user ? WHITE : C_TEXT, true);
        yy += hs[i] + 10;
    }
    if (a->chat_wait) {
        Rectangle b = { area.x, yy, bw, live_h };
        rrect(b, 12, C_CARD2);
        coin(b.x + 18, b.y + 18, 8, ui_time());
        if (a->chat_live[0]) { text(F.s13, "Vani is writing...", b.x + 32, b.y + 10, C_VIOLET); chat_rich(F.r13, a->chat_live, b.x + 12, b.y + 30, bw - 24, C_TEXT, true); }
        else text(F.r13, "Vani is thinking...", b.x + 32, b.y + 11, C_DIM);
    }
    EndScissorMode();

    /* suggestions + input */
    const char *sg[2]; char sb[2][120]; dock_suggestions(a, sg, sb);
    float sy = P.y + P.height - 112, sx = P.x + 14;
    for (int k = 0; k < 2; k++) {
        float w = fminf(text_w(F.r12, sg[k]) + 22, P.width - 28);
        if (sx + w > P.x + P.width - 14) { sx = P.x + 14; sy += 30; }
        Rectangle c = { sx, sy, w, 26 };
        bool h = hover(c);
        rrect(c, 13, h ? C_HOVER : C_BG2); rline(c, 13, h ? C_VIOLET : C_BORDER);
        text(F.r12, sg[k], c.x + 11, c.y + 6, h ? C_TEXT : C_DIM);
        if (h) { SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !a->chat_wait) chat_send(a, sg[k]); }
        sx += w + 6;
    }
    Rectangle in = { P.x + 14, P.y + P.height - 58, P.width - 84, 44 };
    bool enter = input(in, a->vani_in, sizeof a->vani_in, "Type a doubt...", I_TEXT);
    if ((button((Rectangle){ P.x + P.width - 64, in.y, 50, 44 }, ">", a->vani_in[0] ? B_PRIMARY : B_SECONDARY) || enter) && a->vani_in[0] && !a->chat_wait) {
        char q[300]; snprintf(q, sizeof q, "%s", a->vani_in); a->vani_in[0] = 0; chat_send(a, q);
    }
}
