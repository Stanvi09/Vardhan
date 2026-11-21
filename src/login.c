/* login.c - "Who's investing today?" Local accounts, PIN protected. */
#include "app.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

void sign_in(App *a, int i) {
    a->cur_user = i; a->logged_in = true;
    profile_set_file(a->users.u[i].file);
    port_attach(a, a->users.u[i].file);
    alerts_attach(a, a->users.u[i].file);
    memset(&a->prof, 0, sizeof a->prof);
    a->nchat = 0; a->paper_on = false; a->wi_open = false;
    a->login_pin[0] = 0; a->login_err[0] = 0; a->login_fails = 0;
    if (profile_load(&a->prof, a->u->dir) && a->prof.done) {
        profile_compute_risk(&a->prof);
        plan_rebuild(a);
        a->nav.top = 0;
        a->scr = S_HOME; a->scr_t = ui_time();
        toast(C_GAIN, "Welcome back, %s", a->prof.name);
        if (a->alert_heap.n) ui_sound(2);
    } else {                                                  /* account exists but setup was never finished */
        snprintf(a->ob_name, sizeof a->ob_name, "%s", a->users.u[i].name);
        a->ob = 1; a->scr = S_ONBOARD; a->scr_t = ui_time();
    }
}

void sign_out(App *a) {
    a->logged_in = false; a->login_sel = -1; a->login_pin[0] = 0; a->login_err[0] = 0;
    a->paper_on = false; a->wi_open = false; a->login_t = ui_time();
}

static void avatar(float cx, float cy, float r, const char *name, int k) {
    DrawCircle((int)cx, (int)cy, r, fade(pick_color(k), 0.25f));
    DrawRing((Vector2){ cx, cy }, r - 1.5f, r, 0, 360, 40, pick_color(k));
    char in[2] = { name[0] ? name[0] : '?', 0 };
    text_c(r > 24 ? F.s24 : F.s16, in, cx, cy - (r > 24 ? 15 : 10), C_TEXT);
}

/* One click: a ready-made account with a profile, a plan and six months of investments.
 * It is rebuilt fresh every time, so the demo always starts the same way. */
void demo_account(App *a) {
    Users *us = &a->users;
    int id = users_find(us, "Demo");
    if (id < 0) { id = users_add(us, "Demo", ""); if (id < 0) { toast(C_WARN, "Too many accounts on this computer"); return; } }
    snprintf(us->u[id].file, sizeof us->u[id].file, "profile_demo.txt");
    us->u[id].hash = 0;
    users_save(us, a->u->dir);
    Profile p = { 0 };
    snprintf(p.name, sizeof p.name, "Aarav"); p.age = 21; p.lump = 300000; p.sip = 20000; p.markets = 3;
    p.quiz[0] = 1; p.quiz[1] = 1; p.quiz[2] = 2; p.quiz[3] = 1;
    p.goals[0] = (Goal){ G_EDU, "Master's abroad", 600000, 3 };
    p.goals[1] = (Goal){ G_HOUSE, "House down payment", 2000000, 8 };
    p.ngoals = 2; p.done = true;
    profile_compute_risk(&p);
    profile_set_file("profile_demo.txt");
    profile_save(&p, a->u->dir);
    /* build the plan, then invest it: lump sum six months ago, plus a monthly SIP since */
    a->prof = p; plan_rebuild(a);
    port_free(&a->port);
    snprintf(a->port_file, sizeof a->port_file, "txn_profile_demo.txt");
    int start = port_months_ago(a->u, 6);
    for (int k = 0; k < a->plan->npicks; k++) { const Pick *pk = &a->plan->picks[k];
        if (p.lump * pk->weight >= 1) port_add(&a->port, a->u, start, pk->stock >= 0 ? a->u->s[pk->stock].sym : "DEPOSIT", p.lump * pk->weight); }
    const char *sip_into = "NIFTYBEES";
    for (int k = 0; k < a->plan->npicks; k++) if (a->plan->picks[k].stock >= 0 && a->u->s[a->plan->picks[k].stock].fund) { sip_into = a->u->s[a->plan->picks[k].stock].sym; break; }
    for (int m = 5; m >= 1; m--) port_add(&a->port, a->u, port_months_ago(a->u, m), sip_into, (double)p.sip);
    port_save(&a->port, a->u->dir, a->port_file);
    alerts_demo(a);
    sign_in(a, id);
    ui_sound(1);
    toast(C_GAIN, "Demo account ready: Aarav, 21, saving for a Master's and a house");
}

static void start_create(App *a) {
    a->creating = true;
    memset(&a->prof, 0, sizeof a->prof); a->prof.markets = 3;
    a->ob_name[0] = a->ob_lump[0] = a->ob_sip[0] = a->ob_gamt[0] = a->ob_pin[0] = 0;
    a->ob_age = 0; a->ob_age_s[0] = 0; a->ob_gyears_s[0] = 0;
    a->ob = 1; a->scr = S_ONBOARD; a->scr_t = ui_time(); a->logged_in = true;   /* inside the setup flow */
    a->cur_user = -1;
}

