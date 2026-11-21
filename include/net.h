/*
 * net.h - live prices, news and AI analysis, done by a background worker.
 *
 * The UI puts jobs in a QUEUE; one worker thread takes them out in order
 * and writes results to files in the data folder; the UI picks up finished
 * results every frame. Internet access uses the `curl` program that ships
 * with Windows 10/11, macOS and Linux, so no extra libraries are needed.
 *
 *   prices : Yahoo Finance chart API (2 years of daily closes, no key)
 *   news   : Google News RSS search (no key)
 *   AI     : on-device model via Ollama (no key, no limits, works offline),
 *            Google Gemini, or any OpenAI-compatible API.
 *   If every AI is unreachable, the last saved analysis is kept.
 */
#ifndef NET_H
#define NET_H

#include "model.h"

enum { AI_OFFLINE = 0, AI_GEMINI = 1, AI_OPENAI = 2, AI_LOCAL = 3 };

typedef struct {
    int  provider;
    char key[160];
    char model[64];
    char base_url[160];        /* OpenAI-compatible endpoint, e.g. https://api.openai.com/v1 */
    char local_model[64];      /* on-device model served by Ollama, e.g. llama3.2:3b */
    bool auto_refresh;         /* refresh prices + news when the app opens */
    int  theme;                /* 0 paper, 1 night, 2 amethyst */
    int  sound;                /* button and money sounds on/off */
} AiSettings;

typedef struct { int type; int stock; bool ok; char msg[200]; } JobResult;

void net_start(Universe *u, AiSettings *ai);
void net_stop(void);
bool net_enqueue(int type, int stock);
bool net_poll(JobResult *out);           /* finished jobs, call every frame */
int  net_pending(void);
bool net_chat(const char *prompt);             /* ask a free-text question (async, JOB_CHAT result) */
void net_chat_reply(char *out, int n);         /* the answer once the JOB_CHAT result arrives */
int  net_chat_partial(char *out, int n);       /* words written so far (on-device AI streams) */
int  net_pending_type(int type);
void open_file(const char *path);               /* how many jobs of a kind are queued or running */
const char *net_current(void);           /* what the worker is doing now */

void settings_load(AiSettings *s, const char *dir);
void settings_save(const AiSettings *s, const char *dir);

/* exposed for tests */
int  net_parse_rss(const char *xml, char titles[][220], char sources[][60], char dates[][40], int max);
bool net_parse_analysis_json(const char *json, Analysis *a);
bool net_parse_yahoo(const char *json, int *dates, float *closes, int *n, int max);

#endif
