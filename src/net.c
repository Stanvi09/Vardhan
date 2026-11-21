/* net.c - background worker: prices, news, AI. See net.h */
#include "net.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>
#ifdef _WIN32
#include <windows.h>
#endif

static Universe   *U;
static AiSettings *AI;
/* Two lanes, each a QUEUE with its own thread: downloads never wait behind the AI. */
typedef struct { JobQueue q; pthread_t th; char current[128]; int cur_type; volatile int abort; const char *name; } Lane;
static Lane L_NET = { .name = "net" }, L_AI = { .name = "ai" };
static bool is_ai_job(int t) { return t == JOB_AI || t == JOB_TEST_AI || t == JOB_CHAT || t == JOB_WARM; }
static JobResult   done[Q_CAP]; static int done_h, done_t, done_n;
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cv = PTHREAD_COND_INITIALIZER;
static bool running;
static bool fresh_news[MAX_STOCKS];
static char chat_q[16000], chat_answer[6000], chat_stream[400];         /* one chat question at a time */               /* headlines fetched this session */

/* ------------------------------- settings ------------------------------- */
void settings_load(AiSettings *s, const char *dir) {
    memset(s, 0, sizeof *s);
    s->provider = AI_LOCAL;
    s->auto_refresh = true;
    s->theme = 2;                                 /* Amethyst */
    s->sound = 1;
    snprintf(s->local_model, sizeof s->local_model, "llama3.2:3b");
    snprintf(s->model, sizeof s->model, "gemini-2.5-flash");
    snprintf(s->base_url, sizeof s->base_url, "https://api.openai.com/v1");
    char p[400], line[300]; snprintf(p, sizeof p, "%.300s/settings.txt", dir);
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *v = strchr(line, '='); if (!v) continue; *v++ = 0;
        if (!strcmp(line, "provider")) s->provider = atoi(v);
        else if (!strcmp(line, "key")) snprintf(s->key, sizeof s->key, "%s", v);
        else if (!strcmp(line, "model")) snprintf(s->model, sizeof s->model, "%s", v);
        else if (!strcmp(line, "base_url")) snprintf(s->base_url, sizeof s->base_url, "%s", v);
        else if (!strcmp(line, "local_model")) snprintf(s->local_model, sizeof s->local_model, "%s", v);
        else if (!strcmp(line, "auto_refresh")) s->auto_refresh = atoi(v) != 0;
        else if (!strcmp(line, "theme")) s->theme = atoi(v);
        else if (!strcmp(line, "sound")) s->sound = atoi(v);
    }
    fclose(f);
}
void settings_save(const AiSettings *s, const char *dir) {
    char p[400]; snprintf(p, sizeof p, "%.300s/settings.txt", dir);
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "provider=%d\nkey=%s\nmodel=%s\nbase_url=%s\nlocal_model=%s\nauto_refresh=%d\ntheme=%d\nsound=%d\n", s->provider, s->key, s->model, s->base_url, s->local_model, s->auto_refresh, s->theme, s->sound);
    fclose(f);
}

/* ------------------------------ run curl ------------------------------ *
 * Runs a command with no window, waits up to `secs`, and can be cancelled
 * (so the user's chat question can interrupt a slow background analysis). */
#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#endif
static int run_hidden(const char *cmd, int secs, volatile int *abort_flag) {
#ifdef _WIN32
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    char buf[4096]; snprintf(buf, sizeof buf, "%s", cmd);
    if (!CreateProcessA(NULL, buf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return -1;
    DWORD start = GetTickCount();
    while (WaitForSingleObject(pi.hProcess, 100) == WAIT_TIMEOUT) {
        if ((abort_flag && *abort_flag) || GetTickCount() - start > (DWORD)(secs + 5) * 1000) { TerminateProcess(pi.hProcess, 1); break; }
    }
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return (int)code;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int dn = open("/dev/null", 1); if (dn >= 0) { dup2(dn, 1); dup2(dn, 2); }
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL); _exit(127);
    }
    time_t start = time(NULL); int st = 1;
    for (;;) {
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) break;
        if ((abort_flag && *abort_flag) || time(NULL) - start > secs + 5) { kill(pid, SIGKILL); waitpid(pid, &st, 0); break; }
        struct timespec ts = { 0, 100 * 1000000L }; nanosleep(&ts, NULL);
    }
    return st;
#endif
}

static char *read_file(const char *p, long *len) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    long r = (long)fread(b, 1, (size_t)n, f); b[r] = 0; fclose(f);
    if (len) *len = r;
    return b;
}

#ifdef _WIN32
#define CURL "curl.exe"
#else
#define CURL "curl"
#endif