void draw_login(App *a) {
    float W = ui_w(), H = ui_h(), cx = W / 2, t = ui_time() - a->login_t;
    /* --- the logo: floats, leans towards the mouse, grows on hover, spins when clicked --- */
    float ly = H / 2 - 250, ls = 104;
    Vector2 m = GetMousePosition();
    float dx = (m.x - cx) / (W / 2), dy = (m.y - ly) / (H / 2);
    bool hl = CheckCollisionPointCircle(m, (Vector2){ cx, ly }, ls * 0.6f);
    static float sc = 1, lean = 0;
    sc += ((hl ? 1.12f : 1.0f) - sc) * 0.15f;
    lean += (dx * 10 - lean) * 0.08f;
    float st = ui_time() - a->spin_t, spin = st < 0.9f ? 360 * (1 - powf(1 - st / 0.9f, 3)) : 0;
    float bob = sinf(ui_time() * 1.6f) * 4 + dy * 3;
    float enter = 1 - powf(1 - fminf(1, t / 0.6f), 3);
    glow(cx, ly, 170 * sc, C_VIOLET, 0.22f);
    brand_mark(cx, ly + bob, ls * sc * (0.6f + 0.4f * enter), lean + spin, 1, enter);
    if (hl) { SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) a->spin_t = ui_time(); }
    text_c(F.b48, "Vardhan", cx, ly + 70, fade(C_TEXT, enter));
    text_c(F.r16, "Know the downside before you invest", cx, ly + 128, fade(C_DIM, enter));

    /* --- the card --- */
    float cw = 460, shake = 0, sk = ui_time() - a->shake_t;
    if (sk < 0.45f) shake = sinf(sk * 55) * 10 * (1 - sk / 0.45f);
    Users *us = &a->users;
    float chh = a->login_sel >= 0 ? 300 : us->n ? 140 + fminf(us->n, 4) * 62 : 220;
    Rectangle C = { cx - cw / 2 + shake, ly + 176, cw, chh };
    card(C);
    float x = C.x + 30, y = C.y + 26, w = cw - 60;
    if (a->login_sel < 0) {
        if (us->n) {
            text(F.s20, "Who's investing today?", x, y, C_TEXT); y += 44;
            for (int i = 0; i < us->n && i < 4; i++) {
                Rectangle r = { x - 8, y - 6, w + 16, 54 };
                bool h = hover(r);
                rrect(r, 12, h ? C_HOVER : C_BG2);
                avatar(x + 22, y + 21, 20, us->u[i].name, i);
                text(F.s16, us->u[i].name, x + 56, y + 11, C_TEXT);
                text_r(F.r13, us->u[i].hash ? "PIN \xe2\x86\x92" : "\xe2\x86\x92", x + w, y + 13, C_DIM);
                if (h) { SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    if (!us->u[i].hash) sign_in(a, i); else { a->login_sel = i; a->login_pin[0] = 0; a->login_err[0] = 0; } } }
                y += 62;
            }
            if (button((Rectangle){ x, y + 4, w * 0.55f - 4, 44 }, "+ Create a new account", B_SECONDARY)) start_create(a);
            if (button((Rectangle){ x + w * 0.55f + 4, y + 4, w * 0.45f - 4, 44 }, "Try the demo", B_PRIMARY)) demo_account(a);
        } else {
            text(F.s20, "Welcome to Vardhan", x, y, C_TEXT);
            text_wrap(F.r14, "Tell us your age, your money and your goals. We'll build a plan and show you the downside before you invest.", x, y + 38, w, C_DIM, 21);
            if (button((Rectangle){ x, C.y + C.height - 74, w * 0.55f - 4, 50 }, "Create my account", B_PRIMARY)) start_create(a);
            if (button((Rectangle){ x + w * 0.55f + 4, C.y + C.height - 74, w * 0.45f - 4, 50 }, "Try the demo", B_SECONDARY)) demo_account(a);
        }
    } else {
        User *u = &us->u[a->login_sel];
        avatar(cx + shake, y + 30, 30, u->name, a->login_sel);
        char hi[64]; snprintf(hi, sizeof hi, "Welcome back, %s", u->name);
        text_c(F.s20, hi, cx + shake, y + 74, C_TEXT);
        text(F.m14, "Your PIN", x, y + 116, C_DIM);
        bool locked = ui_time() < a->lock_until;
        if (!ui_focused(a->login_pin)) ui_focus(a->login_pin);
        bool enter = input((Rectangle){ x, y + 140, w, 52 }, a->login_pin, 7, "4 to 6 digits", I_PASSWORD);
        bool go_ = button((Rectangle){ x, y + 206, w, 48 }, locked ? "Please wait..." : "Sign in", a->login_pin[0] && !locked ? B_PRIMARY : B_SECONDARY);
        if ((go_ || enter) && a->login_pin[0] && !locked) {
            if (users_check(us, a->login_sel, a->login_pin)) sign_in(a, a->login_sel);
            else {
                a->login_fails++; a->login_pin[0] = 0; a->shake_t = ui_time();
                if (a->login_fails >= 5) { a->lock_until = ui_time() + 30; a->login_fails = 0; snprintf(a->login_err, sizeof a->login_err, "Too many tries. Wait 30 seconds."); }
                else snprintf(a->login_err, sizeof a->login_err, "That PIN doesn't match. %d tr%s left.", 5 - a->login_fails, 5 - a->login_fails == 1 ? "y" : "ies");
            }
        }
        if (a->login_err[0]) text_c(F.m13, a->login_err, cx + shake, C.y + C.height + 14, C_LOSS);
        if (button((Rectangle){ cx - 90, C.y + C.height + 40, 180, 30 }, "Not you? Switch account", B_LINK)) { a->login_sel = -1; a->login_err[0] = 0; }
    }
    text_c(F.r12, "Your data stays on this computer. Each account is protected by its own PIN.", cx, H - 36, C_FAINT);
}
