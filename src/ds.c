/* ds.c - implementations of the data structures in ds.h */
#include "ds.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================== HASH TABLE ============================== */
static int ci_cmp(const char *a, const char *b) {
    while (*a && toupper((unsigned char)*a) == toupper((unsigned char)*b)) { a++; b++; }
    return toupper((unsigned char)*a) - toupper((unsigned char)*b);
}
unsigned hm_hash(const char *s) {                /* djb2 */
    unsigned h = 5381;
    while (*s) h = h * 33 + (unsigned char)toupper((unsigned char)*s++);
    return h;
}
void hm_init(HashMap *m) { memset(m, 0, sizeof *m); }

static int slot(HashMap *m, const char *key, bool *found) {
    unsigned i = hm_hash(key) & (HM_CAP - 1);
    m->last_probes = 1;
    while (m->used[i]) {                          /* linear probing */
        if (ci_cmp(m->key[i], key) == 0) { *found = true; return (int)i; }
        i = (i + 1) & (HM_CAP - 1);
        m->last_probes++;
    }
    *found = false;
    return (int)i;
}
bool hm_put(HashMap *m, const char *key, int val) {
    if (m->count >= HM_CAP * 3 / 4) return false;
    bool f; int i = slot(m, key, &f);
    if (!f) { m->used[i] = true; strncpy(m->key[i], key, 19); m->count++; }
    m->val[i] = val;
    return true;
}
int hm_get(HashMap *m, const char *key) { bool f; int i = slot(m, key, &f); return f ? m->val[i] : -1; }

/* ================================= TRIE ================================= */
static int tidx(char c) {
    c = (char)toupper((unsigned char)c);
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return 26 + c - '0';
    if (c == '&') return 36;
    if (c == ' ') return 37;
    return -1;
}
static TrieNode *tnew(Trie *t) { t->nodes++; return calloc(1, sizeof(TrieNode)); }
void trie_init(Trie *t) { t->nodes = 0; t->last_steps = 0; t->root = tnew(t); }
static void tfree(TrieNode *n) { if (!n) return; for (int i = 0; i < TRIE_ALPHA; i++) tfree(n->kid[i]); free(n); }
void trie_free(Trie *t) { tfree(t->root); t->root = NULL; }

void trie_add(Trie *t, const char *w, int id) {
    TrieNode *n = t->root;
    for (; *w; w++) {
        int i = tidx(*w);
        if (i < 0) continue;
        if (!n->kid[i]) n->kid[i] = tnew(t);
        n = n->kid[i];
    }
    for (int k = 0; k < n->nids; k++) if (n->ids[k] == id) return;
    if (n->nids < 6) n->ids[n->nids++] = id;
}

static void collect(TrieNode *n, int *out, int *k, int max) {
    if (!n || *k >= max) return;
    for (int j = 0; j < n->nids && *k < max; j++) {
        bool dup = false;
        for (int q = 0; q < *k; q++) if (out[q] == n->ids[j]) dup = true;
        if (!dup) out[(*k)++] = n->ids[j];
    }
    for (int i = 0; i < TRIE_ALPHA; i++) collect(n->kid[i], out, k, max);
}
int trie_find(Trie *t, const char *p, int *out, int max) {
    TrieNode *n = t->root; t->last_steps = 0;
    if (!*p) return 0;
    for (; *p && n; p++) { int i = tidx(*p); if (i < 0) return 0; n = n->kid[i]; t->last_steps++; }
    int k = 0;
    collect(n, out, &k, max);
    return k;
}

/* ================================= HEAP ================================= */
void heap_init(MaxHeap *h) { h->n = 0; }
static void hswap(HeapItem *a, HeapItem *b) { HeapItem t = *a; *a = *b; *b = t; }
bool heap_push(MaxHeap *h, float key, int id) {
    if (h->n == HEAP_CAP) return false;
    int i = h->n++;
    h->a[i] = (HeapItem){ key, id };
    while (i > 0 && h->a[(i - 1) / 2].key < h->a[i].key) { hswap(&h->a[i], &h->a[(i - 1) / 2]); i = (i - 1) / 2; }
    return true;
}
bool heap_pop(MaxHeap *h, HeapItem *out) {
    if (!h->n) return false;
    *out = h->a[0];
    h->a[0] = h->a[--h->n];
    int i = 0;
    for (;;) {
        int l = 2 * i + 1, r = l + 1, b = i;
        if (l < h->n && h->a[l].key > h->a[b].key) b = l;
        if (r < h->n && h->a[r].key > h->a[b].key) b = r;
        if (b == i) break;
        hswap(&h->a[i], &h->a[b]); i = b;
    }
    return true;
}
bool heap_valid(const MaxHeap *h) {
    for (int i = 1; i < h->n; i++) if (h->a[(i - 1) / 2].key < h->a[i].key) return false;
    return true;
}