/* GET url -> file. Returns file contents (malloc) or NULL. */
#define UA "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36"
static char *http_get(const char *url, const char *out) {
    char cmd[1600];
    snprintf(cmd, sizeof cmd, CURL " -s -L -m 25 --connect-timeout 8 -A \"" UA "\" -o \"%s\" \"%s\"", out, url);
    remove(out);
    run_hidden(cmd, 25, NULL);
    return read_file(out, NULL);
}

/* POST json body (file) with headers (file) -> response file */
static char *http_post(const char *url, const char *hdr_file, const char *body_file, const char *out, int timeout) {
    char cmd[1600];
    snprintf(cmd, sizeof cmd, CURL " -s -N -m %d -X POST -H @\"%s\" --data-binary @\"%s\" -o \"%s\" \"%s\"", timeout, hdr_file, body_file, out, url);
    remove(out);
    run_hidden(cmd, timeout, &L_AI.abort);
    return read_file(out, NULL);
}

static void tmp(char *out, int n, const char *name) { snprintf(out, n, "%.300s/tmp_%s", U->dir, name); }

/* -------------------------------- prices -------------------------------- */
bool net_parse_yahoo(const char *json, int *dates, float *closes, int *n, int max) {
    cJSON *root = cJSON_Parse(json);
    *n = 0;
    if (!root) return false;
    cJSON *res = cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "chart"), "result"), 0);
    cJSON *ts = cJSON_GetObjectItem(res, "timestamp");
    cJSON *q = cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(res, "indicators"), "quote"), 0);
    cJSON *cl = cJSON_GetObjectItem(q, "close");
    int cnt = cJSON_GetArraySize(ts);
    int start = cnt > max ? cnt - max : 0;
    for (int i = start; i < cnt; i++) {
        cJSON *t = cJSON_GetArrayItem(ts, i), *c = cJSON_GetArrayItem(cl, i);
        if (!cJSON_IsNumber(t) || !cJSON_IsNumber(c) || c->valuedouble <= 0) continue;   /* skip null days */
        time_t tt = (time_t)t->valuedouble; struct tm *g = gmtime(&tt);
        dates[*n] = (g->tm_year + 1900) * 10000 + (g->tm_mon + 1) * 100 + g->tm_mday;
        closes[*n] = (float)c->valuedouble;
        (*n)++;
    }
    cJSON_Delete(root);
    return *n >= 30;
}

static void today_str(char *b, int n) { time_t t = time(NULL); strftime(b, n, "%d %b %Y %H:%M", localtime(&t)); }

static bool job_prices(int i, char *msg) {
    Stock *s = &U->s[i];
    char url[300], out[400];
    char enc[40]; int k = 0;
    for (const char *p = s->yahoo; *p && k < 36; p++) { if (*p == '^') { enc[k++] = '%'; enc[k++] = '5'; enc[k++] = 'E'; } else enc[k++] = *p; }
    enc[k] = 0;
    snprintf(url, sizeof url, "https://query1.finance.yahoo.com/v8/finance/chart/%s?range=2y&interval=1d", enc);
    tmp(out, sizeof out, "prices.json");
    char *body = http_get(url, out);
    if (!body || !strstr(body, "\"timestamp\"")) {                 /* busy? try Yahoo's second server */
        free(body);
        snprintf(url, sizeof url, "https://query2.finance.yahoo.com/v8/finance/chart/%s?range=2y&interval=1d", enc);
        body = http_get(url, out);
    }
    if (!body) { snprintf(msg, 200, "%s: no internet / download failed", s->sym); return false; }
    static int dates[MAX_DAYS]; static float closes[MAX_DAYS]; int n = 0;
    bool ok = net_parse_yahoo(body, dates, closes, &n, MAX_DAYS);
    free(body);
    if (!ok) { snprintf(msg, 200, "%s: price data not available", s->sym); return false; }
    char p[400]; snprintf(p, sizeof p, "%.300s/prices/%s.csv", U->dir, s->sym);
    FILE *f = fopen(p, "w");
    if (!f) { snprintf(msg, 200, "cannot write %s", p); return false; }
    char ts[40]; today_str(ts, sizeof ts);
    fprintf(f, "#live,%s\n", ts);
    for (int d = 0; d < n; d++) fprintf(f, "%d,%.4f\n", dates[d], closes[d]);
    fclose(f);
    snprintf(msg, 200, "%s: %d days of real prices", s->sym, n);
    return true;
}

