/* app.h - Vardhan application state. */
#ifndef APP_H
#define APP_H

#include "model.h"
#include "net.h"
#include "ui.h"

typedef enum { S_ONBOARD = 0, S_HOME, S_PLAN, S_ASK, S_EXPLORE, S_FRIEND, S_PROOF, S_HOOD, S_SETTINGS, S_COUNT } Scr;

#define CHAT_MAX 40
typedef struct { int who; char text[2400]; char by[64]; } ChatMsg;   /* who: 0 user, 1 instant, 2 AI, 3 note */

typedef struct {
    Universe  *u;
    Profile    prof;
    Plan      *plan;
    AiSettings ai;
    NavStack   nav;
    Scr        scr;
    float      scr_t;                      /* time the screen was opened (animations) */
    bool       plan_dirty;

    /* onboarding */
    int  ob;
    char ob_name[32], ob_lump[14], ob_sip[12];
    int  ob_age, ob_gkind, ob_gyears;
    char ob_gamt[14];
    char ob_age_s[4], ob_gyears_s[4];      /* typed, not dragged */

    /* explore */
    int  ex;                               /* selected stock */
    char ex_q[24];
    int  ex_mkt;
    char ex_amt[14];
    Outcome ex_out; int ex_out_for; long long ex_out_amt;
    bool drag; int sel_a, sel_b; bool has_sel;
    float ex_scroll;

    /* friend */
    char fr_q[24], fr_amt[14];
    int  fr_goal, fr_stock;
    Verdict fr_v; Outcome fr_o; bool fr_done; float fr_t;

    /* settings */
    char set_key[160], set_model[64], set_url[160], set_local[64];
    int  upd_prices, upd_ai;

    /* chat */
    ChatMsg chat[CHAT_MAX]; int nchat;
    char  chat_in[300];
    bool  chat_wait, chat_stick;
    float chat_scroll, chat_sent_t;

    /* what-if */
    bool  wi_open, wi_dirty;
    int   wi_sip_k, wi_lump_k, wi_years[MAX_GOALS];
    float wi_prob[MAX_GOALS], wi_t;
    long long wi_p50[MAX_GOALS];

    /* proof */
    bool  bt_ready;

    /* newspaper + loading moments */
    bool  paper_on; int paper_stock; long long paper_amt; float paper_t;

    /* portfolio: what was actually invested */
    Portfolio port; char port_file[64];
    int   home_tab;
    bool  inv_open; int inv_mode, inv_when; char inv_sym[16], inv_amt[14]; float inv_t;

    /* alerts */
    AlertBook alerts; char alert_file[64]; MaxHeap alert_heap;
    bool  al_open; int al_stock, al_kind; char al_level[16]; float al_t;

    /* report */
    int   report_req;              /* frames until the PDF is made */

    /* Vani dock */
    bool  vani_open; float vani_t; char vani_in[300];

    /* sign-in */
    Users users; int cur_user, login_sel, login_fails;
    bool  logged_in, intro_done, creating;
    char  login_pin[8], login_err[96], ob_pin[8];
    float intro_t0, login_t, shake_t, spin_t, lock_until;
    float build_t;
    int   plan_news_tab;
    unsigned char want_ai[MAX_STOCKS];   /* run the AI once this stock's news arrives */
    char  chat_live[2400];               /* streamed words so far */                     /* "building your plan" overlay start */                 /* background refresh counters */
    char last_update[32];
    JobResult log[12]; int nlog;
} App;

void go(App *a, Scr s);
void plan_rebuild(App *a);
void draw_onboard(App *a);
void draw_login(App *a);
void sign_in(App *a, int user);
void sign_out(App *a);
void demo_account(App *a);
void alerts_demo(App *a);
bool report_make(App *a, char *path_out, int n);
void paper_print(App *a, int stock, float ox, float oy, float pw, float ph);
void alerts_attach(App *a, const char *profile_file);
void alerts_refresh(App *a);
void alert_open(App *a, int stock);
void draw_alert_banner(App *a, Rectangle r);
void draw_alert_modal(App *a);
bool brand_intro(float t);
void brand_mark(float cx, float cy, float size, float rot_deg, float draw, float alpha);
void brand_window_icon(void);
void draw_plan(App *a, Rectangle r);
void draw_home(App *a, Rectangle r);
void nav_icon(int k, float cx, float cy, Color c);
void port_attach(App *a, const char *profile_file);
void draw_ask(App *a, Rectangle r);
void draw_vani_dock(App *a);
Rectangle vani_dock_rect(App *a);
float chat_rich(Font f, const char *src, float x, float y, float w, Color c, bool draw);
void draw_proof(App *a, Rectangle r);
void chat_send(App *a, const char *q);
void chat_on_result(App *a, bool ok, const char *by);
void logo_mark(float x, float y, float s);
void paper_open(App *a, int stock, long long amount);
void draw_paper(App *a);
void draw_explore(App *a, Rectangle r);
void draw_friend(App *a, Rectangle r);
void draw_hood(App *a, Rectangle r);
void draw_settings(App *a, Rectangle r);
void header(const char *title, const char *sub, Rectangle r);
Color pick_color(int k);
void  refresh_all(App *a, bool prices, int ai);

#endif
