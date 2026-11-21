/*
 * app.c - Vardhan: window, main loop, sidebar and the onboarding wizard.
 */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

Color pick_color(int k) {
    static const Color c[] = { { 124, 92, 255, 255 }, { 34, 211, 238, 255 }, { 43, 212, 125, 255 }, { 255, 181, 71, 255 },
                               { 236, 72, 153, 255 }, { 79, 140, 255, 255 }, { 250, 204, 21, 255 }, { 167, 139, 250, 255 }, { 45, 212, 191, 255 } };
    /* paper: an editorial palette, like the charts in a financial newspaper */
    static const Color pp[] = { { 34, 92, 140, 255 }, { 214, 124, 36, 255 }, { 46, 125, 80, 255 }, { 150, 70, 110, 255 },
                                { 44, 140, 140, 255 }, { 176, 138, 43, 255 }, { 181, 82, 59, 255 }, { 96, 110, 130, 255 }, { 120, 150, 70, 255 } };
    static const Color am[] = { { 139, 92, 246, 255 }, { 232, 121, 249, 255 }, { 52, 211, 153, 255 }, { 251, 191, 36, 255 },
                                { 99, 102, 241, 255 }, { 192, 132, 252, 255 }, { 244, 114, 182, 255 }, { 96, 165, 250, 255 }, { 167, 139, 250, 255 } };
    return THEME == THEME_PAPER ? pp[k % 9] : THEME == THEME_AMETHYST ? am[k % 9] : c[k % 9];
}

void go(App *a, Scr s) {
    if (s == a->scr) return;
    nav_push(&a->nav, a->scr);                                   /* STACK: remember where we came from */
    a->scr = s; a->scr_t = ui_time();
    ui_unfocus();
}

void plan_rebuild(App *a) {
    uni_compute_stats(a->u);
    profile_compute_risk(&a->prof);
    advisor_build_plan(a->u, &a->prof, a->plan);
    a->plan_dirty = false;
    a->ex_out_for = -1;
    a->bt_ready = false;
}

/* ai: 0 = no news, 1 = automatic (on start), 2 = the user asked.
 * The AI reads a stock only after its fresh headlines have arrived (see apply_results).
 * On-device AI runs on the laptop's CPU, so it is never started automatically
 * for every stock: only for your own holdings, and only when you ask. */
void refresh_all(App *a, bool prices, int ai) {
    if (prices) { net_enqueue(JOB_FX, -1); for (int i = 0; i < a->u->n; i++) net_enqueue(JOB_PRICES, i); }
    if (!ai) return;
    bool local = a->ai.provider == AI_LOCAL, cloud = a->ai.provider == AI_GEMINI || a->ai.provider == AI_OPENAI;
    for (int i = 0; i < a->u->n; i++) {
        if (a->u->s[i].hidden) continue;
        bool mine = false; for (int k = 0; k < a->plan->npicks; k++) mine |= a->plan->picks[k].stock == i;
        if (cloud || (local && ai == 2 && mine)) a->want_ai[i] = 1;
        net_enqueue(JOB_NEWS, i);
    }
    if (local) net_enqueue(JOB_WARM, -1);
}

void header(const char *title, const char *sub, Rectangle r) {
    text(F.b28, title, r.x, r.y, C_TEXT);
    if (sub) text(F.r16, sub, r.x, r.y + 42, C_DIM);
}

/* ------------------------------- background ------------------------------- */
static void backdrop(void) {
    int W = ui_w(), H = ui_h();
    if (THEME == THEME_PAPER) return;                 /* paper is calm: no floating glows */
    if (THEME == THEME_AMETHYST) {                   /* a purple wash, like light behind frosted glass */
        float t2 = ui_time();
        glow(W * 0.92f, -H * 0.10f + 20 * sinf(t2 * 0.2f), 760, (Color){ 139, 92, 246, 255 }, 0.20f);
        glow(W * 0.20f, H * 1.10f, 700, (Color){ 168, 85, 247, 255 }, 0.12f);
        glow(W * 0.55f, H * 0.45f, 900, (Color){ 76, 29, 149, 255 }, 0.06f);
        return;
    }
    float t = ui_time();
    glow(W * 0.85f + 40 * sinf(t * 0.2f), H * 0.05f, 520, C_VIOLET, 0.10f);
    glow(W * 0.30f, H * 1.05f + 30 * cosf(t * 0.25f), 560, C_CYAN, 0.06f);
}

