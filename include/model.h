/*
 * model.h - stocks, the investor profile and the personal plan.
 *
 * Prices are float rupees/dollars per share; all money shown to the user
 * is converted to rupees (US stocks use the USD/INR rate).
 */
#ifndef MODEL_H
#define MODEL_H

#include "ds.h"

#define MAX_STOCKS  40
#define MAX_DAYS    560          /* ~2 years of trading days */
#define MAX_FACTORS 6
#define MAX_GOALS   4
#define MAX_PICKS   10
#define FAN_STEPS   53           /* weekly points over one year */

typedef enum { MKT_IN = 0, MKT_US = 1 } Market;

/* What the AI concluded after reading the news. */
typedef struct {
    char category[20];           /* Leadership, Earnings, Geopolitics, Regulation, Macro, Sector, Operations */
    int  impact;                 /* -3 (very bad) .. +3 (very good) */
    char text[200];
} Factor;

typedef struct {
    bool  valid;
    char  when[32];              /* date generated */
    char  by[32];                /* "Pre-generated", "Gemini", "OpenAI" */
    int   nf;
    Factor f[MAX_FACTORS];
    float sentiment;             /* -1 .. +1 */
    float vol_mult;              /* how much more/less volatile than history, 0.7 .. 2.0 */
    float drift_tilt;            /* change to expected yearly return, -0.20 .. +0.10 */
    float event_prob;            /* chance of a sudden shock within a year, 0 .. 0.35 */
    float event_size;            /* size of that shock, e.g. -0.25 */
    char  summary[320];
    char  headline[160];         /* a newspaper headline for this stock */
} Analysis;

typedef struct {
    char   sym[16];              /* display ticker: RELIANCE, NVDA */
    char   yahoo[20];            /* data ticker: RELIANCE.NS, NVDA, ^NSEI */
    char   name[48];
    char   sector[24];
    Market mkt;
    bool   fund;                 /* index fund / ETF */
    bool   hidden;               /* benchmark index, not investable */

    int    n;                    /* number of daily prices */
    int    date[MAX_DAYS];       /* YYYYMMDD */
    float  close[MAX_DAYS];
    bool   live;                 /* true = real downloaded prices */
    char   updated[32];
    SegTree seg;

    /* statistics computed from prices */
    float price, chg1d, ret1y, vol, mu, beta, maxdd, corr_index;

    Analysis  ai;
    NewsItem *news;              /* LINKED LIST, newest first */
    int    status;               /* bit flags for background jobs */
} Stock;

typedef struct {
    Stock   s[MAX_STOCKS];
    int     n;
    int     idx_in, idx_us;      /* benchmark index positions */
    float   usdinr;
    HashMap map;                 /* HASH TABLE: ticker -> index */
    Trie    trie;                /* TRIE: search */
    Graph   corr;                /* GRAPH: correlation links */
    char    dir[300];            /* data folder */
} Universe;

/* ------------------------------- profile ------------------------------- */
enum { G_HOUSE = 0, G_EDU, G_RETIRE, G_CAR, G_TRAVEL, G_WEALTH, G_KINDS };
typedef struct {
    int       kind;
    char      name[32];
    long long target;            /* rupees */
    int       years;
} Goal;

typedef struct {
    bool      done;
    char      name[32];
    int       age;
    long long lump;              /* one-time amount to invest now */
    long long sip;               /* monthly amount */
    int       quiz[4];           /* answers 0..2 */
    int       markets;           /* 1 India, 2 US, 3 both */
    Goal      goals[MAX_GOALS];
    int       ngoals;
    float     risk;              /* 0 (careful) .. 1 (bold), computed */
} Profile;

/* --------------------------------- plan --------------------------------- */
typedef struct {
    int   stock;                 /* -1 = safe bucket (fixed deposit) */
    float weight;
    long long amount;
    float fit;
    char  why[200];
} Pick;

typedef struct {
    bool  ready;
    float equity;                /* share in stocks, rest in safe deposits */
    int   npicks;
    Pick  picks[MAX_PICKS];
    float exp_ret, vol;          /* yearly */
    /* one-year outlook for the lump sum, in rupees */
    float fan[5][FAN_STEPS];     /* P5 P25 P50 P75 P95 */
    long long good, likely, bad, worst, var95, cvar95, stress_covid, stress_2008;
    float prob_loss;             /* chance of ending the year below what you put in */
    /* goals */
    float goal_prob[MAX_GOALS];
    long long goal_p50[MAX_GOALS], goal_sip_needed[MAX_GOALS];
    int   heap_considered, clusters;
} Plan;

/* data.c */
void  uni_init(Universe *u, const char *dir);
void  uni_free(Universe *u);
void  uni_compute_stats(Universe *u);         /* stats, segment trees, correlation graph */
int   uni_find(Universe *u, const char *ticker);
bool  uni_load_cache(Universe *u);
bool  uni_save_prices(Universe *u, int i);
bool  uni_save_analysis(Universe *u, int i);
void  uni_load_analyses(Universe *u);
void  uni_reload_analysis(Universe *u, int i);
bool  uni_reload_prices(Universe *u, int i);
float uni_corr(const Universe *u, int a, int b);
float to_inr(const Universe *u, int i, float px);