static bool job_fx(char *msg) {
    char out[400]; tmp(out, sizeof out, "fx.json");
    char *body = http_get("https://query1.finance.yahoo.com/v8/finance/chart/INR=X?range=5d&interval=1d", out);
    if (!body) { snprintf(msg, 200, "USD/INR: download failed"); return false; }
    static int d[MAX_DAYS]; static float c[MAX_DAYS]; int n = 0;
    net_parse_yahoo(body, d, c, &n, MAX_DAYS);
    free(body);
    if (n < 1) { snprintf(msg, 200, "USD/INR: not available"); return false; }
    char p[400]; snprintf(p, sizeof p, "%.300s/fx.txt", U->dir);
    FILE *f = fopen(p, "w"); if (f) { fprintf(f, "%.3f\n", c[n - 1]); fclose(f); }
    snprintf(msg, 200, "USD/INR = %.2f", c[n - 1]);
    return true;
}

/* --------------------------------- news --------------------------------- */
static void unescape(char *s) {
    const struct { const char *e; char c; } map[] = { {"&amp;", '&'}, {"&quot;", '"'}, {"&#39;", '\''}, {"&apos;", '\''}, {"&lt;", '<'}, {"&gt;", '>'}, {"&nbsp;", ' '} };
    for (char *p = s; *p; p++) {
        if (*p != '&') continue;
        for (size_t k = 0; k < sizeof map / sizeof map[0]; k++) {
            size_t L = strlen(map[k].e);
            if (!strncmp(p, map[k].e, L)) { *p = map[k].c; memmove(p + 1, p + L, strlen(p + L) + 1); break; }
        }
    }
    for (char *p = s; *p; p++) if (*p == '|' || *p == '\n' || *p == '\r') *p = ' ';
}

static const char *tag(const char *from, const char *end, const char *open, const char *close, char *out, int n) {
    const char *a = strstr(from, open);
    if (!a || a > end) return NULL;
    a = strchr(a, '>'); if (!a) return NULL; a++;
    const char *b = strstr(a, close);
    if (!b || b > end) return NULL;
    if (!strncmp(a, "<![CDATA[", 9)) { a += 9; const char *c = strstr(a, "]]>"); if (c && c < b) b = c; }
    int L = (int)(b - a); if (L >= n) L = n - 1;
    memcpy(out, a, (size_t)L); out[L] = 0;
    return b;
}

int net_parse_rss(const char *xml, char titles[][220], char sources[][60], char dates[][40], int max) {
    int n = 0;
    const char *p = xml;
    while (n < max && (p = strstr(p, "<item>"))) {
        const char *end = strstr(p, "</item>"); if (!end) break;
        char t[400] = "", s[100] = "", d[80] = "";
        tag(p, end, "<title", "</title>", t, sizeof t);
        tag(p, end, "<source", "</source>", s, sizeof s);
        tag(p, end, "<pubDate", "</pubDate>", d, sizeof d);
        unescape(t); unescape(s);
        size_t sl = strlen(s), tl = strlen(t);                   /* Google appends " - Source" */
        if (sl && tl > sl + 3 && !strcmp(t + tl - sl, s) && t[tl - sl - 2] == '-') t[tl - sl - 3] = 0;
        char shortd[40] = "";
        if (strlen(d) >= 16) snprintf(shortd, sizeof shortd, "%.6s", d + 5);   /* "21 Nov" */
        snprintf(titles[n], 220, "%s", t);
        snprintf(sources[n], 60, "%s", s[0] ? s : "Google News");
        snprintf(dates[n], 40, "%s", shortd);
        if (t[0]) n++;
        p = end;
    }
    return n;
}

static bool job_news(int i, char *msg) {
    Stock *s = &U->s[i];
    char q[200], url[400], out[400];
    int k = 0;
    const char *name = s->fund ? (s->mkt == MKT_IN ? "Nifty 50 Sensex market" : "S&P 500 stock market") : s->name;
    for (const char *p = name; *p && k < 150; p++) { if (*p == ' ') { q[k++] = '+'; } else if (*p == '&') { memcpy(q + k, "%26", 3); k += 3; } else q[k++] = *p; }
    q[k] = 0;
    snprintf(url, sizeof url, "https://news.google.com/rss/search?q=%s+stock+when:14d&hl=%s", q, s->mkt == MKT_IN ? "en-IN&gl=IN&ceid=IN:en" : "en-US&gl=US&ceid=US:en");
    tmp(out, sizeof out, "news.xml");
    char *xml = http_get(url, out);
    if (!xml) { snprintf(msg, 200, "%s: news download failed", s->sym); return false; }
    static char T[12][220], S[12][60], D[12][40];
    int n = net_parse_rss(xml, T, S, D, 12);
    free(xml);
    char p[400]; snprintf(p, sizeof p, "%.300s/news_%s.txt", U->dir, s->sym);
    FILE *f = fopen(p, "w");
    if (f) { for (int j = 0; j < n; j++) fprintf(f, "%s|%s|%s\n", D[j], S[j], T[j]); fclose(f); }
    snprintf(msg, 200, "%s: %d headlines", s->sym, n);
    if (n > 0) fresh_news[i] = true;
    return n > 0;
}