/* After onboarding: show the work the engine is doing, step by step. */
static void build_overlay(App *a) {
    float t = ui_time() - a->build_t;
    if (a->build_t == 0 || t > 2.9f) return;
    int W = ui_w(), H = ui_h();
    float out = t < 2.5f ? 1 : 1 - (t - 2.5f) / 0.4f;
    DrawRectangle(0, 0, W, H, fade(C_BG, 0.97f * out));
    if (t < 2.5f) ui_modal(true, (Rectangle){ 0, 0, 0, 0 });
    float cx = W / 2.0f, y = H / 2.0f - 250;
    coin(cx, y, 54, t * 1.6f);
    char l[96]; snprintf(l, sizeof l, "Building %s's plan", a->prof.name);
    text_c(F.b48, l, cx, y + 86, fade(C_TEXT, out));
    text_c(F.r18, "This takes a moment. Here's what Vardhan is doing for you:", cx, y + 148, fade(C_DIM, out));
    char s2[64]; int ns = 0; for (int i = 0; i < a->u->n; i++) ns += !a->u->s[i].hidden;
    snprintf(s2, sizeof s2, "Scoring %d stocks for you (max-heap)", ns);
    const char *steps[] = { "Reading your age, money and goals", s2, "Linking stocks that move together (graph + BFS)", "Simulating 3,000 possible years", "Checking the latest news" };
    float bx = cx - 290;
    for (int i = 0; i < 5; i++) {
        float st = t - 0.25f - i * 0.36f;
        if (st < 0) break;
        float e = fminf(1, st / 0.25f), yy = y + 206 + i * 52;
        bool done = st > 0.32f;
        if (done) {
            DrawCircle((int)bx, (int)(yy + 14), 14, fade(C_GAIN, out * e));
            DrawLineEx((Vector2){ bx - 6, yy + 14 }, (Vector2){ bx - 1, yy + 20 }, 3, C_CARD);
            DrawLineEx((Vector2){ bx - 1, yy + 20 }, (Vector2){ bx + 7, yy + 8 }, 3, C_CARD);
        } else DrawRing((Vector2){ bx, yy + 14 }, 10, 13, ui_time() * 400, ui_time() * 400 + 260, 24, fade(C_DIM, out));
        text(F.s24, steps[i], bx + 30, yy, fade(done ? C_TEXT : C_DIM, out * e));
    }
}


/* -------------------------------- sidebar -------------------------------- */
void nav_icon(int k, float cx, float cy, Color c) {
    switch (k) {
    case 0: DrawRing((Vector2){ cx, cy }, 5, 9, -90, 180, 20, c); DrawRing((Vector2){ cx, cy }, 5, 9, 180, 270, 10, fade(c, 0.5f)); break;
    case 1:
        DrawRectangleRounded((Rectangle){ cx - 9, cy - 8, 18, 13 }, 0.4f, 6, c);
        DrawTriangle((Vector2){ cx - 5, cy + 4 }, (Vector2){ cx - 6, cy + 9 }, (Vector2){ cx, cy + 4 }, c);
        for (int d = 0; d < 3; d++) DrawCircle((int)cx - 4 + d * 4, (int)cy - 2, 1.3f, C_BG);
        break;
    case 2: DrawRing((Vector2){ cx - 2, cy - 2 }, 5, 7.5f, 0, 360, 24, c); DrawLineEx((Vector2){ cx + 3, cy + 3 }, (Vector2){ cx + 8, cy + 8 }, 2.5f, c); break;
    case 3: DrawCircle((int)cx - 4, (int)cy - 3, 4, c); DrawCircle((int)cx + 5, (int)cy - 3, 4, fade(c, 0.6f)); DrawRectangleRounded((Rectangle){ cx - 9, cy + 3, 18, 6 }, 1, 4, c); break;
    case 4: DrawRing((Vector2){ cx, cy }, 6.5f, 9, 0, 360, 24, c); DrawLineEx((Vector2){ cx - 4, cy }, (Vector2){ cx - 1, cy + 3 }, 2, c); DrawLineEx((Vector2){ cx - 1, cy + 3 }, (Vector2){ cx + 4, cy - 3 }, 2, c); break;
    case 5: for (int i = 0; i < 3; i++) DrawRectangleRounded((Rectangle){ cx - 8 + i * 6, cy + 6 - (i + 1) * 4.0f, 4, (i + 1) * 4.0f + 2, }, 0.5f, 4, c); break;
    case 6: DrawRing((Vector2){ cx, cy }, 4, 8, 0, 360, 24, c); for (int i = 0; i < 6; i++) { float an = i * 60 * DEG2RAD; DrawCircle((int)(cx + cosf(an) * 9), (int)(cy + sinf(an) * 9), 2, c); } break;
    case 7: DrawTriangle((Vector2){ cx, cy - 10 }, (Vector2){ cx - 10, cy - 1 }, (Vector2){ cx + 10, cy - 1 }, c);           /* home */
            DrawRectangleRounded((Rectangle){ cx - 7, cy - 2, 14, 11 }, 0.25f, 4, c); DrawRectangle((int)cx - 2, (int)cy + 3, 4, 6, C_BG); break;
    case 8: DrawRectangleRounded((Rectangle){ cx - 10, cy - 7, 20, 15 }, 0.3f, 4, c); DrawRectangle((int)cx - 10, (int)cy - 3, 20, 3, C_BG);  /* newspaper-ish card */
            break;
    }
}