/* profile.c */
void  profile_compute_risk(Profile *p);
bool  profile_save(const Profile *p, const char *dir);
bool  profile_load(Profile *p, const char *dir);
const char *risk_label(float r);
const char *goal_kind_name(int k);

/* risk.c */
typedef struct { float p[5]; float mean, prob_loss, var95, cvar95; float hist[40]; float hist_lo, hist_hi; } Outcome;
void  risk_stock_outcome(const Universe *u, int i, float amount_inr, int days, Outcome *o, unsigned seed);
void  risk_stress(const Universe *u, int i, float *covid, float *gfc);   /* fractional loss in each crisis */
void  risk_build_plan_outlook(const Universe *u, Plan *p, long long amount);
float risk_goal_probability(const Universe *u, const Plan *p, long long start, long long sip, int years, long long target, long long *p50);

/* advisor.c */
float advisor_fit(const Universe *u, const Profile *pr, int i, char *why, int whylen);
void  advisor_build_plan(Universe *u, const Profile *pr, Plan *plan);
typedef struct { int verdict; char headline[120]; char reasons[5][200]; int nreasons; int alternatives[3]; int nalt; float fit; } Verdict;
void  advisor_friend_check(Universe *u, const Profile *pr, int stock, long long amount, int goal, Verdict *v, Outcome *o);


/* users.c - local accounts: name -> profile file, protected by a PIN */
#define MAX_USERS 16
typedef struct { char name[32]; char file[64]; unsigned long long salt, hash; } User;
typedef struct { User u[MAX_USERS]; int n; HashMap map; } Users;
void profile_set_file(const char *name);
unsigned long long pin_hash(unsigned long long salt, const char *pin);
void users_load(Users *us, const char *dir);
bool users_save(const Users *us, const char *dir);
int  users_find(Users *us, const char *name);                 /* HASH TABLE lookup, -1 if none */
int  users_add(Users *us, const char *name, const char *pin);  /* -1 if taken or full */
bool users_check(const Users *us, int i, const char *pin);

/* portfolio.c - money actually invested (LINKED LIST of transactions) */
typedef struct Txn { int date; char sym[16]; double amount, price; struct Txn *next; } Txn;
typedef struct { Txn *head; int n; } Portfolio;
typedef struct { char sym[16]; double invested, value; } Holding;
long   port_days(int a, int b);
int    price_index(const Stock *s, int date);                       /* binary search */
int    port_today(const Universe *u);
int    port_months_ago(const Universe *u, int months);
Txn   *port_add(Portfolio *p, Universe *u, int date, const char *sym, double amount);
bool   port_remove(Portfolio *p, Txn *t);
void   port_free(Portfolio *p);
double txn_value(const Universe *u, const Txn *t, int date);
void   port_totals(const Universe *u, const Portfolio *p, double *invested, double *value);
int    port_holdings(const Universe *u, const Portfolio *p, Holding *out, int max);
double port_xirr(const Universe *u, const Portfolio *p, bool *ok);
int    port_history(const Universe *u, const Portfolio *p, double *val, double *inv, int n);
bool   port_save(const Portfolio *p, const char *dir, const char *file);
void   port_load(Portfolio *p, const char *dir, const char *file);

/* alerts.c - price and news alerts, most urgent first (MAX-HEAP) */
#define MAX_ALERTS 32
enum { ALERT_BELOW = 0, ALERT_ABOVE, ALERT_NEWS };
typedef struct { char sym[16]; int kind; float level; bool dismissed; } Alert;
typedef struct { Alert a[MAX_ALERTS]; int n; } AlertBook;
void  alerts_load(AlertBook *b, const char *dir, const char *file);
bool  alerts_save(const AlertBook *b, const char *dir, const char *file);
bool  alerts_add(AlertBook *b, const char *sym, int kind, float level);
void  alerts_remove(AlertBook *b, int i);
float alert_severity(Universe *u, const Alert *x);
int   alerts_check(Universe *u, const AlertBook *b, MaxHeap *h);
void  alert_text(Universe *u, const Alert *x, char *out, int n);

/* backtest.c - "Proof it works" */
typedef struct { int stock, tests, breaks; } Calib;
typedef struct {
    bool  ok; char msg[96];
    Plan  plan;                              /* plan built with prices up to one year ago */
    int   from, to;                          /* YYYYMMDD */
    long long amount;
    float actual[FAN_STEPS], index[FAN_STEPS];
    int   weeks_inside; bool inside, in_middle; float worst_dip;
    int   hot; float hot_final, hot_dip, hot_past_ret;
    Calib cal[MAX_STOCKS]; int ncal, tests, breaks;
} Backtest;
bool bt_run(Universe *u, const Profile *pr, Backtest *bt);

/* assistant.c - "Ask Vardhan" */
int       chat_find_stocks(Universe *u, const char *q, int *out, int max);
long long chat_amount(const char *q);
void      chat_offline(Universe *u, const Profile *pr, const Plan *pl, const char *q, char *out, int n);
void      chat_prompt(Universe *u, const Profile *pr, const Plan *pl, const char *history, const char *q, char *out, int n);

#endif
