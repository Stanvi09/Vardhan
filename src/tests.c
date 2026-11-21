/*
 * tests.c - automatic tests: data structures, risk engine, advisor,
 * parsers for live data / AI replies, and boundary cases.
 * Run:  risklens_tests   (from the folder that contains /data)
 */
#include "model.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
#endif

static int pass, fail;
#define CHECK(c, name) do { if (c) { pass++; printf("  \x1b[32mPASS\x1b[0m  %s\n", name); } else { fail++; printf("  \x1b[31mFAIL\x1b[0m  %s  (line %d)\n", name, __LINE__); } } while (0)
#define SECTION(s) printf("\n\x1b[1m%s\x1b[0m\n", s)

static void test_ds(void) {
    SECTION("Data structures");
    HashMap m; hm_init(&m);
    hm_put(&m, "RELIANCE", 3); hm_put(&m, "NVDA", 7);
    CHECK(hm_get(&m, "NVDA") == 7 && hm_get(&m, "nvda") == 7 && hm_get(&m, "XYZ") == -1, "Hash table: lookup is case-insensitive; unknown ticker returns -1");

    Trie t; trie_init(&t);
    trie_add(&t, "TCS", 1); trie_add(&t, "TATA", 1); trie_add(&t, "TATA", 2); trie_add(&t, "TMPV", 2);
    int out[8];
    CHECK(trie_find(&t, "ta", out, 8) == 2, "Trie: 'ta' finds both Tata companies");
    CHECK(trie_find(&t, "", out, 8) == 0 && trie_find(&t, "zz", out, 8) == 0 && trie_find(&t, "T$", out, 8) == 0, "Trie: empty / unknown / invalid prefix gives nothing");
    trie_free(&t);

    MaxHeap h; heap_init(&h);
    float keys[] = { 40, 90, 10, 75, 60, 99, 5 };
    for (int i = 0; i < 7; i++) heap_push(&h, keys[i], i);
    HeapItem it; float prev = 1e9f; bool ok = heap_valid(&h);
    while (heap_pop(&h, &it)) { if (it.key > prev) ok = false; prev = it.key; }
    CHECK(ok, "Max-heap: scores come out highest first");
    CHECK(!heap_pop(&h, &it), "Max-heap: pop from empty heap is refused");

    Graph g; graph_init(&g, 6);
    graph_link(&g, 0, 1, 0.8f); graph_link(&g, 1, 2, 0.7f); graph_link(&g, 3, 4, 0.9f);
    CHECK(graph_components(&g) == 3 && g.comp[0] == g.comp[2] && g.comp[3] == g.comp[4] && g.comp[5] != g.comp[0], "Graph: BFS finds 3 clusters {0,1,2} {3,4} {5}");

    float v[200]; for (int i = 0; i < 200; i++) v[i] = (float)sin(i * 0.37) * 100 + i;
    SegTree s = { 0 }; seg_build(&s, v, 200); bool sok = true;
    for (int q = 0; q < 300; q++) {
        int l = rand() % 200, r = l + rand() % (200 - l);
        float lo, hi, blo = 1e30f, bhi = -1e30f; seg_query(&s, l, r, &lo, &hi);
        for (int i = l; i <= r; i++) { if (v[i] < blo) blo = v[i]; if (v[i] > bhi) bhi = v[i]; }
        if (lo != blo || hi != bhi) sok = false;
    }
    CHECK(sok, "Segment tree: 300 random range high/low queries match a full scan");
    seg_free(&s);

    JobQueue q; queue_init(&q); Job j;
    CHECK(!queue_pop(&q, &j), "Queue: empty queue gives nothing");
    for (int i = 0; i < Q_CAP; i++) queue_push(&q, (Job){ JOB_PRICES, i });
    CHECK(!queue_push(&q, (Job){ JOB_AI, 0 }), "Queue: full queue refuses new jobs");
    queue_pop(&q, &j);
    CHECK(j.stock == 0, "Queue: first job in is the first job out");

    NewsItem *n = NULL;
    n = news_push_front(n, "old", "A", "1 Nov"); n = news_push_front(n, "new", "B", "2 Nov");
    CHECK(news_count(n) == 2 && !strcmp(n->title, "new"), "Linked list: newest headline is at the front");
    news_free(n);

    NavStack ns = { .top = 0 };
    nav_push(&ns, 1); nav_push(&ns, 2);
    CHECK(nav_pop(&ns, 0) == 2 && nav_pop(&ns, 0) == 1 && nav_pop(&ns, 9) == 9, "Stack: Back returns screens in reverse; empty stack falls back");
}

