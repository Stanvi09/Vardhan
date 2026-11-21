/*
 * ds.h - data structures used by Vardhan.
 *
 *   Structure        Where it is used
 *   ---------------  ------------------------------------------------------
 *   Hash table       ticker -> stock, O(1) lookup ("RELIANCE", "NVDA")
 *   Trie             stock search box + friend-tip autocomplete
 *   Max-heap         ranking every stock by how well it fits YOU -> top K
 *   Graph            stocks that move together (correlation > 0.6) are
 *                    linked; BFS finds clusters so your plan is diversified
 *   Segment tree     highest / lowest price over any date range on a chart
 *   Queue            background jobs: download prices, fetch news, ask AI
 *   Linked list      each stock's news feed (newest first)
 *   Stack            screen navigation (Back button)
 */
#ifndef DS_H
#define DS_H

#include <stdbool.h>
#include <stdint.h>

/* ----------------------------- HASH TABLE ----------------------------- */
#define HM_CAP 128                         /* power of two, > 2x stocks */
typedef struct {
    char key[HM_CAP][20];
    int  val[HM_CAP];
    bool used[HM_CAP];
    int  count;
    int  last_probes;
} HashMap;
unsigned hm_hash(const char *s);
void hm_init(HashMap *m);
bool hm_put(HashMap *m, const char *key, int val);
int  hm_get(HashMap *m, const char *key);           /* -1 if missing */

/* -------------------------------- TRIE -------------------------------- */
#define TRIE_ALPHA 38                      /* A-Z 0-9 & space */
typedef struct TrieNode {
    struct TrieNode *kid[TRIE_ALPHA];
    int ids[6]; int nids;                  /* stocks whose ticker/name word ends here */
} TrieNode;
typedef struct { TrieNode *root; int nodes; int last_steps; } Trie;
void trie_init(Trie *t);
void trie_free(Trie *t);
void trie_add(Trie *t, const char *word, int id);
int  trie_find(Trie *t, const char *prefix, int *out, int max);

/* -------------------------------- HEAP -------------------------------- */
#define HEAP_CAP 64
typedef struct { float key; int id; } HeapItem;
typedef struct { HeapItem a[HEAP_CAP]; int n; } MaxHeap;
void heap_init(MaxHeap *h);
bool heap_push(MaxHeap *h, float key, int id);
bool heap_pop(MaxHeap *h, HeapItem *out);
bool heap_valid(const MaxHeap *h);

/* -------------------------------- GRAPH ------------------------------- */
#define G_MAX 40
typedef struct {
    int   n;
    float w[G_MAX][G_MAX];                 /* correlation, 0 = no edge */
    bool  edge[G_MAX][G_MAX];
    int   comp[G_MAX];                     /* cluster id from BFS */
    int   ncomp;
} Graph;
void graph_init(Graph *g, int n);
void graph_link(Graph *g, int a, int b, float w);
int  graph_components(Graph *g);           /* BFS, fills comp[] */
int  graph_degree(const Graph *g, int v);

/* ---------------------------- SEGMENT TREE ---------------------------- */
typedef struct { float *mn, *mx; int n; } SegTree;
void  seg_build(SegTree *s, const float *v, int n);
void  seg_free(SegTree *s);
void  seg_query(const SegTree *s, int l, int r, float *lo, float *hi);

/* ------------------------------- QUEUE -------------------------------- */
#define Q_CAP 128
enum { JOB_PRICES = 1, JOB_NEWS, JOB_AI, JOB_FX, JOB_TEST_AI, JOB_CHAT, JOB_WARM };
typedef struct { int type; int stock; } Job;
typedef struct { Job a[Q_CAP]; int head, tail, count; } JobQueue;
void queue_init(JobQueue *q);
bool queue_push(JobQueue *q, Job j);
bool queue_pop(JobQueue *q, Job *out);
bool queue_push_front(JobQueue *q, Job j);          /* deque: priority job */
int  queue_count_type(const JobQueue *q, int type);

/* ---------------------------- LINKED LIST ----------------------------- */
typedef struct NewsItem {
    char title[220];
    char source[60];
    char when[40];
    struct NewsItem *next;
} NewsItem;
NewsItem *news_push_front(NewsItem *head, const char *title, const char *source, const char *when);
int       news_count(const NewsItem *h);
void      news_free(NewsItem *h);

/* ------------------------------- STACK -------------------------------- */
#define NAV_MAX 32
typedef struct { int a[NAV_MAX]; int top; } NavStack;
void nav_push(NavStack *s, int screen);
int  nav_pop(NavStack *s, int fallback);

#endif