/* ---------------------------------- AI ---------------------------------- */
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

bool net_parse_analysis_json(const char *json, Analysis *a) {
    const char *st = strchr(json, '{'), *en = strrchr(json, '}');      /* tolerate ```json fences */
    if (!st || !en || en < st) return false;
    char *buf = malloc((size_t)(en - st) + 2);
    memcpy(buf, st, (size_t)(en - st) + 1); buf[en - st + 1] = 0;
    cJSON *r = cJSON_Parse(buf);
    free(buf);
    if (!r) return false;
    memset(a, 0, sizeof *a);
    cJSON *x;
    if ((x = cJSON_GetObjectItem(r, "summary")) && cJSON_IsString(x)) snprintf(a->summary, sizeof a->summary, "%s", x->valuestring);
    if ((x = cJSON_GetObjectItem(r, "headline")) && cJSON_IsString(x)) snprintf(a->headline, sizeof a->headline, "%s", x->valuestring);
    a->sentiment  = clampf((x = cJSON_GetObjectItem(r, "sentiment"))  && cJSON_IsNumber(x) ? (float)x->valuedouble : 0, -1, 1);
    a->vol_mult   = clampf((x = cJSON_GetObjectItem(r, "vol_mult"))   && cJSON_IsNumber(x) ? (float)x->valuedouble : 1, 0.7f, 2.0f);
    a->drift_tilt = clampf((x = cJSON_GetObjectItem(r, "drift_tilt")) && cJSON_IsNumber(x) ? (float)x->valuedouble : 0, -0.20f, 0.10f);
    a->event_prob = clampf((x = cJSON_GetObjectItem(r, "event_prob")) && cJSON_IsNumber(x) ? (float)x->valuedouble : 0, 0, 0.35f);
    a->event_size = clampf((x = cJSON_GetObjectItem(r, "event_size")) && cJSON_IsNumber(x) ? (float)x->valuedouble : 0, -0.6f, 0);
    cJSON *fs = cJSON_GetObjectItem(r, "factors"), *f;
    cJSON_ArrayForEach(f, fs) {
        if (a->nf >= MAX_FACTORS) break;
        Factor *t = &a->f[a->nf];
        cJSON *c = cJSON_GetObjectItem(f, "category"), *im = cJSON_GetObjectItem(f, "impact"), *tx = cJSON_GetObjectItem(f, "text");
        if (!cJSON_IsString(tx)) continue;
        snprintf(t->category, sizeof t->category, "%s", cJSON_IsString(c) ? c->valuestring : "News");
        t->impact = cJSON_IsNumber(im) ? (int)clampf((float)im->valuedouble, -3, 3) : 0;
        snprintf(t->text, sizeof t->text, "%s", tx->valuestring);
        for (char *p = t->text; *p; p++) if (*p == '|' || *p == '\n') *p = ' ';
        a->nf++;
    }
    for (char *p = a->summary; *p; p++) if (*p == '\n') *p = ' ';
    cJSON_Delete(r);
    a->valid = a->nf > 0 || a->summary[0];
    return a->valid;
}

static void build_prompt(int i, char *out, int n) {
    Stock *s = &U->s[i];
    char news[5000] = ""; int k = 0;
    char p[400], line[400]; snprintf(p, sizeof p, "%.300s/news_%s.txt", U->dir, s->sym);
    FILE *f = fopen(p, "r");
    if (f) { while (fgets(line, sizeof line, f) && k < 4500) k += snprintf(news + k, sizeof news - k, "- %s", line); fclose(f); }
    time_t t = time(NULL); char today[40]; strftime(today, sizeof today, "%d %B %Y", localtime(&t));
    snprintf(out, n,
        "You are a careful risk analyst helping first-time investors. Today is %s.\n"
        "Company: %s (%s), sector: %s, market: %s.\n"
        "Price history: yearly volatility %.0f%%, 1-year return %.0f%%, biggest fall from a peak %.0f%%.\n"
        "Recent news headlines (newest first):\n%s\n"
        "Using ONLY these headlines and general knowledge of how such events affect share prices "
        "(leadership changes, losses, wars, sanctions, regulation, interest rates), assess the RISK for "
        "the next 12 months. Do not predict a price. Be conservative and write in plain English a beginner understands.\n"
        "Reply with ONLY this JSON:\n"
        "{\"headline\": \"a newspaper headline of at most 11 words about the single most important news item for this company, specific and factual\", "
        "\"summary\": \"2 sentences\", \"sentiment\": number -1..1, "
        "\"vol_mult\": number 0.7..2.0 (how much jumpier than its history the news makes it), "
        "\"drift_tilt\": number -0.20..0.10 (change to expected yearly return), "
        "\"event_prob\": number 0..0.35 (chance of a sudden big drop within a year), "
        "\"event_size\": number -0.6..0 (size of that drop), "
        "\"factors\": [{\"category\": \"Leadership|Earnings|Geopolitics|Regulation|Macro|Sector|Operations|Governance\", "
        "\"impact\": integer -3..3, \"text\": \"one plain sentence citing the headline\"}] (2 to 5 factors)}",
        today, s->name, s->sym, s->sector, s->mkt == MKT_IN ? "India (NSE)" : "USA",
        s->vol * 100, s->ret1y * 100, s->maxdd * 100, news[0] ? news : "(no recent headlines found)\n");
}