static void test_parsers(void) {
    SECTION("Live data and AI reply parsing");
    const char *yahoo = "{\"chart\":{\"result\":[{\"timestamp\":[1763337600,1763424000,1763510400],"
                        "\"indicators\":{\"quote\":[{\"close\":[1300.5,null,1367.2]}]}}]}}";
    int d[MAX_DAYS]; float c[MAX_DAYS]; int n;
    net_parse_yahoo(yahoo, d, c, &n, MAX_DAYS);
    CHECK(n == 2 && fabsf(c[1] - 1367.2f) < 0.01f, "Yahoo prices: null (holiday) days are skipped");
    CHECK(!net_parse_yahoo("not json", d, c, &n, MAX_DAYS), "Yahoo prices: garbage input is rejected safely");

    const char *rss = "<rss><channel><item><title>Nvidia beats estimates &amp; raises outlook - Reuters</title>"
                      "<pubDate>Wed, 18 Nov 2026 22:10:00 GMT</pubDate><source url=\"x\">Reuters</source></item>"
                      "<item><title><![CDATA[Huang out as CEO]]></title><pubDate>Thu, 12 Nov 2026 10:00:00 GMT</pubDate><source url=\"y\">TheStreet</source></item></channel></rss>";
    char T[4][220], S[4][60], D[4][40];
    int k = net_parse_rss(rss, T, S, D, 4);
    CHECK(k == 2 && !strcmp(T[0], "Nvidia beats estimates & raises outlook") && !strcmp(S[0], "Reuters") && !strcmp(D[0], "18 Nov"), "News RSS: title, source, date; '&amp;' decoded; ' - Source' removed");
    CHECK(!strcmp(T[1], "Huang out as CEO"), "News RSS: CDATA titles are read");

    Analysis a;
    const char *ai = "```json\n{\"summary\":\"CEO left.\",\"sentiment\":-2,\"vol_mult\":5,\"drift_tilt\":-0.5,\"event_prob\":0.9,\"event_size\":-0.9,"
                     "\"factors\":[{\"category\":\"Leadership\",\"impact\":-7,\"text\":\"CEO resigned | suddenly\"}]}\n```";
    bool ok = net_parse_analysis_json(ai, &a);
    CHECK(ok && a.nf == 1 && !strcmp(a.f[0].category, "Leadership"), "AI reply: JSON inside ```fences``` is accepted");
    CHECK(a.sentiment == -1 && a.vol_mult == 2.0f && a.drift_tilt == -0.20f && a.event_prob == 0.35f && a.event_size == -0.6f && a.f[0].impact == -3,
          "AI reply: out-of-range numbers are clamped (an AI can't push risk to absurd values)");
    CHECK(!strchr(a.f[0].text, '|'), "AI reply: characters that would break the save file are removed");
    CHECK(!net_parse_analysis_json("Sorry, I can't help with that.", &a), "AI reply: a non-JSON answer is rejected");
}