static void sidebar(App *a) {
    int H = ui_h();
    DrawRectangle(0, 0, 248, H, fade(C_BG2, 0.85f));
    DrawLine(248, 0, 248, H, C_BORDER);
    logo_mark(24, 24, 34);
    text(F.s20, "Vardhan", 68, 28, C_TEXT);
    text(F.r12, "see the downside first", 69, 51, C_FAINT);
    const char *items[] = { "Home", "My Plan", "Ask Vani", "Explore stocks", "Friend's tip check", "Proof it works", "Under the hood", "Settings & AI" };
    Scr sc[] = { S_HOME, S_PLAN, S_ASK, S_EXPLORE, S_FRIEND, S_PROOF, S_HOOD, S_SETTINGS };
    int ic[] = { 7, 0, 1, 2, 3, 4, 5, 6 };
    for (int i = 0; i < 8; i++) {
        Rectangle r = { 14, 96.0f + i * 46, 220, 42 };
        bool act = a->scr == sc[i], h = hover(r);
        if (act) { rrect(r, 11, fade(C_VIOLET, 0.18f)); DrawRectangleRounded((Rectangle){ r.x, r.y + 10, 3, 22 }, 1, 4, C_VIOLET); }
        else if (h) rrect(r, 11, C_CARD2);
        nav_icon(ic[i], r.x + 26, r.y + 21, act ? C_TEXT : C_DIM);
        text(act ? F.s16 : F.m16, items[i], r.x + 48, r.y + 12, act ? C_TEXT : C_DIM);
        if (h) { SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) go(a, sc[i]); }
    }
    /* data + AI status */
    int live = 0, ai = 0, tot = 0;
    for (int i = 0; i < a->u->n; i++) { if (a->u->s[i].hidden) continue; tot++; live += a->u->s[i].live; ai += a->u->s[i].ai.valid; }
    float y = H - 210;
    Rectangle st = { 14, y, 220, 92 };
    rrect(st, 12, C_CARD); rline(st, 12, C_BORDER);
    DrawCircle(30, (int)y + 22, 4, live ? C_GAIN : C_WARN);
    char b[64]; snprintf(b, sizeof b, live ? "Live prices %d/%d" : "Sample prices", live, tot);
    text(F.m13, b, 42, y + 14, C_TEXT);
    DrawCircle(30, (int)y + 46, 4, a->ai.provider ? C_GAIN : C_DIM);
    static const char *pn[] = { "AI off", "Gemini", "Cloud AI", "On-device AI" };
    snprintf(b, sizeof b, "%s  \xc2\xb7  %d/%d analysed", pn[a->ai.provider & 3], ai, tot);
    text(F.m13, b, 42, y + 38, C_TEXT);
    int pend = net_pending();
    if (pend) { spinner(32, y + 70, 6, C_CYAN); snprintf(b, sizeof b, "Updating in background (%d)", pend); text(F.r12, b, 44, y + 63, C_CYAN); }
    else if (a->last_update[0]) { snprintf(b, sizeof b, "Updated %s", a->last_update); text(F.r12, b, 24, y + 63, C_FAINT); }
    else text(F.r12, "Using saved data", 24, y + 63, C_FAINT);

    Rectangle me = { 14, H - 104.0f, 220, 88 };
    rrect(me, 14, C_CARD2);
    char ini[2] = { a->prof.name[0] ? a->prof.name[0] : 'Y', 0 };
    DrawCircle((int)me.x + 32, (int)me.y + 32, 18, fade(C_VIOLET, 0.3f));
    text_c(F.s16, ini, me.x + 32, me.y + 23, C_TEXT);
    text(F.s14, a->prof.name[0] ? a->prof.name : "You", me.x + 60, me.y + 14, C_TEXT);
    char rk[48]; snprintf(rk, sizeof rk, "%s investor", risk_label(a->prof.risk));
    text(F.r12, rk, me.x + 60, me.y + 34, C_DIM);
    if (button((Rectangle){ me.x + 8, me.y + 56, 100, 24 }, "Edit profile", B_GHOST)) { a->creating = false; a->ob = 1; go(a, S_ONBOARD); }
    if (button((Rectangle){ me.x + 112, me.y + 56, 100, 24 }, "Sign out", B_GHOST)) sign_out(a);
}

/* ------------------------------ onboarding ------------------------------ */
static const char *QUIZ[4][4] = {
    { "If your " RS "1,00,000 investment fell to " RS "80,000 in one month, you would...", "Sell everything", "Wait it out", "Invest more" },
    { "What matters most to you?", "Not losing money", "A balance of both", "Growing it as much as possible" },
    { "How much have you invested before?", "Never", "A little", "Quite a lot" },
    { "If you suddenly needed this money, could you manage without it?", "No, I'd need it", "For a few months", "Yes, easily" },
};

static void step_dots(float cx, float y, int n, int cur) {
    for (int i = 0; i < n; i++) {
        float x = cx - (n - 1) * 14 + i * 28;
        if (i == cur) rrect((Rectangle){ x - 12, y - 4, 24, 8 }, 4, C_VIOLET);
        else DrawCircle((int)x, (int)y, 4, i < cur ? C_VIOLET : C_BORDER);
    }
}