/* One request to one provider. */
static bool parse_stream(const char *nd, char *reply, int n);
static bool call_ai(int provider, const char *prompt, char *reply, int n, char *msg, bool json, bool stream) {
    char hdr[400], body[400], out[400];
    tmp(hdr, sizeof hdr, "hdr.txt"); tmp(body, sizeof body, "body.json"); tmp(out, sizeof out, "reply.json");
    if (stream && provider == AI_LOCAL) { tmp(out, sizeof out, "chat_stream.ndjson"); pthread_mutex_lock(&mx); snprintf(chat_stream, sizeof chat_stream, "%s", out); pthread_mutex_unlock(&mx); }
    FILE *h = fopen(hdr, "w"); if (!h) return false;
    cJSON *root = cJSON_CreateObject();
    char url[400]; int timeout = 90;
    if (provider == AI_GEMINI) {
        fprintf(h, "Content-Type: application/json\nx-goog-api-key: %s\n", AI->key);
        cJSON *contents = cJSON_AddArrayToObject(root, "contents"), *c = cJSON_CreateObject();
        cJSON *parts = cJSON_AddArrayToObject(c, "parts"), *pt = cJSON_CreateObject();
        cJSON_AddStringToObject(pt, "text", prompt); cJSON_AddItemToArray(parts, pt); cJSON_AddItemToArray(contents, c);
        cJSON *gc = cJSON_AddObjectToObject(root, "generationConfig");
        if (json) cJSON_AddStringToObject(gc, "responseMimeType", "application/json");
        cJSON_AddNumberToObject(gc, "temperature", 0.2);
        snprintf(url, sizeof url, "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent", AI->model);
    } else {
        bool local = provider == AI_LOCAL;
        if (local) fprintf(h, "Content-Type: application/json\n");
        else fprintf(h, "Content-Type: application/json\nAuthorization: Bearer %s\n", AI->key);
        cJSON_AddStringToObject(root, "model", local ? AI->local_model : AI->model);
        cJSON *msgs = cJSON_AddArrayToObject(root, "messages"), *m = cJSON_CreateObject();
        cJSON_AddStringToObject(m, "role", "user"); cJSON_AddStringToObject(m, "content", prompt); cJSON_AddItemToArray(msgs, m);
        if (local) {
            /* Ollama's own API: keep the model in memory, cap the answer length, stream words as they come */
            cJSON_AddBoolToObject(root, "stream", stream);
            cJSON_AddStringToObject(root, "keep_alive", "30m");
            if (json) cJSON_AddStringToObject(root, "format", "json");
            cJSON *op = cJSON_AddObjectToObject(root, "options");
            cJSON_AddNumberToObject(op, "temperature", 0.2);
            cJSON_AddNumberToObject(op, "num_predict", json ? 600 : 220);
            cJSON_AddNumberToObject(op, "num_ctx", 4096);
            snprintf(url, sizeof url, "http://localhost:11434/api/chat"); timeout = 300;
        } else {
            cJSON_AddNumberToObject(root, "temperature", 0.2);
            cJSON_AddBoolToObject(root, "stream", false);
            if (json) { cJSON *rf = cJSON_AddObjectToObject(root, "response_format"); cJSON_AddStringToObject(rf, "type", "json_object"); }
            snprintf(url, sizeof url, "%s/chat/completions", AI->base_url);
        }
    }
    fclose(h);
    char *js = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    FILE *b = fopen(body, "w"); if (b) { fputs(js, b); fclose(b); }
    free(js);
    char *resp = http_post(url, hdr, body, out, timeout);
    remove(hdr);                                              /* never leave a key in a temp file */
    if (L_AI.abort) { free(resp); snprintf(msg, 200, "interrupted"); return false; }
    if (!resp || !resp[0]) { free(resp); snprintf(msg, 200, provider == AI_LOCAL ? "On-device AI not running (open Ollama)" : "AI could not be reached"); return false; }
    if (stream && provider == AI_LOCAL) {
        bool ok = parse_stream(resp, reply, n);
        if (!ok) snprintf(msg, 200, "On-device AI did not answer");
        free(resp); return ok && reply[0];
    }
    cJSON *r = cJSON_Parse(resp);
    bool ok = false;
    if (r) {
        cJSON *txt = NULL;
        if (provider == AI_GEMINI)
            txt = cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(r, "candidates"), 0), "content"), "parts"), 0), "text");
        else if (provider == AI_LOCAL)
            txt = cJSON_GetObjectItem(cJSON_GetObjectItem(r, "message"), "content");
        else
            txt = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(r, "choices"), 0), "message"), "content");
        if (cJSON_IsString(txt)) { snprintf(reply, n, "%s", txt->valuestring); ok = true; }
        else {
            cJSON *e = cJSON_GetObjectItem(r, "error");
            cJSON *em = cJSON_IsObject(e) ? cJSON_GetObjectItem(e, "message") : e;
            snprintf(msg, 200, "AI error: %.150s", cJSON_IsString(em) ? em->valuestring : "unexpected reply");
        }
        cJSON_Delete(r);
    } else snprintf(msg, 200, "AI reply could not be read");
    free(resp);
    return ok;
}