static void test_engine(void) {
    SECTION("Risk engine and advisor");
    static Universe u; uni_init(&u, "data"); uni_load_cache(&u); uni_load_analyses(&u); uni_compute_stats(&u);
    int nv = uni_find(&u, "NVDA"), hul = uni_find(&u, "HINDUNILVR"), nifty = uni_find(&u, "NIFTYBEES");
    CHECK(nv >= 0 && hul >= 0 && u.s[nv].ai.valid, "Universe loads with saved AI analyses");
    Outcome o; risk_stock_outcome(&u, hul, 100000, 252, &o, 1);
    CHECK(o.p[0] < o.p[1] && o.p[1] < o.p[2] && o.p[2] < o.p[3] && o.p[3] < o.p[4], "Outcome percentiles are in order: bad < ... < good");
    CHECK(o.cvar95 >= o.var95, "Average of the worst 5% is at least as bad as the 1-in-20 bad year");
    Outcome on; risk_stock_outcome(&u, nv, 100000, 252, &on, 1);
    CHECK(on.p[4] - on.p[0] > o.p[4] - o.p[0], "Nvidia (CEO exit, high volatility) has a wider range than Hindustan Unilever");
    float c19, c08, f19, f08; risk_stress(&u, nv, &c19, &c08); risk_stress(&u, nifty, &f19, &f08);
    CHECK(c19 < f19 && c08 < c19 && c08 >= -0.95f, "Crash tests: Nvidia falls more than the index; 2008 worse than Covid; never below -95%");

    Analysis keep = u.s[hul].ai;
    u.s[hul].ai.vol_mult = 1.8f; u.s[hul].ai.event_prob = 0.3f; u.s[hul].ai.event_size = -0.4f;
    Outcome o2; risk_stock_outcome(&u, hul, 100000, 252, &o2, 1);
    CHECK(o2.cvar95 > o.cvar95 && o2.prob_loss > o.prob_loss, "Bad news from the AI makes the predicted losses bigger");
    u.s[hul].ai = keep;

    Profile p = { 0 }; strcpy(p.name, "T"); p.age = 22; p.lump = 500000; p.sip = 15000; p.markets = 3; p.done = true;
    int qz[4] = { 1, 1, 0, 1 }; memcpy(p.quiz, qz, sizeof qz);
    p.goals[0] = (Goal){ G_HOUSE, "House", 2000000, 8 }; p.ngoals = 1;
    profile_compute_risk(&p);
    Profile old = p; old.age = 60; profile_compute_risk(&old);
    CHECK(p.risk > old.risk, "Same answers: a 22-year-old gets a higher risk level than a 60-year-old");
    Profile soon = p; soon.goals[0].years = 1; profile_compute_risk(&soon);
    CHECK(soon.risk < p.risk, "A goal next year lowers the risk level");

    static Plan pl; advisor_build_plan(&u, &p, &pl);
    float sum = 0; bool cap = true;
    for (int k = 0; k < pl.npicks; k++) { sum += pl.picks[k].weight; if (pl.picks[k].stock >= 0 && pl.picks[k].weight > 0.36f) cap = false; }
    CHECK(fabsf(sum - 1) < 0.01f, "Plan weights add up to 100%");
    CHECK(cap, "No single stock is more than 35% of the plan");
    bool div = true;
    for (int a1 = 0; a1 < pl.npicks; a1++) for (int b1 = a1 + 1; b1 < pl.npicks; b1++) {
        int x = pl.picks[a1].stock, y = pl.picks[b1].stock;
        if (x >= 0 && y >= 0 && !u.s[x].fund && !u.s[y].fund && u.corr.edge[x][y]) div = false;
    }
    CHECK(div, "Diversified: no two picked companies are linked in the correlation graph");
    CHECK(pl.fan[0][FAN_STEPS - 1] <= pl.fan[2][FAN_STEPS - 1] && pl.fan[2][FAN_STEPS - 1] <= pl.fan[4][FAN_STEPS - 1], "12-month fan: bad <= likely <= good");
    CHECK(pl.goal_prob[0] >= 0 && pl.goal_prob[0] <= 1, "Goal probability is between 0% and 100%");

    Profile soon2 = p; soon2.goals[0].years = 1; static Plan pn; advisor_build_plan(&u, &soon2, &pn);
    CHECK(pn.equity < pl.equity, "Money needed next year: much more of it is kept safe");

    Profile in_only = p; in_only.markets = 1; static Plan pi; advisor_build_plan(&u, &in_only, &pi);
    bool only = true; for (int k = 0; k < pi.npicks; k++) if (pi.picks[k].stock >= 0 && u.s[pi.picks[k].stock].mkt != MKT_IN) only = false;
    CHECK(only, "India-only investor gets only Indian stocks");

    Verdict v; Outcome fo;
    advisor_friend_check(&u, &p, nv, 100000, 0, &v, &fo);
    CHECK(v.verdict == 2 && v.nalt > 0, "Friend's tip 'NVDA' for a Balanced investor: Risky, with safer alternatives");
    bool flag = false; for (int k = 0; k < v.nreasons; k++) if (strstr(v.reasons[k], "Leadership")) flag = true;
    CHECK(flag, "The CEO resignation is shown as a news red flag");
    advisor_friend_check(&u, &p, nifty, 400000, 0, &v, &fo);
    bool conc = false; for (int k = 0; k < v.nreasons; k++) if (strstr(v.reasons[k], "single company")) conc = true;
    CHECK(conc, "Putting 80% of your money in one stock triggers a concentration warning");

    SECTION("Accounts");
    { static Users us; memset(&us, 0, sizeof us); hm_init(&us.map);
      int a1 = users_add(&us, "Aarav", "4821"), a2 = users_add(&us, "Riya", "1234");
      CHECK(a1 == 0 && a2 == 1 && users_add(&us, "aarav", "0000") == -1, "Accounts are unique by name (hash table, any capitals)");
      CHECK(users_find(&us, "RIYA") == 1 && users_find(&us, "Zoya") == -1, "Accounts are found by name in O(1)");
      CHECK(users_check(&us, 0, "4821") && !users_check(&us, 0, "4822") && !users_check(&us, 1, "4821"), "The right PIN signs in, a wrong one doesn't");
      CHECK(us.u[0].hash != 4821 && us.u[0].hash != us.u[1].hash, "PINs are never stored, only salted hashes");
      users_save(&us, "test_tmp"); Users back; users_load(&back, "test_tmp");
      CHECK(back.n == 2 && users_check(&back, 1, "1234") && users_find(&back, "aarav") == 0, "Accounts survive a restart"); }

    SECTION("Portfolio");
    { static Portfolio pf; memset(&pf, 0, sizeof pf);
      const Stock *rs = &u.s[uni_find(&u, "RELIANCE")];
      int mid = rs->date[rs->n / 2];
      CHECK(price_index(rs, mid) == rs->n / 2 && price_index(rs, rs->date[0] - 1) == -1 && price_index(rs, 99991231) == rs->n - 1, "Binary search finds the price on any date");
      port_add(&pf, &u, port_months_ago(&u, 6), "RELIANCE", 10000);
      port_add(&pf, &u, port_months_ago(&u, 12), "DEPOSIT", 10000);
      port_add(&pf, &u, port_today(&u), "TCS", 5000);
      double inv, val; port_totals(&u, &pf, &inv, &val);
      CHECK(pf.n == 3 && fabs(inv - 25000) < 1e-6, "Invested amount adds up");
      CHECK(pf.head->date >= pf.head->next->date && pf.head->next->date >= pf.head->next->next->date, "Transactions are kept newest first (linked list)");
      double dep = txn_value(&u, pf.head->next->next, port_today(&u));
      CHECK(dep > 10600 && dep < 10800, "A deposit grows about 7% in a year");
      double rel = txn_value(&u, pf.head->next, port_today(&u));
      CHECK(fabs(rel - 10000 * rs->close[rs->n - 1] / pf.head->next->price) < 1, "A stock's value follows its real price since the purchase day");
      bool ok; double x = port_xirr(&u, &pf, &ok);
      double f = val; int td = port_today(&u);
      for (Txn *t = pf.head; t; t = t->next) f -= t->amount * pow(1 + x, port_days(t->date, td) / 365.0);
      CHECK(ok && fabs(f) < 1, "XIRR (Newton's method) balances the cash flows");
      port_save(&pf, "test_tmp", "txn_test.txt"); static Portfolio back; memset(&back, 0, sizeof back); port_load(&back, "test_tmp", "txn_test.txt");
      double i2, v2; port_totals(&u, &back, &i2, &v2);
      CHECK(back.n == 3 && fabs(v2 - val) < 1, "Portfolio survives a restart");
      port_free(&pf); port_free(&back); }

    SECTION("Alerts");
    { static AlertBook ab; memset(&ab, 0, sizeof ab);
      int ni = uni_find(&u, "NVDA"), ri = uni_find(&u, "RELIANCE");
      float np = u.s[ni].price, rp = u.s[ri].price;
      alerts_add(&ab, "NVDA", ALERT_BELOW, np * 1.30f);        /* far past its line: most urgent */
      alerts_add(&ab, "RELIANCE", ALERT_BELOW, rp * 1.05f);    /* just past */
      alerts_add(&ab, "RELIANCE", ALERT_ABOVE, rp * 1.50f);    /* not triggered */
      static MaxHeap h; int n = alerts_check(&u, &ab, &h);
      HeapItem top; heap_pop(&h, &top);
      CHECK(n == 2 && top.id == 0, "Triggered alerts come out most urgent first (max-heap)");
      ab.a[0].dismissed = true; n = alerts_check(&u, &ab, &h);
      CHECK(n == 1 && h.a[0].id == 1, "A dismissed alert stops showing");
      alerts_save(&ab, "test_tmp", "alerts_test.txt"); static AlertBook bb; alerts_load(&bb, "test_tmp", "alerts_test.txt");
      CHECK(bb.n == 3 && bb.a[0].dismissed && bb.a[2].kind == ALERT_ABOVE, "Alerts survive a restart"); }

    SECTION("Ask Vardhan and Proof");
    CHECK(chat_amount("put 50k in it") == 50000 && chat_amount("Rs 2 lakh") == 200000 && chat_amount("1.5L please") == 150000
          && chat_amount("50,000 for 3 years") == 50000 && chat_amount("in 5 years") == 0, "Money amounts are understood: 50k, 2 lakh, 1.5L, 50,000");
    int ids[4]; int ns = chat_find_stocks(&u, "Should I put 50k in nvda or Reliance?", ids, 4);
    CHECK(ns == 2 && !strcmp(u.s[ids[0]].sym, "NVDA") && !strcmp(u.s[ids[1]].sym, "RELIANCE"), "Stocks in a question are found (hash table + trie)");
    CHECK(chat_find_stocks(&u, "How much could I lose in a bad year?", ids, 4) == 0, "Ordinary words are not mistaken for stocks");
    static char ans[2400]; chat_offline(&u, &p, &pl, "Should I put 1 lakh in Nvidia?", ans, sizeof ans);
    CHECK(strstr(ans, "Nvidia") || strstr(ans, "NVIDIA") || strstr(ans, "NVDA"), "Instant answer works with no AI and names the stock");
    static char pr_[16000]; chat_prompt(&u, &p, &pl, "", "Is Reliance good for my house?", pr_, sizeof pr_);
    CHECK(strstr(pr_, "ONLY the facts") && strstr(pr_, "STOCK Reliance") && strstr(pr_, "GOAL: House"), "AI prompt is grounded in the user's plan and the stock's numbers");
    static Backtest bt; bool bok = bt_run(&u, &p, &bt);
    CHECK(bok && bt.plan.ready && bt.actual[0] > 0, "Back-test rebuilds the plan from one-year-old prices");
    CHECK(bt.tests > 100 && bt.breaks >= 0 && bt.breaks < bt.tests, "VaR calibration is tested across history");
    CHECK(u.s[nv].n > 400, "Back-test does not damage today's data");
    uni_free(&u);
}