void draw_onboard(App *a) {
    int W = ui_w(), H = ui_h();
    Profile *p = &a->prof;
    float cw = 820, ch = 620, cx = (W - cw) / 2, cy = (H - ch) / 2 + 10;
    float t = ui_time() - a->scr_t;
    if (a->ob == 0) a->ob = 1;                              /* the sign-in page is the welcome now */
    if (a->ob == 0) {
        float x = W / 2.0f;
        logo_mark(x - 44, H / 2.0f - 250, 88);
        text_c(F.b64, "Vardhan", x, H / 2.0f - 140, C_TEXT);
        text_c(F.r18, "See the downside before you invest.", x, H / 2.0f - 60, C_DIM);
        const char *pts[] = { "Plans built around YOUR age, money and goals",
                              "An AI reads the news so you don't invest blindly",
                              "Shows how much you could LOSE, not just gain" };
        for (int i = 0; i < 3; i++) {
            float y = H / 2.0f - 6 + i * 38, w = text_w(F.m16, pts[i]);
            DrawCircle((int)(x - w / 2 - 18), (int)y + 9, 4, i == 2 ? C_LOSS : C_CYAN);
            text(F.m16, pts[i], x - w / 2, y, C_TEXT);
        }
        if (button((Rectangle){ x - 150, H / 2.0f + 140, 300, 54 }, "Get started", B_PRIMARY)) { a->ob = 1; a->scr_t = ui_time(); }
        text_c(F.r12, "Educational tool, not investment advice. Prices can fall as well as rise.", x, H - 40.0f, C_FAINT);
        return;
    }
    card((Rectangle){ cx, cy, cw, ch });
    step_dots(cx + cw / 2, cy + 34, 5, a->ob - 1);
    float x = cx + 60, y = cy + 70, w = cw - 120;
    bool next = false, can = true;
    char err[120] = "";
    switch (a->ob) {
    case 1:
        text(F.b28, "Tell us about you", x, y, C_TEXT);
        text(F.r16, "Your age changes how much risk makes sense: younger investors have time to recover.", x, y + 42, C_DIM);
        text(F.m14, "Your first name", x, y + 100, C_DIM);
        input((Rectangle){ x, y + 124, w, 52 }, a->ob_name, sizeof a->ob_name, "e.g. Aarav", I_TEXT);
        text(F.m14, "Your age", x, y + 206, C_DIM);
        if (!a->ob_age_s[0] && a->ob_age && !ui_focused(a->ob_age_s)) snprintf(a->ob_age_s, sizeof a->ob_age_s, "%d", a->ob_age);
        input((Rectangle){ x, y + 230, 160, 52 }, a->ob_age_s, sizeof a->ob_age_s, "e.g. 21", I_NUMBER);
        a->ob_age = atoi(a->ob_age_s);
        text(F.r14, "years", x + 176, y + 247, C_DIM);
        if (a->creating) {
            text(F.m14, "Choose a PIN (4 to 6 digits)", x + w * 0.42f, y + 206, C_DIM);
            input((Rectangle){ x + w * 0.42f, y + 230, 200, 52 }, a->ob_pin, 7, "PIN", I_PASSWORD);
            text(F.r12, "You'll use it to sign in", x + w * 0.42f + 214, y + 249, C_FAINT);
        }
        text(F.m14, "Which markets do you want to invest in?", x, y + 310, C_DIM);
        { const char *m[] = { "India (NSE)", "USA", "Both" }; int v[] = { 1, 2, 3 };
          for (int i = 0; i < 3; i++) if (chip((Rectangle){ x + i * 170, y + 336, 160, 40 }, m[i], p->markets == v[i])) p->markets = v[i]; }
        can = a->ob_name[0] != 0 && a->ob_age >= 18 && a->ob_age <= 90;
        if (!a->ob_name[0]) snprintf(err, sizeof err, "Enter your name to continue");
        else if (!can) snprintf(err, sizeof err, "Enter your age (18 or older)");
        else if (a->creating && users_find(&a->users, a->ob_name) >= 0) { can = false; snprintf(err, sizeof err, "There's already an account called %s", a->ob_name); }
        else if (a->creating && strlen(a->ob_pin) < 4) { can = false; snprintf(err, sizeof err, "Choose a PIN of 4 to 6 digits"); }
        break;
    case 2:
        text(F.b28, "How much can you invest?", x, y, C_TEXT);
        text(F.r16, "Only invest money you won't need for daily life. Keep an emergency fund aside first.", x, y + 42, C_DIM);
        text(F.m14, "One-time amount you can invest now", x, y + 100, C_DIM);
        input((Rectangle){ x, y + 124, w, 52 }, a->ob_lump, sizeof a->ob_lump, "e.g. 100000", I_MONEY);
        { const long long q[] = { 25000, 100000, 500000, 1000000 }; const char *ql[] = { RS "25K", RS "1 lakh", RS "5 lakh", RS "10 lakh" };
          for (int i = 0; i < 4; i++) if (chip((Rectangle){ x + i * 128, y + 186, 118, 34 }, ql[i], atoll(a->ob_lump) == q[i])) snprintf(a->ob_lump, sizeof a->ob_lump, "%lld", q[i]); }
        text(F.m14, "Amount you can add every month (SIP)", x, y + 250, C_DIM);
        input((Rectangle){ x, y + 274, w, 52 }, a->ob_sip, sizeof a->ob_sip, "e.g. 5000", I_MONEY);
        { const long long q[] = { 2000, 5000, 15000, 30000 };
          for (int i = 0; i < 4; i++) { char l[24]; snprintf(l, sizeof l, "%s / month", inr((double)q[i])); if (chip((Rectangle){ x + i * 150, y + 336, 140, 34 }, l, atoll(a->ob_sip) == q[i])) snprintf(a->ob_sip, sizeof a->ob_sip, "%lld", q[i]); } }
        can = atoll(a->ob_lump) > 0 || atoll(a->ob_sip) > 0;
        if (!can) snprintf(err, sizeof err, "Enter a one-time amount or a monthly amount");
        break;
    case 3: {
        text(F.b28, "What are you investing for?", x, y, C_TEXT);
        text(F.r16, "Different goals need different plans. Money for next year should not be in risky stocks.", x, y + 42, C_DIM);
        float gx = x;
        for (int k = 0; k < G_KINDS; k++) {
            float bw = (w - 5 * 10) / 6;
            Rectangle b = { gx + k * (bw + 10), y + 92, bw, 54 };
            bool on = a->ob_gkind == k, h = hover(b);
            rrect(b, 12, on ? fade(C_VIOLET, 0.22f) : h ? C_HOVER : C_CARD2); rline(b, 12, on ? C_VIOLET : C_BORDER);
            text_c(F.s13, goal_kind_name(k), b.x + b.width / 2, b.y + 20, on ? C_TEXT : C_DIM);
            if (h) { SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) a->ob_gkind = k; }
        }
        text(F.m14, "How much will you need?", x, y + 168, C_DIM);
        input((Rectangle){ x, y + 192, w * 0.48f, 48 }, a->ob_gamt, sizeof a->ob_gamt, "e.g. 2500000", I_MONEY);
        text(F.m14, "In how many years?", x + w * 0.54f, y + 168, C_DIM);
        input((Rectangle){ x + w * 0.54f, y + 192, 140, 48 }, a->ob_gyears_s, sizeof a->ob_gyears_s, "e.g. 5", I_NUMBER);
        text(F.r14, "years", x + w * 0.54f + 156, y + 207, C_DIM);
        a->ob_gyears = atoi(a->ob_gyears_s);
        if (button((Rectangle){ x, y + 256, 160, 40 }, "+ Add this goal", B_SECONDARY)) {
            if (p->ngoals >= MAX_GOALS) toast(C_WARN, "Up to %d goals", MAX_GOALS);
            else if (atoll(a->ob_gamt) <= 0) toast(C_WARN, "Enter how much you will need");
            else if (a->ob_gyears < 1 || a->ob_gyears > 40) toast(C_WARN, "Enter in how many years (1 to 40)");
            else {
                Goal *g = &p->goals[p->ngoals++];
                g->kind = a->ob_gkind; g->target = atoll(a->ob_gamt); g->years = a->ob_gyears;
                snprintf(g->name, sizeof g->name, "%s", goal_kind_name(a->ob_gkind));
                a->ob_gamt[0] = 0; a->ob_gyears_s[0] = 0;
            }
        }
        for (int i = 0; i < p->ngoals; i++) {
            Rectangle gr = { x, y + 316 + i * 52, w, 44 };
            rrect(gr, 10, C_CARD2);
            DrawCircle((int)gr.x + 20, (int)gr.y + 22, 5, pick_color(i));
            text(F.s14, p->goals[i].name, gr.x + 36, gr.y + 13, C_TEXT);
            char d[80]; snprintf(d, sizeof d, "%s in %d year%s", inr((double)p->goals[i].target), p->goals[i].years, p->goals[i].years == 1 ? "" : "s");
            text(F.m14, d, gr.x + 240, gr.y + 13, C_DIM);
            if (button((Rectangle){ gr.x + gr.width - 90, gr.y + 7, 80, 30 }, "Remove", B_GHOST)) {
                memmove(&p->goals[i], &p->goals[i + 1], (size_t)(p->ngoals - i - 1) * sizeof(Goal)); p->ngoals--; break;
            }
        }
        can = p->ngoals > 0;
        if (!can) snprintf(err, sizeof err, "Add at least one goal");
        break;
    }
    case 4: {
        text(F.b28, "How do you feel about risk?", x, y, C_TEXT);
        text(F.r16, "There are no wrong answers. Be honest: this decides how bumpy your plan will be.", x, y + 42, C_DIM);
        for (int q = 0; q < 4; q++) {
            float qy = y + 92 + q * 104;
            char qs[160]; snprintf(qs, sizeof qs, "%d.  %s", q + 1, QUIZ[q][0]);
            text(F.s16, qs, x, qy, C_TEXT);
            for (int o = 0; o < 3; o++) {
                float bw = (w - 20) / 3;
                Rectangle b = { x + o * (bw + 10), qy + 32, bw, 48 };
                bool on = p->quiz[q] == o, h = hover(b);
                rrect(b, 12, on ? fade(C_VIOLET, 0.22f) : h ? C_HOVER : C_CARD2); rline(b, 12, on ? C_VIOLET : C_BORDER);
                text_c(F.m14, QUIZ[q][o + 1], b.x + b.width / 2, b.y + 16, on ? C_TEXT : C_DIM);
                if (h) { SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) p->quiz[q] = o; }
            }
        }
        break;
    }
    case 5: {
        snprintf(p->name, sizeof p->name, "%s", a->ob_name);
        p->age = a->ob_age; p->lump = atoll(a->ob_lump); p->sip = atoll(a->ob_sip);
        profile_compute_risk(p);
        text_c(F.b28, "Your risk profile", cx + cw / 2, y, C_TEXT);
        float an = t < 1.2f ? 1 - powf(1 - t / 1.2f, 3) : 1;
        gauge(cx + cw / 2, y + 250, 170, p->risk, an);
        text_c(F.b48, risk_label(p->risk), cx + cw / 2, y + 270, C_TEXT);
        static const char *desc[] = {
            "You prefer safety. Most of your money goes to steady options, with a small part in the steadiest stocks.",
            "You want growth but sleep better without big swings. A mix of steady stocks, index funds and safe deposits.",
            "You can ride out falls for better long-term growth. More stocks, still spread across sectors.",
            "You accept big ups and downs for the highest growth. Expect years where you are down 20-30%." };
        int k = p->risk < 0.3f ? 0 : p->risk < 0.5f ? 1 : p->risk < 0.7f ? 2 : 3;
        text_wrap(F.r16, desc[k], cx + 120, y + 340, cw - 240, C_DIM, 24);
        char why[160]; snprintf(why, sizeof why, "Based on your answers, your age (%d) and when you need the money.", p->age);
        text_c(F.r13, why, cx + cw / 2, y + 410, C_FAINT);
        break;
    }
    }
    /* footer */
    float fy = cy + ch - 76;
    if (err[0] && !can) text(F.m14, err, x, fy + 14, C_WARN);
    if (a->ob > 1 && button((Rectangle){ x, fy, 120, 48 }, "Back", B_GHOST)) { a->ob--; a->scr_t = ui_time(); }
    if (a->ob == 1 && button((Rectangle){ x + w - 360, fy, 120, 48 }, "Cancel", B_GHOST)) {
        if (a->creating || !a->prof.done) { a->creating = false; sign_out(a); return; }
        a->scr = S_PLAN; a->scr_t = ui_time(); return;
    }
    const char *nl = a->ob == 5 ? "Build my plan" : "Continue";
    next = button((Rectangle){ x + w - 220, fy, 220, 48 }, nl, can ? B_PRIMARY : B_SECONDARY) && can;
    if (next) {
        if (a->ob < 5) { a->ob++; a->scr_t = ui_time(); }
        else {
            p->done = true;
            if (a->creating) {                              /* new account: name -> its own profile file */
                int id = users_add(&a->users, p->name, a->ob_pin);
                if (id >= 0) { users_save(&a->users, a->u->dir); a->cur_user = id; profile_set_file(a->users.u[id].file); port_attach(a, a->users.u[id].file); alerts_attach(a, a->users.u[id].file); }
                a->creating = false; a->ob_pin[0] = 0;
            }
            profile_save(p, a->u->dir);
            plan_rebuild(a);
            a->nav.top = 0;
            a->build_t = ui_time();
            a->scr = S_HOME; a->scr_t = ui_time() + 2.3f;     /* home animates in after the build overlay */
        }
    }
}

