/* scr_settings.c - AI provider, API key, live data refresh, status. */
#include "app.h"
#include <stdio.h>
#include <string.h>

void draw_settings(App *a, Rectangle r) {
    Universe *u = a->u;
    header("Settings & AI", "Connect an AI to read the news, and download real prices. Everything is saved on this computer.", r);
    {   /* appearance */
        float ax = r.x + r.width - 390;
        if (chip((Rectangle){ ax - 220, r.y + 6, 120, 34 }, UI_SOUND ? "Sounds: on" : "Sounds: off", UI_SOUND)) { UI_SOUND = !UI_SOUND; a->ai.sound = UI_SOUND; settings_save(&a->ai, a->u->dir); if (UI_SOUND) ui_sound(1); }
        text(F.m13, "Look", ax - 46, r.y + 14, C_DIM);
        const char *tn[] = { "Paper", "Night", "Amethyst" }; int to[] = { THEME_AMETHYST, THEME_PAPER, THEME_NIGHT };
        for (int i = 0; i < 3; i++) { int t = to[i];
            if (chip((Rectangle){ ax + i * 130, r.y + 6, 122, 34 }, tn[t], THEME == t) && THEME != t) { theme_set(t); a->ai.theme = t; settings_save(&a->ai, a->u->dir); } }
    }
    float y = r.y + 88, G = 18, lw = r.width * 0.5f;
    Rectangle A = { r.x, y, lw, 520 };
    card(A);
    float x = A.x + 24, w = A.width - 48, yy = A.y + 20;
    text(F.s18, "AI that reads the news", x, yy, C_TEXT); yy += 38;
    const char *pv[] = { "On-device AI", "Google Gemini", "Other cloud AI", "Off" };
    const int pid[] = { AI_LOCAL, AI_GEMINI, AI_OPENAI, AI_OFFLINE };
    float bw = (w - 24) / 4;
    for (int i = 0; i < 4; i++) if (chip((Rectangle){ x + i * (bw + 8), yy, bw, 36 }, pv[i], a->ai.provider == pid[i])) {
        a->ai.provider = pid[i];
        if (pid[i] == AI_GEMINI && strncmp(a->set_model, "gemini", 6)) snprintf(a->set_model, sizeof a->set_model, "gemini-2.5-flash");
        if (pid[i] == AI_OPENAI && !strncmp(a->set_model, "gemini", 6)) snprintf(a->set_model, sizeof a->set_model, "gpt-4o-mini");
    }
    yy += 52;
    if (a->ai.provider == AI_LOCAL) {
        Rectangle hl = { x, yy, w, 126 };
        rrect(hl, 12, fade(C_GAIN, 0.08f)); rline(hl, 12, fade(C_GAIN, 0.35f));
        text(F.s14, "Runs on this laptop: free, no key, no limits, works offline", hl.x + 16, hl.y + 12, C_GAIN);
        text(F.r13, "One-time setup (5 minutes):", hl.x + 16, hl.y + 38, C_DIM);
        text(F.r13, "1.  Install Ollama from ollama.com and open it", hl.x + 16, hl.y + 58, C_TEXT);
        char st[120]; snprintf(st, sizeof st, "2.  In a terminal run:   ollama pull %s", a->set_local[0] ? a->set_local : "llama3.2:3b");
        text(F.r13, st, hl.x + 16, hl.y + 78, C_TEXT);
        text(F.r13, "3.  Click Save, then Test connection", hl.x + 16, hl.y + 98, C_TEXT);
        yy += 140;
        text(F.m13, "Model", x, yy, C_DIM);
        input((Rectangle){ x, yy + 20, w * 0.42f, 42 }, a->set_local, sizeof a->set_local, "llama3.2:3b", I_TEXT);
        text(F.m13, "Backup: Gemini key (optional)", x + w * 0.46f, yy, C_DIM);
        input((Rectangle){ x + w * 0.46f, yy + 20, w * 0.54f, 42 }, a->set_key, sizeof a->set_key, "used only if Ollama is closed", I_PASSWORD);
        yy += 76;
    } else if (a->ai.provider == AI_OFFLINE) {
        text_wrap(F.r13, "Vardhan will use the analyses already saved in the data folder. Each one shows the date of the news it was based on.", x, yy, w, C_DIM, 18);
        yy += 216;
    } else {
        text_wrap(F.r13, a->ai.provider == AI_GEMINI ? "Free key from aistudio.google.com -> Get API key. The free tier resets daily; refreshing every stock uses about 23 requests."
                                                     : "Any OpenAI-compatible service (OpenAI, Groq, OpenRouter...). Enter its address, model and key.", x, yy, w, C_CYAN, 18);
        yy += 44;
        text(F.m13, "API key", x, yy, C_DIM);
        input((Rectangle){ x, yy + 20, w, 42 }, a->set_key, sizeof a->set_key, "paste your key (Ctrl+V)", I_PASSWORD);
        yy += 74;
        text(F.m13, "Model", x, yy, C_DIM);
        input((Rectangle){ x, yy + 20, a->ai.provider == AI_OPENAI ? w * 0.4f : w, 42 }, a->set_model, sizeof a->set_model, "gemini-2.5-flash", I_TEXT);
        if (a->ai.provider == AI_OPENAI) { text(F.m13, "Address", x + w * 0.43f, yy, C_DIM); input((Rectangle){ x + w * 0.43f, yy + 20, w * 0.57f, 42 }, a->set_url, sizeof a->set_url, "https://api.openai.com/v1", I_TEXT); }
        yy += 98;
    }
    Rectangle cb = { x, yy, 22, 22 };
    rrect(cb, 6, a->ai.auto_refresh ? C_VIOLET : C_BG2); rline(cb, 6, a->ai.auto_refresh ? C_VIOLET : C_BORDER);
    if (a->ai.auto_refresh) text_c(F.s14, "\xe2\x9c\x93", cb.x + 11, cb.y + 2, WHITE);
    text(F.r14, "Update prices and news automatically when Vardhan opens", x + 32, yy + 2, C_TEXT);
    if (clicked((Rectangle){ x, yy, w, 24 })) a->ai.auto_refresh = !a->ai.auto_refresh;
    if (hover((Rectangle){ x, yy, w, 24 })) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    yy += 38;
    #define APPLY() do { snprintf(a->ai.key, sizeof a->ai.key, "%s", a->set_key); snprintf(a->ai.model, sizeof a->ai.model, "%s", a->set_model); \
                         snprintf(a->ai.base_url, sizeof a->ai.base_url, "%s", a->set_url); snprintf(a->ai.local_model, sizeof a->ai.local_model, "%s", a->set_local[0] ? a->set_local : "llama3.2:3b"); } while (0)
    if (button((Rectangle){ x, yy, 150, 44 }, "Save", B_PRIMARY)) { APPLY(); settings_save(&a->ai, u->dir); toast(C_GAIN, "Settings saved"); }
    if (button((Rectangle){ x + 162, yy, 170, 44 }, "Test connection", B_SECONDARY)) { APPLY(); net_enqueue(JOB_TEST_AI, -1); toast(C_CYAN, "Checking the AI..."); }
    #undef APPLY
    text(F.r12, "If the AI can't be reached (e.g. no Wi-Fi), the last saved analysis is shown. Nothing breaks.", x, A.y + A.height - 26, C_FAINT);

    /* data actions */
    Rectangle B = { r.x + lw + G, y, r.width - lw - G, 520 };
    card(B);
    x = B.x + 24; w = B.width - 48; yy = B.y + 20;
    text(F.s18, "Live data", x, yy, C_TEXT); yy += 36;
    int live = 0, tot = 0, ai = 0;
    for (int i = 0; i < u->n; i++) if (!u->s[i].hidden) { tot++; live += u->s[i].live; ai += u->s[i].ai.valid; }
    char s1[96]; snprintf(s1, sizeof s1, "%d of %d stocks have real downloaded prices", live, tot);
    text(F.m14, s1, x, yy, live ? C_GAIN : C_WARN); yy += 24;
    snprintf(s1, sizeof s1, "USD/INR rate: %.2f", u->usdinr); text(F.r13, s1, x, yy, C_DIM); yy += 34;
    if (button((Rectangle){ x, yy, w, 46 }, "Download real prices (2 years, all stocks)", B_PRIMARY)) { refresh_all(a, true, 0); toast(C_CYAN, "Downloading 2 years of prices for %d stocks...", tot); }
    yy += 58;
    if (button((Rectangle){ x, yy, w, 46 }, a->ai.provider == AI_LOCAL ? "Let the AI read today's news for my stocks" : "Let the AI read today's news for every stock", B_SECONDARY)) {
        if (a->ai.provider == AI_OFFLINE || (a->ai.provider != AI_LOCAL && !a->ai.key[0])) toast(C_WARN, "Choose an AI first, then Save");
        else { refresh_all(a, false, 2); toast(C_CYAN, a->ai.provider == AI_LOCAL ? "Reading news for your %d holdings. About a minute each on this laptop; you can keep using the app." : "Reading news for %d stocks...", a->ai.provider == AI_LOCAL ? a->plan->npicks - 1 : tot); }
    }
    yy += 64;
    int pend = net_pending();
    if (pend) {
        spinner(x + 10, yy + 10, 8, C_CYAN); char b[200];
        int pp = net_pending_type(JOB_PRICES), nn = net_pending_type(JOB_NEWS), aa = net_pending_type(JOB_AI);
        if (pp) snprintf(b, sizeof b, "Downloading prices: %d of %d done", tot + 2 - pp < 0 ? 0 : u->n - pp, u->n);
        else if (nn) snprintf(b, sizeof b, "Reading headlines: %d stocks left", nn);
        else if (aa) snprintf(b, sizeof b, "AI reading the news: %d left  \xc2\xb7  %s", aa, net_current());
        else snprintf(b, sizeof b, "%s", net_current());
        text(F.m13, b, x + 28, yy + 2, C_CYAN);
        if (pp) progress((Rectangle){ x, yy + 26, w, 6 }, (float)(u->n - pp) / u->n, C_CYAN);
    }
    else text(F.r13, "Everything is saved, so the app also works with no internet.", x, yy + 2, C_FAINT);
    yy += 34;
    text(F.m13, "Recent activity", x, yy, C_DIM); yy += 22;
    for (int i = a->nlog - 1; i >= 0 && yy < B.y + B.height - 20; i--) {
        DrawCircle((int)x + 4, (int)yy + 8, 3, a->log[i].ok ? C_GAIN : C_LOSS);
        ui_scissor((float)x + 12, (int)yy, (int)w - 12, 18);
        text(F.r12, a->log[i].msg, x + 14, yy + 1, a->log[i].ok ? C_DIM : C_LOSS);
        EndScissorMode();
        yy += 19;
    }

    /* status table */
    Rectangle T = { r.x, y + 520 + G, r.width, r.y + r.height - y - 520 - G };
    card(T);
    text(F.s16, "Data status", T.x + 22, T.y + 16, C_TEXT);
    text(F.r12, "Investor education tool, not investment advice. Past prices and AI readings of news do not guarantee future results.", T.x + 150, T.y + 19, C_FAINT);
    int cols = 3, row = 0, col = 0;
    float cw = (T.width - 44) / cols;
    for (int i = 0; i < u->n; i++) {
        if (u->s[i].hidden) continue;
        float cx = T.x + 22 + col * cw, cy = T.y + 48 + row * 22;
        if (cy > T.y + T.height - 20) break;
        Stock *s = &u->s[i];
        text(F.s13, s->sym, cx, cy, C_TEXT);
        text(F.r12, s->live ? "live" : "sample", cx + 96, cy + 1, s->live ? C_GAIN : C_WARN);
        char ab[48]; snprintf(ab, sizeof ab, "%s", s->ai.valid ? s->ai.by : "no AI yet");
        text(F.r12, ab, cx + 150, cy + 1, C_DIM);
        if (++row * 22 + 48 > T.height - 24) { row = 0; col++; if (col >= cols) break; }
    }
}