/* Try the chosen AI; if the on-device AI is unavailable and a Gemini key
 * is saved, fall back to Gemini. Returns which provider answered. */
/* Ollama streams one JSON object per line: {"message":{"content":"word"},"done":false} */
static bool parse_stream(const char *nd, char *reply, int n) {
    int k = 0; bool any = false; reply[0] = 0;
    const char *p = nd;
    while (*p) {
        const char *e = strchr(p, '\n'); size_t L = e ? (size_t)(e - p) : strlen(p);
        if (L > 2) {
            char *line = malloc(L + 1); memcpy(line, p, L); line[L] = 0;
            cJSON *r = cJSON_Parse(line);
            cJSON *t = r ? cJSON_GetObjectItem(cJSON_GetObjectItem(r, "message"), "content") : NULL;
            if (cJSON_IsString(t) && k < n - 1) { k += snprintf(reply + k, (size_t)(n - k), "%s", t->valuestring); any = true; }
            cJSON_Delete(r); free(line);
        }
        if (!e) break;
        p = e + 1;
    }
    if (k >= n) reply[n - 1] = 0;
    return any;
}

static int ask_ai_ex(const char *prompt, char *reply, int n, char *msg, bool json, bool stream) {
    if (AI->provider == AI_OFFLINE) { snprintf(msg, 200, "AI is switched off"); return 0; }
    if (AI->provider != AI_LOCAL && !AI->key[0]) { snprintf(msg, 200, "No API key saved"); return 0; }
    if (call_ai(AI->provider, prompt, reply, n, msg, json, stream)) return AI->provider;
    if (L_AI.abort) return 0;
    if (AI->provider == AI_LOCAL && AI->key[0] && call_ai(AI_GEMINI, prompt, reply, n, msg, json, false)) return AI_GEMINI;
    return 0;
}
static int ask_ai(const char *prompt, char *reply, int n, char *msg, bool json) { return ask_ai_ex(prompt, reply, n, msg, json, false); }

static bool job_ai(int i, char *msg) {
    Stock *s = &U->s[i];
    static char prompt[9000], reply[12000];
    if (!fresh_news[i]) { snprintf(msg, 200, "%s: no new headlines, kept the saved analysis", s->sym); return false; }
    build_prompt(i, prompt, sizeof prompt);
    int who = ask_ai(prompt, reply, sizeof reply, msg, true);
    if (!who) return false;
    Analysis a;
    if (!net_parse_analysis_json(reply, &a)) { snprintf(msg, 200, "%s: AI answer was not valid JSON", s->sym); return false; }
    time_t t = time(NULL); strftime(a.when, sizeof a.when, "%d %b %Y", localtime(&t));
    snprintf(a.by, sizeof a.by, "%s", who == AI_LOCAL ? "On-device AI" : who == AI_GEMINI ? "Gemini" : "AI");
    /* write analysis file (news lines appended so they travel together) */
    char p[400], np[400], line[400]; snprintf(p, sizeof p, "%.300s/analysis/%s.txt", U->dir, s->sym);
    snprintf(np, sizeof np, "%.300s/news_%s.txt", U->dir, s->sym);
    FILE *f = fopen(p, "w");
    if (!f) { snprintf(msg, 200, "cannot save analysis"); return false; }
    fprintf(f, "when=%s\nby=%s\nsentiment=%.2f\nvol_mult=%.2f\ndrift_tilt=%.3f\nevent_prob=%.2f\nevent_size=%.2f\nsummary=%s\n",
            a.when, a.by, a.sentiment, a.vol_mult, a.drift_tilt, a.event_prob, a.event_size, a.summary);
    if (a.headline[0]) fprintf(f, "headline=%s\n", a.headline);
    for (int k = 0; k < a.nf; k++) fprintf(f, "factor=%s|%d|%s\n", a.f[k].category, a.f[k].impact, a.f[k].text);
    FILE *nf = fopen(np, "r");
    if (nf) { int c = 0; while (fgets(line, sizeof line, nf) && c++ < 8) fprintf(f, "news=%s", line); fclose(nf); }
    fclose(f);
    snprintf(msg, 200, "%s: AI read the news (%d factors)", s->sym, a.nf);
    return true;
}