/* ----------------------------------- main ----------------------------------- */
static void apply_results(App *a) {
    JobResult r; bool changed = false;
    while (net_poll(&r)) {
        if (a->nlog == 12) { memmove(a->log, a->log + 1, 11 * sizeof(JobResult)); a->nlog = 11; }
        a->log[a->nlog++] = r;
        bool user = r.stock >= 0 && r.stock < a->u->n && (a->u->s[r.stock].status & 1);
        if (r.stock >= 0 && r.stock < a->u->n && r.type == JOB_AI) a->u->s[r.stock].status &= ~1;
        if (!r.ok && r.type == JOB_NEWS && r.stock >= 0) {
            a->want_ai[r.stock] = 0;
            if (user) { a->u->s[r.stock].status &= ~1; toast(C_WARN, "Couldn't fetch today's headlines for %s. Showing the saved analysis.", a->u->s[r.stock].sym); }
        }
        if (!r.ok) {
            /* never show raw failures on screen: the saved analysis simply stays */
            if (r.type == JOB_AI && user) {
                const Stock *s = &a->u->s[r.stock];
                toast(C_WARN, "Couldn't refresh %s right now. Showing the saved analysis (%s).", s->sym, s->ai.when[0] ? s->ai.when : "earlier");
            }
            if (r.type == JOB_TEST_AI) toast(C_WARN, "%s", r.msg);
            if (r.type == JOB_CHAT) chat_on_result(a, false, NULL);
            continue;
        }
        if (r.type == JOB_NEWS && r.stock >= 0 && a->want_ai[r.stock]) { a->want_ai[r.stock] = 0; net_enqueue(JOB_AI, r.stock); }
        if (r.type == JOB_PRICES) { uni_reload_prices(a->u, r.stock); a->upd_prices++; changed = true; }
        if (r.type == JOB_FX) { uni_load_cache(a->u); changed = true; }
        if (r.type == JOB_AI) {
            uni_reload_analysis(a->u, r.stock); changed = true;
            if (user) toast(C_CYAN, "%s: the AI has read today's news", a->u->s[r.stock].sym);
            else a->upd_ai++;
        }
        if (r.type == JOB_TEST_AI) toast(C_GAIN, "%s", r.msg);
        if (r.type == JOB_CHAT) chat_on_result(a, true, r.msg);
    }
    if (changed) { a->plan_dirty = true; alerts_refresh(a); }
    if (net_pending_type(JOB_PRICES) + net_pending_type(JOB_FX) + net_pending_type(JOB_NEWS) == 0) {   /* the AI lane may still be busy */
        if (a->plan_dirty && a->prof.done) plan_rebuild(a);
        if (a->upd_prices || a->upd_ai) {
            if (a->upd_ai) toast(C_GAIN, "Up to date: live prices for %d stocks, fresh AI news analysis for %d", a->upd_prices, a->upd_ai);
            else toast(C_GAIN, "Up to date: live prices for %d stocks", a->upd_prices);
            time_t t = time(NULL); strftime(a->last_update, sizeof a->last_update, "%d %b, %I:%M %p", localtime(&t));
            a->upd_prices = a->upd_ai = 0;
        }
    }
}