static void test_profile_io(void) {
    SECTION("Saving your profile");
    Profile p = { 0 }; strcpy(p.name, "Riya"); p.age = 24; p.lump = 120000; p.sip = 8000; p.markets = 1;
    p.goals[0] = (Goal){ G_EDU, "MBA", 1800000, 4 }; p.ngoals = 1;
    profile_save(&p, "test_tmp");
    Profile q; bool ok = profile_load(&q, "test_tmp");
    CHECK(ok && !strcmp(q.name, "Riya") && q.age == 24 && q.lump == 120000 && q.ngoals == 1 && q.goals[0].target == 1800000 && !strcmp(q.goals[0].name, "MBA"), "Profile is saved and loaded back exactly");
    Profile e; CHECK(!profile_load(&e, "no_such_folder"), "Missing profile file is handled (shows onboarding)");
    remove("test_tmp/profile.txt");
}

int main(void) {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE); DWORD md = 0; if (GetConsoleMode(h, &md)) SetConsoleMode(h, md | 0x0004);
#endif
    printf("\n\x1b[1mVardhan tests\x1b[0m\n");
    if (system(
#ifdef _WIN32
        "mkdir test_tmp >NUL 2>&1"
#else
        "mkdir -p test_tmp"
#endif
    )) {}
    test_ds();
    test_parsers();
    test_engine();
    test_profile_io();
    printf("\n\x1b[1m%d passed, %d failed\x1b[0m\n\n", pass, fail);
    return fail ? 1 : 0;
}