static bool job_test_ai(char *msg) {
    if (AI->provider == AI_LOCAL) {                  /* is Ollama running, and is the model installed? */
        char out[400]; tmp(out, sizeof out, "tags.json");
        char *tags = http_get("http://localhost:11434/api/tags", out);
        if (!tags || !tags[0]) { free(tags); snprintf(msg, 200, "Ollama is not running. Install it from ollama.com and open it."); return false; }
        bool have = false; char base[64]; snprintf(base, sizeof base, "%s", AI->local_model);
        cJSON *r = cJSON_Parse(tags), *m;
        cJSON_ArrayForEach(m, cJSON_GetObjectItem(r, "models")) {
            cJSON *nm = cJSON_GetObjectItem(m, "name");
            if (cJSON_IsString(nm) && !strncmp(nm->valuestring, base, strlen(base))) have = true;
        }
        cJSON_Delete(r); free(tags);
        if (!have) { snprintf(msg, 200, "Ollama is running. Now install the model: ollama pull %s", AI->local_model); return false; }
    }
    char reply[2000];
    if (!ask_ai("Reply with only this JSON: {\"summary\": \"connection works\", \"factors\": []}", reply, sizeof reply, msg, true)) return false;
    snprintf(msg, 200, "AI connected and answering (%s)", AI->provider == AI_LOCAL ? AI->local_model : AI->model);
    return true;
}

static bool job_chat(char *msg) {
    static char prompt[16000], reply[6000];
    pthread_mutex_lock(&mx); snprintf(prompt, sizeof prompt, "%s", chat_q); pthread_mutex_unlock(&mx);
    int who = ask_ai_ex(prompt, reply, sizeof reply, msg, false, true);
    pthread_mutex_lock(&mx); snprintf(chat_answer, sizeof chat_answer, "%s", who ? reply : ""); chat_stream[0] = 0; pthread_mutex_unlock(&mx);
    if (who) snprintf(msg, 200, "%s", who == AI_LOCAL ? "On-device AI" : who == AI_GEMINI ? "Gemini" : "AI");
    return who != 0;
}

bool net_chat(const char *prompt) {
    pthread_mutex_lock(&mx); snprintf(chat_q, sizeof chat_q, "%s", prompt); pthread_mutex_unlock(&mx);
    return net_enqueue(JOB_CHAT, -1);
}
/* The answer so far, while the on-device AI is still writing it. */
int net_chat_partial(char *out, int n) {
    char path[400]; pthread_mutex_lock(&mx); snprintf(path, sizeof path, "%s", chat_stream); pthread_mutex_unlock(&mx);
    out[0] = 0;
    if (!path[0]) return 0;
    char *nd = read_file(path, NULL);
    if (!nd) return 0;
    parse_stream(nd, out, n); free(nd);
    return (int)strlen(out);
}

/* Loads the model into memory in the background so the first question is quick. */
static bool job_warm(char *msg) {
    if (AI->provider != AI_LOCAL) return true;
    char reply[64];
    bool ok = call_ai(AI_LOCAL, "Reply with: ok", reply, sizeof reply, msg, false, false);
    snprintf(msg, 200, ok ? "On-device AI is warmed up" : "On-device AI not running");
    return ok;
}
void net_chat_reply(char *out, int n) { pthread_mutex_lock(&mx); snprintf(out, n, "%s", chat_answer); pthread_mutex_unlock(&mx); }