/* ================================= GRAPH ================================ */
void graph_init(Graph *g, int n) { memset(g, 0, sizeof *g); g->n = n; }
void graph_link(Graph *g, int a, int b, float w) { g->edge[a][b] = g->edge[b][a] = true; g->w[a][b] = g->w[b][a] = w; }
int graph_degree(const Graph *g, int v) { int d = 0; for (int i = 0; i < g->n; i++) d += g->edge[v][i]; return d; }

/* Breadth-first search from every unvisited node labels each connected
 * component ("cluster" of stocks that tend to move together). */
int graph_components(Graph *g) {
    int q[G_MAX];
    for (int i = 0; i < g->n; i++) g->comp[i] = -1;
    g->ncomp = 0;
    for (int s = 0; s < g->n; s++) {
        if (g->comp[s] >= 0) continue;
        int h = 0, t = 0;
        q[t++] = s; g->comp[s] = g->ncomp;
        while (h < t) {
            int v = q[h++];
            for (int u = 0; u < g->n; u++)
                if (g->edge[v][u] && g->comp[u] < 0) { g->comp[u] = g->ncomp; q[t++] = u; }
        }
        g->ncomp++;
    }
    return g->ncomp;
}

/* ============================= SEGMENT TREE ============================= */
static void sbuild(SegTree *s, const float *v, int node, int l, int r) {
    if (l == r) { s->mn[node] = s->mx[node] = v[l]; return; }
    int m = (l + r) / 2;
    sbuild(s, v, 2 * node, l, m); sbuild(s, v, 2 * node + 1, m + 1, r);
    s->mn[node] = s->mn[2 * node] < s->mn[2 * node + 1] ? s->mn[2 * node] : s->mn[2 * node + 1];
    s->mx[node] = s->mx[2 * node] > s->mx[2 * node + 1] ? s->mx[2 * node] : s->mx[2 * node + 1];
}
void seg_build(SegTree *s, const float *v, int n) {
    seg_free(s);
    if (n <= 0) return;
    s->n = n;
    s->mn = malloc(sizeof(float) * 4 * (size_t)n);
    s->mx = malloc(sizeof(float) * 4 * (size_t)n);
    sbuild(s, v, 1, 0, n - 1);
}
void seg_free(SegTree *s) { free(s->mn); free(s->mx); s->mn = s->mx = NULL; s->n = 0; }
static void sq(const SegTree *s, int node, int l, int r, int ql, int qr, float *lo, float *hi) {
    if (qr < l || r < ql) return;
    if (ql <= l && r <= qr) { if (s->mn[node] < *lo) *lo = s->mn[node]; if (s->mx[node] > *hi) *hi = s->mx[node]; return; }
    int m = (l + r) / 2;
    sq(s, 2 * node, l, m, ql, qr, lo, hi); sq(s, 2 * node + 1, m + 1, r, ql, qr, lo, hi);
}
void seg_query(const SegTree *s, int l, int r, float *lo, float *hi) {
    *lo = 1e30f; *hi = -1e30f;
    if (!s->n) return;
    if (l < 0) l = 0;
    if (r >= s->n) r = s->n - 1;
    sq(s, 1, 0, s->n - 1, l, r, lo, hi);
}

/* ================================= QUEUE ================================= */
void queue_init(JobQueue *q) { q->head = q->tail = q->count = 0; }
bool queue_push(JobQueue *q, Job j) {
    if (q->count == Q_CAP) return false;
    q->a[q->tail] = j; q->tail = (q->tail + 1) % Q_CAP; q->count++;
    return true;
}
/* DEQUE: the user's own question jumps ahead of background work */
bool queue_push_front(JobQueue *q, Job j) {
    if (q->count == Q_CAP) return false;
    q->head = (q->head - 1 + Q_CAP) % Q_CAP; q->a[q->head] = j; q->count++;
    return true;
}
int queue_count_type(const JobQueue *q, int type) {
    int n = 0; for (int i = 0; i < q->count; i++) n += q->a[(q->head + i) % Q_CAP].type == type;
    return n;
}
bool queue_pop(JobQueue *q, Job *out) {
    if (!q->count) return false;
    *out = q->a[q->head]; q->head = (q->head + 1) % Q_CAP; q->count--;
    return true;
}

/* ============================== LINKED LIST ============================== */
NewsItem *news_push_front(NewsItem *head, const char *title, const char *source, const char *when) {
    NewsItem *n = calloc(1, sizeof *n);
    strncpy(n->title, title, sizeof n->title - 1);
    strncpy(n->source, source, sizeof n->source - 1);
    strncpy(n->when, when, sizeof n->when - 1);
    n->next = head;
    return n;
}
int news_count(const NewsItem *h) { int c = 0; for (; h; h = h->next) c++; return c; }
void news_free(NewsItem *h) { while (h) { NewsItem *n = h->next; free(h); h = n; } }

/* ================================= STACK ================================= */
void nav_push(NavStack *s, int screen) {
    if (s->top == NAV_MAX) { memmove(s->a, s->a + 1, (NAV_MAX - 1) * sizeof(int)); s->top--; }
    s->a[s->top++] = screen;
}
int nav_pop(NavStack *s, int fallback) { return s->top ? s->a[--s->top] : fallback; }