int main(void) {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1480, 920, "Vardhan - see the downside before you invest");
    SetWindowMinSize(1100, 680);
    if (!getenv("RL_SHOT")) MaximizeWindow();
    if (getenv("RL_SIZE")) { int w = 0, h = 0; if (sscanf(getenv("RL_SIZE"), "%dx%d", &w, &h) == 2) SetWindowSize(w, h); }
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    ui_init();
    brand_window_icon();

    App *a = calloc(1, sizeof *a);
    a->u = calloc(1, sizeof(Universe));
    a->plan = calloc(1, sizeof(Plan));
    char dir[400]; snprintf(dir, sizeof dir, "%sdata", GetApplicationDirectory());
    if (!DirectoryExists(dir)) snprintf(dir, sizeof dir, "data");
    MKDIR(dir);
    uni_init(a->u, dir);
    uni_load_cache(a->u);
    uni_load_analyses(a->u);
    uni_compute_stats(a->u);
    settings_load(&a->ai, dir);
    if (getenv("RL_THEME")) a->ai.theme = atoi(getenv("RL_THEME"));
    theme_set(a->ai.theme);
    UI_SOUND = a->ai.sound != 0;
    snprintf(a->set_key, sizeof a->set_key, "%s", a->ai.key);
    snprintf(a->set_model, sizeof a->set_model, "%s", a->ai.model);
    snprintf(a->set_url, sizeof a->set_url, "%s", a->ai.base_url);
    snprintf(a->set_local, sizeof a->set_local, "%s", a->ai.local_model);
    net_start(a->u, &a->ai);
    if (a->ai.auto_refresh && !getenv("RL_SHOT")) refresh_all(a, true, a->ai.provider != AI_OFFLINE ? 1 : 0);   /* quietly, in the background */

    a->ob_age = 21; a->ob_gyears = 5; a->prof.markets = 3; a->ex = uni_find(a->u, "RELIANCE"); a->ex_out_for = -1; a->fr_stock = -1;
    strcpy(a->ex_amt, "50000"); strcpy(a->fr_amt, "50000");
    /* accounts: an older single profile.txt becomes the first account (no PIN yet) */
    users_load(&a->users, dir);
    if (!a->users.n) {
        Profile old;
        profile_set_file("profile.txt");
        if (profile_load(&old, dir) && old.done && users_add(&a->users, old.name, "") >= 0) {
            snprintf(a->users.u[0].file, sizeof a->users.u[0].file, "profile.txt");
            users_save(&a->users, dir);
        }
    }
    a->login_sel = -1; a->cur_user = -1;
    a->intro_t0 = (float)GetTime();
    a->intro_done = getenv("RL_SHOT") && !getenv("RL_INTRO");
    if (getenv("RL_INTRO")) a->intro_t0 = (float)GetTime() - (float)atof(getenv("RL_INTRO"));
    if (getenv("RL_DEMO")) { plan_rebuild(a); demo_account(a); }
    else if (getenv("RL_SHOT") && !getenv("RL_LOGIN") && a->users.n) sign_in(a, 0);      /* screenshots skip sign-in */
    if (getenv("RL_LOGIN_SEL")) a->login_sel = atoi(getenv("RL_LOGIN_SEL"));
    if (a->logged_in) {
        snprintf(a->ob_name, sizeof a->ob_name, "%s", a->prof.name); a->ob_age = a->prof.age;
        snprintf(a->ob_lump, sizeof a->ob_lump, "%lld", a->prof.lump); snprintf(a->ob_sip, sizeof a->ob_sip, "%lld", a->prof.sip);
    }
    if (getenv("RL_INVEST") && a->logged_in && !a->port.n) {      /* screenshot hook: plan invested N months ago (not saved) */
        int d = port_months_ago(a->u, atoi(getenv("RL_INVEST")));
        for (int k = 0; k < a->plan->npicks; k++) { const Pick *pk = &a->plan->picks[k];
            port_add(&a->port, a->u, d, pk->stock >= 0 ? a->u->s[pk->stock].sym : "DEPOSIT", a->prof.lump * pk->weight); }
        port_add(&a->port, a->u, port_months_ago(a->u, 2), "NIFTYBEES", 20000);
    }
    if (getenv("RL_REPORT") && a->logged_in) a->report_req = 2;
    if (getenv("RL_VANI")) a->vani_open = true;
    if (getenv("RL_ALERTSET") && a->ex >= 0) alert_open(a, a->ex), a->al_t = -5;
    if (getenv("RL_HOMETAB")) a->home_tab = atoi(getenv("RL_HOMETAB"));
    if (getenv("RL_INVOPEN")) { a->inv_open = true; a->inv_mode = atoi(getenv("RL_INVOPEN")); a->inv_t = -5; strcpy(a->inv_amt, "50000"); strcpy(a->inv_sym, "SBIN"); a->inv_when = 6; }
    const char *sc = getenv("RL_SCREEN");
    if (sc && a->prof.done) a->scr = (Scr)atoi(sc);
    const char *ob = getenv("RL_OB");
    if (ob) { a->scr = S_ONBOARD; a->ob = atoi(ob) ? atoi(ob) : 1; a->logged_in = true; a->creating = !a->prof.done; }
    const char *fr = getenv("RL_FRIEND");
    if (fr && a->prof.done) { snprintf(a->fr_q, sizeof a->fr_q, "%s", fr); a->fr_stock = uni_find(a->u, fr); a->fr_goal = 0;
        advisor_friend_check(a->u, &a->prof, a->fr_stock, 100000, 0, &a->fr_v, &a->fr_o); a->fr_done = true; strcpy(a->fr_amt, "100000"); }
    const char *ex = getenv("RL_EX");
    if (ex) { int i = uni_find(a->u, ex); if (i >= 0) a->ex = i; }
    if (getenv("RL_WHATIF") && a->prof.done) {            /* screenshot hook: drawer open with a bigger SIP */
        a->scr = S_PLAN; a->wi_open = true; a->wi_dirty = true; a->wi_t = -1;
        a->wi_sip_k = (int)(a->prof.sip / 1000) + atoi(getenv("RL_WHATIF")); a->wi_lump_k = (int)(a->prof.lump / 1000);
        for (int g = 0; g < a->prof.ngoals; g++) { a->wi_years[g] = a->prof.goals[g].years + (g == 1); a->wi_prob[g] = a->plan->goal_prob[g]; a->wi_p50[g] = a->plan->goal_p50[g]; }
    }
    const char *ask = getenv("RL_ASK");
    if (ask && a->prof.done) { a->scr = S_ASK; chat_send(a, ask); }
    a->scr_t = (float)GetTime();

    float t0 = (float)GetTime();
    if (getenv("RL_BUILD")) a->build_t = t0 - (float)atof(getenv("RL_BUILD"));
    if (getenv("RL_PAPER")) { int i = uni_find(a->u, getenv("RL_PAPER")); if (i >= 0) { paper_open(a, i, 0); a->paper_t = getenv("RL_PAPER_T") ? t0 + (float)atof(getenv("RL_PAPER_T")) : t0 - 5; } }
    while (!WindowShouldClose()) {
        ui_begin();
        apply_results(a);
        if (IsMouseButtonPressed(MOUSE_BUTTON_SIDE) && a->scr != S_ONBOARD) { a->scr = (Scr)nav_pop(&a->nav, S_HOME); a->scr_t = ui_time(); }
        BeginDrawing();
        ClearBackground(C_BG);
        ui_cam_begin();
        backdrop();
        int W = ui_w(), H = ui_h();
        if (a->al_open) ui_modal(true, (Rectangle){ ui_w() / 2 - 260, ui_h() / 2 - 230, 520, 460 });
        bool busy_overlay = !a->intro_done || (a->build_t != 0 && ui_time() - a->build_t < 2.2f);
        if (a->paper_on || busy_overlay) ui_modal(true, (Rectangle){ 0, 0, 0, 0 });   /* overlays take all input */
        else if (!a->al_open && a->logged_in && a->prof.done && a->scr != S_ONBOARD && a->scr != S_ASK && !a->wi_open && !a->inv_open) {
            Rectangle vr = vani_dock_rect(a);                        /* the page underneath ignores the mouse over Vani */
            if (CheckCollisionPointRec(GetMousePosition(), vr)) ui_modal(true, vr);
        }
        if (!a->logged_in) draw_login(a);
        else if (a->scr == S_ONBOARD || !a->prof.done) draw_onboard(a);
        else {
            Rectangle content = { 280, 30, W - 280 - 32.0f, H - 52.0f };
            switch (a->scr) {
            case S_HOME: draw_home(a, content); break;
            case S_PLAN: draw_plan(a, content); break;
            case S_ASK: draw_ask(a, content); break;
            case S_PROOF: draw_proof(a, content); break;
            case S_EXPLORE: draw_explore(a, content); break;
            case S_FRIEND: draw_friend(a, content); break;
            case S_HOOD: draw_hood(a, content); break;
            case S_SETTINGS: draw_settings(a, content); break;
            default: a->scr = S_HOME;
            }
            sidebar(a);
            ui_modal(false, (Rectangle){ 0 });
            draw_vani_dock(a);
            build_overlay(a);
        }
        draw_alert_modal(a);
        draw_paper(a);
        if (!a->intro_done && brand_intro((float)GetTime() - a->intro_t0)) { a->intro_done = true; a->login_t = ui_time(); }
        toasts_draw();
        ui_end();
        ui_cam_end();
        EndDrawing();
        if (a->report_req && --a->report_req == 0) {            /* made between frames, then opened */
            char path[600];
            if (report_make(a, path, sizeof path)) { toast(C_GAIN, "Report saved: %s", GetFileName(path)); ui_sound(1); if (!getenv("RL_SHOT")) open_file(path); }
            else toast(C_WARN, "Couldn't save the report");
        }
        const char *shot = getenv("RL_SHOT");
        if (shot && GetTime() - t0 > (getenv("RL_WAIT") ? atof(getenv("RL_WAIT")) : 2.0)) { TakeScreenshot(shot); break; }
    }
    net_stop();
    uni_free(a->u);
    free(a->u); free(a->plan); free(a);
    ui_shutdown();
    CloseWindow();
    return 0;
}