/* -------------------------------- workers -------------------------------- */
static const char *job_label(int t) {
    return t == JOB_PRICES ? "Downloading prices:" : t == JOB_FX ? "Exchange rate" : t == JOB_NEWS ? "Reading news:" : t == JOB_AI ? "AI reading the news:" :
           t == JOB_CHAT ? "AI is answering" : t == JOB_WARM ? "Warming up the on-device AI" : "Checking the AI";
}
static void *work(void *arg) {
    Lane *L = arg;
    for (;;) {
        Job j;
        pthread_mutex_lock(&mx);
        while (running && !queue_pop(&L->q, &j)) pthread_cond_wait(&cv, &mx);
        if (!running) { pthread_mutex_unlock(&mx); break; }
        snprintf(L->current, sizeof L->current, "%s %s", job_label(j.type), j.stock >= 0 && j.stock < U->n ? U->s[j.stock].sym : "");
        L->cur_type = j.type; L->abort = 0;
        pthread_mutex_unlock(&mx);

        JobResult r = { j.type, j.stock, false, "" };
        switch (j.type) {
        case JOB_PRICES:  r.ok = job_prices(j.stock, r.msg); break;
        case JOB_FX:      r.ok = job_fx(r.msg); break;
        case JOB_NEWS:    r.ok = job_news(j.stock, r.msg); break;
        case JOB_AI:      r.ok = job_ai(j.stock, r.msg); break;
        case JOB_TEST_AI: r.ok = job_test_ai(r.msg); break;
        case JOB_CHAT:    r.ok = job_chat(r.msg); break;
        case JOB_WARM:    r.ok = job_warm(r.msg); break;
        }
        pthread_mutex_lock(&mx);
        if (L->abort && j.type == JOB_AI) queue_push(&L->q, j);          /* interrupted by a question: redo later */
        else if (j.type != JOB_WARM && done_n < Q_CAP) { done[done_t] = r; done_t = (done_t + 1) % Q_CAP; done_n++; }
        L->current[0] = 0; L->cur_type = 0; L->abort = 0;
        pthread_mutex_unlock(&mx);
    }
    return NULL;
}

void net_start(Universe *u, AiSettings *ai) {
    U = u; AI = ai;
    queue_init(&L_NET.q); queue_init(&L_AI.q);
    char p[400];
    snprintf(p, sizeof p, "%.300s/prices", u->dir);
#ifdef _WIN32
    CreateDirectoryA(p, NULL);
    snprintf(p, sizeof p, "%.300s/analysis", u->dir); CreateDirectoryA(p, NULL);
#else
    char cmd[500]; snprintf(cmd, sizeof cmd, "mkdir -p \"%s\" \"%.300s/analysis\"", p, u->dir); if (system(cmd)) {}
#endif
    running = true;
    pthread_create(&L_NET.th, NULL, work, &L_NET);
    pthread_create(&L_AI.th, NULL, work, &L_AI);
}

void net_stop(void) {
    pthread_mutex_lock(&mx); running = false; L_AI.abort = 1; pthread_cond_broadcast(&cv); pthread_mutex_unlock(&mx);
    pthread_join(L_NET.th, NULL); pthread_join(L_AI.th, NULL);
}

bool net_enqueue(int type, int stock) {
    pthread_mutex_lock(&mx);
    bool ok;
    if (type == JOB_CHAT || type == JOB_TEST_AI) {
        ok = queue_push_front(&L_AI.q, (Job){ type, stock });              /* the user is waiting: go first */
        if (L_AI.cur_type == JOB_AI) L_AI.abort = 1;                      /* and interrupt a background analysis */
    } else ok = queue_push(is_ai_job(type) ? &L_AI.q : &L_NET.q, (Job){ type, stock });
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mx);
    return ok;
}

int net_pending_type(int type) {
    pthread_mutex_lock(&mx);
    int n = queue_count_type(&L_NET.q, type) + queue_count_type(&L_AI.q, type) + (L_NET.cur_type == type) + (L_AI.cur_type == type);
    pthread_mutex_unlock(&mx);
    return n;
}

bool net_poll(JobResult *out) {
    pthread_mutex_lock(&mx);
    bool ok = done_n > 0;
    if (ok) { *out = done[done_h]; done_h = (done_h + 1) % Q_CAP; done_n--; }
    pthread_mutex_unlock(&mx);
    return ok;
}

int net_pending(void) {
    pthread_mutex_lock(&mx);
    int n = L_NET.q.count + L_AI.q.count + (L_NET.current[0] ? 1 : 0) + (L_AI.current[0] && L_AI.cur_type != JOB_WARM ? 1 : 0);
    pthread_mutex_unlock(&mx);
    return n;
}
const char *net_current(void) {
    static char c[128]; pthread_mutex_lock(&mx);
    snprintf(c, sizeof c, "%s", L_NET.current[0] ? L_NET.current : L_AI.current);
    pthread_mutex_unlock(&mx); return c;
}

/* Opens a file with the computer's default app (the PDF viewer for reports). */
void open_file(const char *path) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
#else
    char cmd[700]; snprintf(cmd, sizeof cmd, "xdg-open \"%s\" >/dev/null 2>&1 &", path); if (system(cmd)) {}
#endif
}
